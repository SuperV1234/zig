/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of zig, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "analyze.hpp"
#include "semantic_info.hpp"
#include "error.hpp"
#include "zig_llvm.hpp"
#include "os.hpp"

struct BlockContext {
    AstNode *node;
    BlockContext *root;
    BlockContext *parent;
};

void add_node_error(CodeGen *g, AstNode *node, Buf *msg) {
    ErrorMsg *err = allocate<ErrorMsg>(1);
    err->line_start = node->line;
    err->column_start = node->column;
    err->line_end = -1;
    err->column_end = -1;
    err->msg = msg;
    err->path = node->owner->path;
    err->source = node->owner->source_code;
    err->line_offsets = node->owner->line_offsets;

    g->errors.append(err);
}

static int parse_version_string(Buf *buf, int *major, int *minor, int *patch) {
    char *dot1 = strstr(buf_ptr(buf), ".");
    if (!dot1)
        return ErrorInvalidFormat;
    char *dot2 = strstr(dot1 + 1, ".");
    if (!dot2)
        return ErrorInvalidFormat;

    *major = (int)strtol(buf_ptr(buf), nullptr, 10);
    *minor = (int)strtol(dot1 + 1, nullptr, 10);
    *patch = (int)strtol(dot2 + 1, nullptr, 10);

    return ErrorNone;
}

static void set_root_export_version(CodeGen *g, Buf *version_buf, AstNode *node) {
    int err;
    if ((err = parse_version_string(version_buf, &g->version_major, &g->version_minor, &g->version_patch))) {
        add_node_error(g, node,
                buf_sprintf("invalid version string"));
    }
}

TypeTableEntry *get_pointer_to_type(CodeGen *g, TypeTableEntry *child_type, bool is_const) {
    TypeTableEntry **parent_pointer = is_const ?
        &child_type->pointer_const_parent :
        &child_type->pointer_mut_parent;
    const char *const_or_mut_str = is_const ? "const" : "mut";
    if (*parent_pointer) {
        return *parent_pointer;
    } else {
        TypeTableEntry *entry = allocate<TypeTableEntry>(1);
        entry->type_ref = LLVMPointerType(child_type->type_ref, 0);
        buf_resize(&entry->name, 0);
        buf_appendf(&entry->name, "*%s %s", const_or_mut_str, buf_ptr(&child_type->name));
        entry->di_type = LLVMZigCreateDebugPointerType(g->dbuilder, child_type->di_type,
                g->pointer_size_bytes * 8, g->pointer_size_bytes * 8, buf_ptr(&entry->name));
        g->type_table.put(&entry->name, entry);
        *parent_pointer = entry;
        return entry;
    }
}

static void resolve_type(CodeGen *g, AstNode *node) {
    assert(!node->codegen_node);
    node->codegen_node = allocate<CodeGenNode>(1);
    TypeNode *type_node = &node->codegen_node->data.type_node;
    switch (node->data.type.type) {
        case AstNodeTypeTypePrimitive:
            {
                Buf *name = &node->data.type.primitive_name;
                auto table_entry = g->type_table.maybe_get(name);
                if (table_entry) {
                    type_node->entry = table_entry->value;
                } else {
                    add_node_error(g, node,
                            buf_sprintf("invalid type name: '%s'", buf_ptr(name)));
                    type_node->entry = g->builtin_types.entry_invalid;
                }
                break;
            }
        case AstNodeTypeTypePointer:
            {
                resolve_type(g, node->data.type.child_type);
                TypeTableEntry *child_type = node->data.type.child_type->codegen_node->data.type_node.entry;
                if (child_type == g->builtin_types.entry_unreachable) {
                    add_node_error(g, node,
                            buf_create_from_str("pointer to unreachable not allowed"));
                }
                type_node->entry = get_pointer_to_type(g, child_type, node->data.type.is_const);
                break;
            }
    }
}

