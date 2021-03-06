cmake_minimum_required(VERSION 2.8.5)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

project(zig C CXX)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

set(ZIG_VERSION_MAJOR 0)
set(ZIG_VERSION_MINOR 0)
set(ZIG_VERSION_PATCH 0)
set(ZIG_VERSION "${ZIG_VERSION_MAJOR}.${ZIG_VERSION_MINOR}.${ZIG_VERSION_PATCH}")
message("Configuring zig version ${ZIG_VERSION}")

set(ZIG_LIBC_LIB_DIR "" CACHE STRING "Default native target libc directory where crt1.o can be found")
set(ZIG_LIBC_STATIC_LIB_DIR "" CACHE STRING "Default native target libc directory where crtbeginT.o can be found")
set(ZIG_LIBC_INCLUDE_DIR "/usr/include" CACHE STRING "Default native target libc include directory")
set(ZIG_DYNAMIC_LINKER "" CACHE STRING "Override dynamic linker for native target")
set(ZIG_EACH_LIB_RPATH off CACHE BOOL "Add each dynamic library to rpath for native target")

option(ZIG_TEST_COVERAGE "Build Zig with test coverage instrumentation" OFF)



find_package(llvm)
include_directories(${LLVM_INCLUDE_DIR})
link_directories(${LLVM_LIBDIRS})

find_package(clang)
include_directories(${CLANG_INCLUDE_DIRS})

find_package(lld)
include_directories(${LLD_INCLUDE_DIRS})

find_package(Threads)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

set(ZIG_SOURCES
    "${CMAKE_SOURCE_DIR}/src/analyze.cpp"
    "${CMAKE_SOURCE_DIR}/src/ast_render.cpp"
    "${CMAKE_SOURCE_DIR}/src/bignum.cpp"
    "${CMAKE_SOURCE_DIR}/src/buffer.cpp"
    "${CMAKE_SOURCE_DIR}/src/c_tokenizer.cpp"
    "${CMAKE_SOURCE_DIR}/src/codegen.cpp"
    "${CMAKE_SOURCE_DIR}/src/errmsg.cpp"
    "${CMAKE_SOURCE_DIR}/src/error.cpp"
    "${CMAKE_SOURCE_DIR}/src/ir.cpp"
    "${CMAKE_SOURCE_DIR}/src/ir_print.cpp"
    "${CMAKE_SOURCE_DIR}/src/link.cpp"
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/os.cpp"
    "${CMAKE_SOURCE_DIR}/src/parser.cpp"
    "${CMAKE_SOURCE_DIR}/src/range_set.cpp"
    "${CMAKE_SOURCE_DIR}/src/target.cpp"
    "${CMAKE_SOURCE_DIR}/src/tokenizer.cpp"
    "${CMAKE_SOURCE_DIR}/src/util.cpp"
    "${CMAKE_SOURCE_DIR}/src/parseh.cpp"
    "${CMAKE_SOURCE_DIR}/src/zig_llvm.cpp"
)

set(ZIG_HOST_LINK_VERSION)
if (APPLE)
    set(LD_V_OUTPUT)
    execute_process(
        COMMAND sh -c "${CMAKE_LINKER} -v 2>&1 | head -1"
        RESULT_VARIABLE HAD_ERROR
        OUTPUT_VARIABLE LD_V_OUTPUT
    )
    if (NOT HAD_ERROR)
        if ("${LD_V_OUTPUT}" MATCHES ".*ld64-([0-9.]+).*")
            string(REGEX REPLACE ".*ld64-([0-9.]+).*" "\\1" ZIG_HOST_LINK_VERSION ${LD_V_OUTPUT})
        elseif ("${LD_V_OUTPUT}" MATCHES "[^0-9]*([0-9.]+).*")
            string(REGEX REPLACE "[^0-9]*([0-9.]+).*" "\\1" ZIG_HOST_LINK_VERSION ${LD_V_OUTPUT})
        endif()
    else()
        message(FATAL_ERROR "${CMAKE_LINKER} failed with status ${HAD_ERROR}")
    endif()