static void resolve_function_proto(CodeGen *g, AstNode *node) {
    assert(node->type == NodeTypeFnProto);

    for (int i = 0; i < node->data.fn_proto.directives->length; i += 1) {
        AstNode *directive_node = node->data.fn_proto.directives->at(i);
        Buf *name = &directive_node->data.directive.name;
        add_node_error(g, directive_node,
                buf_sprintf("invalid directive: '%s'", buf_ptr(name)));
    }

    for (int i = 0; i < node->data.fn_proto.params.length; i += 1) {
        AstNode *child = node->data.fn_proto.params.at(i);
        assert(child->type == NodeTypeParamDecl);

        // parameter names are not important here.

        resolve_type(g, child->data.param_decl.type);
    }

    resolve_type(g, node->data.fn_proto.return_type);
}

static void preview_function_declarations(CodeGen *g, ImportTableEntry *import, AstNode *node) {
    switch (node->type) {
        case NodeTypeExternBlock:
            for (int i = 0; i < node->data.extern_block.directives->length; i += 1) {
                AstNode *directive_node = node->data.extern_block.directives->at(i);
                Buf *name = &directive_node->data.directive.name;
                Buf *param = &directive_node->data.directive.param;
                if (buf_eql_str(name, "link")) {
                    g->link_table.put(param, true);
                } else {
                    add_node_error(g, directive_node,
                            buf_sprintf("invalid directive: '%s'", buf_ptr(name)));
                }
            }

            for (int fn_decl_i = 0; fn_decl_i < node->data.extern_block.fn_decls.length; fn_decl_i += 1) {
                AstNode *fn_decl = node->data.extern_block.fn_decls.at(fn_decl_i);
                assert(fn_decl->type == NodeTypeFnDecl);
                AstNode *fn_proto = fn_decl->data.fn_decl.fn_proto;
                bool is_pub = (fn_proto->data.fn_proto.visib_mod == FnProtoVisibModPub);
                resolve_function_proto(g, fn_proto);
                Buf *name = &fn_proto->data.fn_proto.name;

                FnTableEntry *fn_table_entry = allocate<FnTableEntry>(1);
                fn_table_entry->proto_node = fn_proto;
                fn_table_entry->is_extern = true;
                fn_table_entry->calling_convention = LLVMCCallConv;
                fn_table_entry->import_entry = import;

                g->fn_protos.append(fn_table_entry);
                import->fn_table.put(name, fn_table_entry);
                if (is_pub) {
                    g->fn_table.put(name, fn_table_entry);
                }
            }
            break;
        case NodeTypeFnDef:
            {
                AstNode *proto_node = node->data.fn_def.fn_proto;
                assert(proto_node->type == NodeTypeFnProto);
                Buf *proto_name = &proto_node->data.fn_proto.name;
                auto entry = import->fn_table.maybe_get(proto_name);
                bool skip = false;
                bool is_internal = (proto_node->data.fn_proto.visib_mod != FnProtoVisibModExport);
                bool is_pub = (proto_node->data.fn_proto.visib_mod == FnProtoVisibModPub);
                if (entry) {
                    add_node_error(g, node,
                            buf_sprintf("redefinition of '%s'", buf_ptr(proto_name)));
                    assert(!node->codegen_node);
                    node->codegen_node = allocate<CodeGenNode>(1);
                    node->codegen_node->data.fn_def_node.skip = true;
                    skip = true;
                } else if (is_pub) {
                    auto entry = g->fn_table.maybe_get(proto_name);
                    if (entry) {
                        add_node_error(g, node,
                                buf_sprintf("redefinition of '%s'", buf_ptr(proto_name)));
                        assert(!node->codegen_node);
                        node->codegen_node = allocate<CodeGenNode>(1);
                        node->codegen_node->data.fn_def_node.skip = true;
                        skip = true;
                    }
                }
                if (!skip) {
                    FnTableEntry *fn_table_entry = allocate<FnTableEntry>(1);
                    fn_table_entry->import_entry = import;
                    fn_table_entry->proto_node = proto_node;
                    fn_table_entry->fn_def_node = node;
                    fn_table_entry->internal_linkage = is_internal;
                    fn_table_entry->calling_convention = is_internal ? LLVMFastCallConv : LLVMCCallConv;

                    g->fn_protos.append(fn_table_entry);
                    g->fn_defs.append(fn_table_entry);

                    import->fn_table.put(proto_name, fn_table_entry);
                    if (is_pub) {
                        g->fn_table.put(proto_name, fn_table_entry);
                    }

                    resolve_function_proto(g, proto_node);
                }
            }
            break;
        case NodeTypeRootExportDecl:
            if (import == g->root_import) {
                for (int i = 0; i < node->data.root_export_decl.directives->length; i += 1) {
                    AstNode *directive_node = node->data.root_export_decl.directives->at(i);
                    Buf *name = &directive_node->data.directive.name;
                    Buf *param = &directive_node->data.directive.param;
                    if (buf_eql_str(name, "version")) {
                        set_root_export_version(g, param, directive_node);
                    } else {
                        add_node_error(g, directive_node,
                                buf_sprintf("invalid directive: '%s'", buf_ptr(name)));
                    }
                }

                if (g->root_export_decl) {
                    add_node_error(g, node,
                            buf_sprintf("only one root export declaration allowed"));
                } else {
                    g->root_export_decl = node;

                    if (!g->root_out_name)
                        g->root_out_name = &node->data.root_export_decl.name;

                    Buf *out_type = &node->data.root_export_decl.type;
                    OutType export_out_type;
                    if (buf_eql_str(out_type, "executable")) {
                        export_out_type = OutTypeExe;
                    } else if (buf_eql_str(out_type, "library")) {
                        export_out_type = OutTypeLib;
                    } else if (buf_eql_str(out_type, "object")) {
                        export_out_type = OutTypeObj;
                    } else {
                        add_node_error(g, node,
                                buf_sprintf("invalid export type: '%s'", buf_ptr(out_type)));
                    }
                    if (g->out_type == OutTypeUnknown)
                        g->out_type = export_out_type;
                }
            } else {
                add_node_error(g, node,
                        buf_sprintf("root export declaration only valid in root source file"));
            }
            break;
        case NodeTypeUse:
            // nothing to do here
            break;
        case NodeTypeDirective:
        case NodeTypeParamDecl:
        case NodeTypeFnProto:
        case NodeTypeType:
        case NodeTypeFnDecl:
        case NodeTypeReturnExpr:
        case NodeTypeRoot:
        case NodeTypeBlock:
        case NodeTypeBinOpExpr:
        case NodeTypeFnCallExpr:
        case NodeTypeNumberLiteral:
        case NodeTypeStringLiteral:
        case NodeTypeUnreachable:
        case NodeTypeVoid:
        case NodeTypeSymbol:
        case NodeTypeCastExpr:
        case NodeTypePrefixOpExpr:
        case NodeTypeIfExpr:
            zig_unreachable();
    }
}

static TypeTableEntry * get_return_type(BlockContext *context) {
    AstNode *fn_def_node = context->root->node;
    assert(fn_def_node->type == NodeTypeFnDef);
    AstNode *fn_proto_node = fn_def_node->data.fn_def.fn_proto;
    assert(fn_proto_node->type == NodeTypeFnProto);
    AstNode *return_type_node = fn_proto_node->data.fn_proto.return_type;
    assert(return_type_node->codegen_node);
    return return_type_node->codegen_node->data.type_node.entry;
}

static void check_type_compatibility(CodeGen *g, AstNode *node, TypeTableEntry *expected_type, TypeTableEntry *actual_type) {
    if (expected_type == nullptr)
        return; // anything will do
    if (expected_type == actual_type)
        return; // match
    if (expected_type == g->builtin_types.entry_invalid || actual_type == g->builtin_types.entry_invalid)
        return; // already complained
    if (actual_type == g->builtin_types.entry_unreachable)
        return; // TODO: is this true?

    // TODO better error message
    add_node_error(g, node, buf_sprintf("type mismatch. expected %s. got %s", buf_ptr(&expected_type->name), buf_ptr(&actual_type->name)));
}

static TypeTableEntry * analyze_expression(CodeGen *g, ImportTableEntry *import, BlockContext *context,
        TypeTableEntry *expected_type, AstNode *node)
{
    TypeTableEntry *return_type = nullptr;
    switch (node->type) {
        case NodeTypeBlock:
            {
                // TODO: nested block scopes
                return_type = g->builtin_types.entry_void;
                for (int i = 0; i < node->data.block.statements.length; i += 1) {
                    AstNode *child = node->data.block.statements.at(i);
                    if (return_type == g->builtin_types.entry_unreachable) {
                        if (child->type == NodeTypeVoid) {
                            // {unreachable;void;void} is allowed.
                            // ignore void statements once we enter unreachable land.
                            continue;
                        }
                        add_node_error(g, child, buf_sprintf("unreachable code"));
                        break;
                    }
                    return_type = analyze_expression(g, import, context, nullptr, child);
                }
                break;
            }

        case NodeTypeReturnExpr:
            {
                TypeTableEntry *expected_return_type = get_return_type(context);
                TypeTableEntry *actual_return_type;
                if (node->data.return_expr.expr) {
                    actual_return_type = analyze_expression(g, import, context, expected_return_type, node->data.return_expr.expr);
                } else {
                    actual_return_type = g->builtin_types.entry_void;
                }

                if (actual_return_type == g->builtin_types.entry_unreachable) {
                    // "return exit(0)" should just be "exit(0)".
                    add_node_error(g, node, buf_sprintf("returning is unreachable."));
                    actual_return_type = g->builtin_types.entry_invalid;
                }

                check_type_compatibility(g, node, expected_return_type, actual_return_type);
                return_type = g->builtin_types.entry_unreachable;
                break;
            }

        case NodeTypeBinOpExpr:
            {
                switch (node->data.bin_op_expr.bin_op) {
                    case BinOpTypeBoolOr:
                    case BinOpTypeBoolAnd:
                        analyze_expression(g, import, context, g->builtin_types.entry_bool,
                                node->data.bin_op_expr.op1);
                        analyze_expression(g, import, context, g->builtin_types.entry_bool,
                                node->data.bin_op_expr.op2);
                        return_type = g->builtin_types.entry_bool;
                        break;
                    case BinOpTypeCmpEq:
                    case BinOpTypeCmpNotEq:
                    case BinOpTypeCmpLessThan:
                    case BinOpTypeCmpGreaterThan:
                    case BinOpTypeCmpLessOrEq:
                    case BinOpTypeCmpGreaterOrEq:
                        // TODO think how should type checking for these work?
                        analyze_expression(g, import, context, g->builtin_types.entry_i32,
                                node->data.bin_op_expr.op1);
                        analyze_expression(g, import, context, g->builtin_types.entry_i32,
                                node->data.bin_op_expr.op2);
                        return_type = g->builtin_types.entry_bool;
                        break;
                    case BinOpTypeBinOr:
                        zig_panic("TODO bin or type");
                        break;
                    case BinOpTypeBinXor:
                        zig_panic("TODO bin xor type");
                        break;
                    case BinOpTypeBinAnd:
                        zig_panic("TODO bin and type");
                        break;
                    case BinOpTypeBitShiftLeft:
                        zig_panic("TODO bit shift left type");
                        break;
                    case BinOpTypeBitShiftRight:
                        zig_panic("TODO bit shift right type");
                        break;
                    case BinOpTypeAdd:
                    case BinOpTypeSub:
                        // TODO think how should type checking for these work?
                        analyze_expression(g, import, context, g->builtin_types.entry_i32,
                                node->data.bin_op_expr.op1);
                        analyze_expression(g, import, context, g->builtin_types.entry_i32,
                                node->data.bin_op_expr.op2);
                        return_type = g->builtin_types.entry_i32;
                        break;
                    case BinOpTypeMult:
                        zig_panic("TODO mult type");
                        break;
                    case BinOpTypeDiv:
                        zig_panic("TODO div type");
                        break;
                    case BinOpTypeMod:
                        zig_panic("TODO modulus type");
                        break;
                    case BinOpTypeInvalid:
                        zig_unreachable();
                }
                break;
            }

        case NodeTypeFnCallExpr:
            {
                Buf *name = hack_get_fn_call_name(g, node->data.fn_call_expr.fn_ref_expr);

                auto entry = import->fn_table.maybe_get(name);
                if (!entry)
                    entry = g->fn_table.maybe_get(name);

                if (!entry) {
                    add_node_error(g, node,
                            buf_sprintf("undefined function: '%s'", buf_ptr(name)));
                    // still analyze the parameters, even though we don't know what to expect
                    for (int i = 0; i < node->data.fn_call_expr.params.length; i += 1) {
                        AstNode *child = node->data.fn_call_expr.params.at(i);
                        analyze_expression(g, import, context, nullptr, child);
                    }

                    return_type = g->builtin_types.entry_invalid;
                } else {
                    FnTableEntry *fn_table_entry = entry->value;
                    assert(fn_table_entry->proto_node->type == NodeTypeFnProto);
                    AstNodeFnProto *fn_proto = &fn_table_entry->proto_node->data.fn_proto;

                    // count parameters
                    int expected_param_count = fn_proto->params.length;
                    int actual_param_count = node->data.fn_call_expr.params.length;
                    if (expected_param_count != actual_param_count) {
                        add_node_error(g, node,
                                buf_sprintf("wrong number of arguments. Expected %d, got %d.",
                                    expected_param_count, actual_param_count));
                    }

                    // analyze each parameter
                    for (int i = 0; i < node->data.fn_call_expr.params.length; i += 1) {
                        AstNode *child = node->data.fn_call_expr.params.at(i);
                        // determine the expected type for each parameter
                        TypeTableEntry *expected_param_type = nullptr;
                        if (i < fn_proto->params.length) {
                            AstNode *param_decl_node = fn_proto->params.at(i);
                            assert(param_decl_node->type == NodeTypeParamDecl);
                            AstNode *param_type_node = param_decl_node->data.param_decl.type;
                            if (param_type_node->codegen_node)
                                expected_param_type = param_type_node->codegen_node->data.type_node.entry;
                        }
                        analyze_expression(g, import, context, expected_param_type, child);
                    }

                    return_type = fn_proto->return_type->codegen_node->data.type_node.entry;
                }
                break;
            }

        case NodeTypeNumberLiteral:
            // TODO: generic literal int type
            return_type = g->builtin_types.entry_i32;
            break;

        case NodeTypeStringLiteral:
            return_type = g->builtin_types.entry_string_literal;
            break;

        case NodeTypeUnreachable:
            return_type = g->builtin_types.entry_unreachable;
            break;

        case NodeTypeVoid:
            return_type = g->builtin_types.entry_void;
            break;

        case NodeTypeSymbol:
            // look up symbol in symbol table
            zig_panic("TODO analyze_expression symbol");

        case NodeTypeCastExpr:
            zig_panic("TODO analyze_expression cast expr");
            break;

        case NodeTypePrefixOpExpr:
            switch (node->data.prefix_op_expr.prefix_op) {
                case PrefixOpBoolNot:
                    analyze_expression(g, import, context, g->builtin_types.entry_bool,
                            node->data.prefix_op_expr.primary_expr);
                    return_type = g->builtin_types.entry_bool;
                    break;
                case PrefixOpBinNot:
                    zig_panic("TODO type check bin not");
                    break;
                case PrefixOpNegation:
                    zig_panic("TODO type check negation");
                    break;
                case PrefixOpInvalid:
                    zig_unreachable();
            }
            break;
        case NodeTypeIfExpr:
            {
                analyze_expression(g, import, context, g->builtin_types.entry_bool, node->data.if_expr.condition);

                TypeTableEntry *else_type;
                if (node->data.if_expr.else_node) {
                    else_type = analyze_expression(g, import, context, expected_type, node->data.if_expr.else_node);
                } else {
                    else_type = g->builtin_types.entry_void;
                }
                TypeTableEntry *then_type = analyze_expression(g, import, context, expected_type,
                        node->data.if_expr.then_block);

                check_type_compatibility(g, node, expected_type, else_type);
                return_type = then_type;
                break;
            }
        case NodeTypeDirective:
        case NodeTypeFnDecl:
        case NodeTypeFnProto:
        case NodeTypeParamDecl:
        case NodeTypeType:
        case NodeTypeRoot:
        case NodeTypeRootExportDecl:
        case NodeTypeExternBlock:
        case NodeTypeFnDef:
        case NodeTypeUse:
            zig_unreachable();
    }
    assert(return_type);
    check_type_compatibility(g, node, expected_type, return_type);

    assert(!node->codegen_node);
    node->codegen_node = allocate<CodeGenNode>(1);
    node->codegen_node->data.expr_node.type_entry = return_type;

    return return_type;
}