endif()


set(C_HEADERS_DEST "lib/zig/include")
set(ZIG_STD_DEST "lib/zig/std")
set(CONFIGURE_OUT_FILE "${CMAKE_BINARY_DIR}/config.h")
configure_file (
    "${CMAKE_SOURCE_DIR}/src/config.h.in"
    ${CONFIGURE_OUT_FILE}
)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
    "${CMAKE_SOURCE_DIR}/src"
)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Werror -Wall")


if(MINGW)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wno-error=format= -Wno-error=format -Wno-error=format-extra-args")
endif()

set(EXE_CFLAGS "-std=c++11 -fno-exceptions -fno-rtti -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -Werror=strict-prototypes -Werror=old-style-definition -Werror=type-limits -Wno-missing-braces")
set(EXE_LDFLAGS " ")
if(ZIG_TEST_COVERAGE)
    set(EXE_CFLAGS "${EXE_CFLAGS} -fprofile-arcs -ftest-coverage")
    set(EXE_LDFLAGS "-fprofile-arcs -ftest-coverage")
endif()

add_executable(zig ${ZIG_SOURCES})
set_target_properties(zig PROPERTIES
    COMPILE_FLAGS ${EXE_CFLAGS}
    LINK_FLAGS ${EXE_LDFLAGS}
)
target_link_libraries(zig LINK_PUBLIC
    ${CLANG_LIBRARIES}
    ${LLD_LIBRARIES}
    ${LLVM_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)
if(MINGW)
    target_link_libraries(zig LINK_PUBLIC version)
endif()
install(TARGETS zig DESTINATION bin)

install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__clang_cuda_builtin_vars.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__clang_cuda_cmath.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__clang_cuda_complex_builtins.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__clang_cuda_intrinsics.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__clang_cuda_math_forward_declares.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__clang_cuda_runtime_wrapper.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__stddef_max_align_t.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__wmmintrin_aes.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/__wmmintrin_pclmul.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/adxintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/altivec.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/ammintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/arm_acle.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/arm_neon.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/armintr.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx2intrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512bwintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512cdintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512dqintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512erintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512fintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512ifmaintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512ifmavlintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512pfintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512vbmiintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512vbmivlintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512vlbwintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512vlcdintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512vldqintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avx512vlintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/avxintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/bmi2intrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/bmiintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/clflushoptintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/cpuid.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/cuda_wrappers/algorithm" DESTINATION "${C_HEADERS_DEST}/cuda_wrappers")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/cuda_wrappers/complex" DESTINATION "${C_HEADERS_DEST}/cuda_wrappers")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/cuda_wrappers/new" DESTINATION "${C_HEADERS_DEST}/cuda_wrappers")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/emmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/f16cintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/float.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/fma4intrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/fmaintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/fxsrintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/htmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/htmxlintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/ia32intrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/immintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/intrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/inttypes.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/iso646.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/limits.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/lzcntintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/mm3dnow.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/mm_malloc.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/mmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/module.modulemap" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/msa.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/mwaitxintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/nmmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/opencl-c.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/pkuintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/pmmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/popcntintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/prfchwintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/rdseedintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/rtmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/s390intrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/shaintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/smmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/stdalign.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/stdarg.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/stdatomic.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/stdbool.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/stddef.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/stdint.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/stdnoreturn.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/tbmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/tgmath.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/tmmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/unwind.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/vadefs.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/varargs.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/vecintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/wmmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/x86intrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/xmmintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/xopintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/xsavecintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/xsaveintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/xsaveoptintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/xsavesintrin.h" DESTINATION "${C_HEADERS_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/c_headers/xtestintrin.h" DESTINATION "${C_HEADERS_DEST}")

install(FILES "${CMAKE_SOURCE_DIR}/std/array_list.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/base64.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/buf_map.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/buf_set.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/buffer.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/build.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/c/darwin.zig" DESTINATION "${ZIG_STD_DEST}/c")
install(FILES "${CMAKE_SOURCE_DIR}/std/c/index.zig" DESTINATION "${ZIG_STD_DEST}/c")
install(FILES "${CMAKE_SOURCE_DIR}/std/c/linux.zig" DESTINATION "${ZIG_STD_DEST}/c")
install(FILES "${CMAKE_SOURCE_DIR}/std/c/windows.zig" DESTINATION "${ZIG_STD_DEST}/c")
install(FILES "${CMAKE_SOURCE_DIR}/std/cstr.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/debug.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/dwarf.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/elf.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/empty.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/endian.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/fmt/index.zig" DESTINATION "${ZIG_STD_DEST}/fmt")
install(FILES "${CMAKE_SOURCE_DIR}/std/fmt/errol/index.zig" DESTINATION "${ZIG_STD_DEST}/fmt/errol")
install(FILES "${CMAKE_SOURCE_DIR}/std/fmt/errol/enum3.zig" DESTINATION "${ZIG_STD_DEST}/fmt/errol")
install(FILES "${CMAKE_SOURCE_DIR}/std/fmt/errol/lookup.zig" DESTINATION "${ZIG_STD_DEST}/fmt/errol")
install(FILES "${CMAKE_SOURCE_DIR}/std/hash_map.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/index.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/io.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/linked_list.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/math/index.zig" DESTINATION "${ZIG_STD_DEST}/math")
install(FILES "${CMAKE_SOURCE_DIR}/std/math/frexp.zig" DESTINATION "${ZIG_STD_DEST}/math")
install(FILES "${CMAKE_SOURCE_DIR}/std/math/fabs.zig" DESTINATION "${ZIG_STD_DEST}/math")
install(FILES "${CMAKE_SOURCE_DIR}/std/mem.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/net.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/child_process.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/darwin.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/darwin_x86_64.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/errno.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/index.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/linux.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/linux_i386.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/linux_x86_64.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/path.zig" DESTINATION "${ZIG_STD_DEST}/os")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/windows/index.zig" DESTINATION "${ZIG_STD_DEST}/os/windows")
install(FILES "${CMAKE_SOURCE_DIR}/std/os/windows/error.zig" DESTINATION "${ZIG_STD_DEST}/os/windows")
install(FILES "${CMAKE_SOURCE_DIR}/std/rand.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/sort.zig" DESTINATION "${ZIG_STD_DEST}")
install(FILES "${CMAKE_SOURCE_DIR}/std/special/bootstrap.zig" DESTINATION "${ZIG_STD_DEST}/special")
install(FILES "${CMAKE_SOURCE_DIR}/std/special/build_file_template.zig" DESTINATION "${ZIG_STD_DEST}/special")
install(FILES "${CMAKE_SOURCE_DIR}/std/special/build_runner.zig" DESTINATION "${ZIG_STD_DEST}/special")
install(FILES "${CMAKE_SOURCE_DIR}/std/special/builtin.zig" DESTINATION "${ZIG_STD_DEST}/special")
install(FILES "${CMAKE_SOURCE_DIR}/std/special/compiler_rt.zig" DESTINATION "${ZIG_STD_DEST}/special")
install(FILES "${CMAKE_SOURCE_DIR}/std/special/test_runner.zig" DESTINATION "${ZIG_STD_DEST}/special")
install(FILES "${CMAKE_SOURCE_DIR}/std/special/zigrt.zig" DESTINATION "${ZIG_STD_DEST}/special")

if (ZIG_TEST_COVERAGE)
    add_custom_target(coverage
        DEPENDS run_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMAND lcov --directory . --zerocounters --rc lcov_branch_coverage=1
        COMMAND ./zig build --build-file ../build.zig test
        COMMAND lcov --directory . --capture --output-file coverage.info --rc lcov_branch_coverage=1
        COMMAND lcov --remove coverage.info '/usr/*' --output-file coverage.info.cleaned --rc lcov_branch_coverage=1
        COMMAND genhtml -o coverage coverage.info.cleaned --rc lcov_branch_coverage=1
        COMMAND rm coverage.info coverage.info.cleaned
    )
endif()