static void analyze_top_level_declaration(CodeGen *g, ImportTableEntry *import, AstNode *node) {
    switch (node->type) {
        case NodeTypeFnDef:
            {
                if (node->codegen_node && node->codegen_node->data.fn_def_node.skip) {
                    // we detected an error with this function definition which prevents us
                    // from further analyzing it.
                    break;
                }

                AstNode *fn_proto_node = node->data.fn_def.fn_proto;
                assert(fn_proto_node->type == NodeTypeFnProto);

                AstNodeFnProto *fn_proto = &fn_proto_node->data.fn_proto;
                for (int i = 0; i < fn_proto->params.length; i += 1) {
                    AstNode *param_decl_node = fn_proto->params.at(i);
                    assert(param_decl_node->type == NodeTypeParamDecl);
                    // TODO: define local variables for parameters
                }

                BlockContext context;
                context.node = node;
                context.root = &context;
                context.parent = nullptr;
                TypeTableEntry *expected_type = fn_proto->return_type->codegen_node->data.type_node.entry;
                TypeTableEntry *block_return_type = analyze_expression(g, import, &context, expected_type, node->data.fn_def.body);

                node->codegen_node = allocate<CodeGenNode>(1);
                node->codegen_node->data.fn_def_node.implicit_return_type = block_return_type;
            }
            break;

        case NodeTypeRootExportDecl:
        case NodeTypeExternBlock:
            // already looked at these in the preview pass
            break;
        case NodeTypeUse:
            for (int i = 0; i < node->data.use.directives->length; i += 1) {
                AstNode *directive_node = node->data.use.directives->at(i);
                Buf *name = &directive_node->data.directive.name;
                add_node_error(g, directive_node,
                        buf_sprintf("invalid directive: '%s'", buf_ptr(name)));
            }
            break;
        case NodeTypeDirective:
        case NodeTypeParamDecl:
        case NodeTypeFnProto:
        case NodeTypeType:
        case NodeTypeFnDecl:
        case NodeTypeReturnExpr:
        case NodeTypeRoot:
        case NodeTypeBlock:
        case NodeTypeBinOpExpr:
        case NodeTypeFnCallExpr:
        case NodeTypeNumberLiteral:
        case NodeTypeStringLiteral:
        case NodeTypeUnreachable:
        case NodeTypeVoid:
        case NodeTypeSymbol:
        case NodeTypeCastExpr:
        case NodeTypePrefixOpExpr:
        case NodeTypeIfExpr:
            zig_unreachable();
    }
}

static void find_function_declarations_root(CodeGen *g, ImportTableEntry *import, AstNode *node) {
    assert(node->type == NodeTypeRoot);

    for (int i = 0; i < node->data.root.top_level_decls.length; i += 1) {
        AstNode *child = node->data.root.top_level_decls.at(i);
        preview_function_declarations(g, import, child);
    }

}

static void analyze_top_level_decls_root(CodeGen *g, ImportTableEntry *import, AstNode *node) {
    assert(node->type == NodeTypeRoot);

    for (int i = 0; i < node->data.root.top_level_decls.length; i += 1) {
        AstNode *child = node->data.root.top_level_decls.at(i);
        analyze_top_level_declaration(g, import, child);
    }
}

void semantic_analyze(CodeGen *g) {
    {
        auto it = g->import_table.entry_iterator();
        for (;;) {
            auto *entry = it.next();
            if (!entry)
                break;

            ImportTableEntry *import = entry->value;
            find_function_declarations_root(g, import, import->root);
        }
    }
    {
        auto it = g->import_table.entry_iterator();
        for (;;) {
            auto *entry = it.next();
            if (!entry)
                break;

            ImportTableEntry *import = entry->value;
            analyze_top_level_decls_root(g, import, import->root);
        }
    }


    if (!g->root_out_name) {
        add_node_error(g, g->root_import->root,
                buf_sprintf("missing export declaration and output name not provided"));
    } else if (g->out_type == OutTypeUnknown) {
        add_node_error(g, g->root_import->root,
                buf_sprintf("missing export declaration and export type not provided"));
    }
}