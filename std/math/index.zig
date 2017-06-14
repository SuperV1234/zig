const assert = @import("../debug.zig").assert;
const builtin = @import("builtin");

pub const frexp = @import("frexp.zig").frexp;

pub const Cmp = enum {
    Less,
    Equal,
    Greater,
};

pub fn min(x: var, y: var) -> @typeOf(x + y) {
    if (x < y) x else y
}

test "math.min" {
    assert(min(i32(-1), i32(2)) == -1);
}

pub fn max(x: var, y: var) -> @typeOf(x + y) {
    if (x > y) x else y
}

test "math.max" {
    assert(max(i32(-1), i32(2)) == 2);
}

error Overflow;
pub fn mul(comptime T: type, a: T, b: T) -> %T {
    var answer: T = undefined;
    if (@mulWithOverflow(T, a, b, &answer)) error.Overflow else answer
}

error Overflow;
pub fn add(comptime T: type, a: T, b: T) -> %T {
    var answer: T = undefined;
    if (@addWithOverflow(T, a, b, &answer)) error.Overflow else answer
}

error Overflow;
pub fn sub(comptime T: type, a: T, b: T) -> %T {
    var answer: T = undefined;
    if (@subWithOverflow(T, a, b, &answer)) error.Overflow else answer
}

pub fn negate(x: var) -> %@typeOf(x) {
    return sub(@typeOf(x), 0, x);
}

error Overflow;
pub fn shl(comptime T: type, a: T, b: T) -> %T {
    var answer: T = undefined;
    if (@shlWithOverflow(T, a, b, &answer)) error.Overflow else answer
}

test "math overflow functions" {
    testOverflow();
    comptime testOverflow();
}

fn testOverflow() {
    assert(%%mul(i32, 3, 4) == 12);
    assert(%%add(i32, 3, 4) == 7);
    assert(%%sub(i32, 3, 4) == -1);
    assert(%%shl(i32, 0b11, 4) == 0b110000);
}


pub fn log(comptime base: usize, value: var) -> @typeOf(value) {
    const T = @typeOf(value);
    switch (@typeId(T)) {
        builtin.TypeId.Int => {
            if (base == 2) {
                return T.bit_count - 1 - @clz(value);
            } else {
                @compileError("TODO implement log for non base 2 integers");
            }
        },
        builtin.TypeId.Float => {
            @compileError("TODO implement log for floats");
        },
        else => {
            @compileError("log expects integer or float, found '" ++ @typeName(T) ++ "'");
        },
    }
}

error Overflow;
pub fn absInt(x: var) -> %@typeOf(x) {
    const T = @typeOf(x);
    comptime assert(@typeId(T) == builtin.TypeId.Int); // must pass an integer to absInt
    comptime assert(T.is_signed); // must pass a signed integer to absInt
    if (x == @minValue(@typeOf(x)))
        return error.Overflow;
    {
        @setDebugSafety(this, false);
        return if (x < 0) -x else x;
    }
}

test "math.absInt" {
    testAbsInt();
    comptime testAbsInt();
}
fn testAbsInt() {
    assert(%%absInt(i32(-10)) == 10);
    assert(%%absInt(i32(10)) == 10);
}

pub const absFloat = @import("fabs.zig").fabs;

error DivisionByZero;
error Overflow;
pub fn divTrunc(comptime T: type, numerator: T, denominator: T) -> %T {
    @setDebugSafety(this, false);
    if (denominator == 0)
        return error.DivisionByZero;
    if (@typeId(T) == builtin.TypeId.Int and T.is_signed and numerator == @minValue(T) and denominator == -1)
        return error.Overflow;
    return @divTrunc(numerator, denominator);
}

test "math.divTrunc" {
    testDivTrunc();
    comptime testDivTrunc();
}
fn testDivTrunc() {
    assert(%%divTrunc(i32, 5, 3) == 1);
    assert(%%divTrunc(i32, -5, 3) == -1);
    if (divTrunc(i8, -5, 0)) |_| unreachable else |err| assert(err == error.DivisionByZero);
    if (divTrunc(i8, -128, -1)) |_| unreachable else |err| assert(err == error.Overflow);

    assert(%%divTrunc(f32, 5.0, 3.0) == 1.0);
    assert(%%divTrunc(f32, -5.0, 3.0) == -1.0);
}

error DivisionByZero;
error Overflow;
pub fn divFloor(comptime T: type, numerator: T, denominator: T) -> %T {
    @setDebugSafety(this, false);
    if (denominator == 0)
        return error.DivisionByZero;
    if (@typeId(T) == builtin.TypeId.Int and T.is_signed and numerator == @minValue(T) and denominator == -1)
        return error.Overflow;
    return @divFloor(numerator, denominator);
}

test "math.divFloor" {
    testDivFloor();
    comptime testDivFloor();
}
fn testDivFloor() {
    assert(%%divFloor(i32, 5, 3) == 1);
    assert(%%divFloor(i32, -5, 3) == -2);
    if (divFloor(i8, -5, 0)) |_| unreachable else |err| assert(err == error.DivisionByZero);
    if (divFloor(i8, -128, -1)) |_| unreachable else |err| assert(err == error.Overflow);

    assert(%%divFloor(f32, 5.0, 3.0) == 1.0);
    assert(%%divFloor(f32, -5.0, 3.0) == -2.0);
}

error DivisionByZero;
error Overflow;
error UnexpectedRemainder;
pub fn divExact(comptime T: type, numerator: T, denominator: T) -> %T {
    @setDebugSafety(this, false);
    if (denominator == 0)
        return error.DivisionByZero;
    if (@typeId(T) == builtin.TypeId.Int and T.is_signed and numerator == @minValue(T) and denominator == -1)
        return error.Overflow;
    const result = @divTrunc(numerator, denominator);
    if (result * denominator != numerator)
        return error.UnexpectedRemainder;
    return result;
}

test "math.divExact" {
    testDivExact();
    comptime testDivExact();
}
fn testDivExact() {
    assert(%%divExact(i32, 10, 5) == 2);
    assert(%%divExact(i32, -10, 5) == -2);
    if (divExact(i8, -5, 0)) |_| unreachable else |err| assert(err == error.DivisionByZero);
    if (divExact(i8, -128, -1)) |_| unreachable else |err| assert(err == error.Overflow);
    if (divExact(i32, 5, 2)) |_| unreachable else |err| assert(err == error.UnexpectedRemainder);

    assert(%%divExact(f32, 10.0, 5.0) == 2.0);
    assert(%%divExact(f32, -10.0, 5.0) == -2.0);
    if (divExact(f32, 5.0, 2.0)) |_| unreachable else |err| assert(err == error.UnexpectedRemainder);
}

error DivisionByZero;
error NegativeDenominator;
pub fn mod(comptime T: type, numerator: T, denominator: T) -> %T {
    @setDebugSafety(this, false);
    if (denominator == 0)
        return error.DivisionByZero;
    if (denominator < 0)
        return error.NegativeDenominator;
    return @mod(numerator, denominator);
}

test "math.mod" {
    testMod();
    comptime testMod();
}
fn testMod() {
    assert(%%mod(i32, -5, 3) == 1);
    assert(%%mod(i32, 5, 3) == 2);
    if (mod(i32, 10, -1)) |_| unreachable else |err| assert(err == error.NegativeDenominator);
    if (mod(i32, 10, 0)) |_| unreachable else |err| assert(err == error.DivisionByZero);

    assert(%%mod(f32, -5, 3) == 1);
    assert(%%mod(f32, 5, 3) == 2);
    if (mod(f32, 10, -1)) |_| unreachable else |err| assert(err == error.NegativeDenominator);
    if (mod(f32, 10, 0)) |_| unreachable else |err| assert(err == error.DivisionByZero);
}

error DivisionByZero;
error NegativeDenominator;
pub fn rem(comptime T: type, numerator: T, denominator: T) -> %T {
    @setDebugSafety(this, false);
    if (denominator == 0)
        return error.DivisionByZero;
    if (denominator < 0)
        return error.NegativeDenominator;
    return @rem(numerator, denominator);
}

test "math.rem" {
    testRem();
    comptime testRem();
}
fn testRem() {
    assert(%%rem(i32, -5, 3) == -2);
    assert(%%rem(i32, 5, 3) == 2);
    if (rem(i32, 10, -1)) |_| unreachable else |err| assert(err == error.NegativeDenominator);
    if (rem(i32, 10, 0)) |_| unreachable else |err| assert(err == error.DivisionByZero);

    assert(%%rem(f32, -5, 3) == -2);
    assert(%%rem(f32, 5, 3) == 2);
    if (rem(f32, 10, -1)) |_| unreachable else |err| assert(err == error.NegativeDenominator);
    if (rem(f32, 10, 0)) |_| unreachable else |err| assert(err == error.DivisionByZero);
}

fn isNan(comptime T: type, x: T) -> bool {
    assert(@typeId(T) == builtin.TypeId.Float);
    if (T == f32) {
        const bits = @bitCast(u32, x);
        return (bits & 0x7fffffff) > 0x7f800000;
    } else if (T == f64) {
        const bits = @bitCast(u64, x);
        return (bits & (@maxValue(u64) >> 1)) > (u64(0x7ff) << 52);
    } else if (T == c_longdouble) {
        @compileError("TODO support isNan for c_longdouble");
    } else {
        unreachable;
    }
}

pub fn floor(x: var) -> @typeOf(x) {
    switch (@typeOf(x)) {
        f32 => floor_f32(x),
        f64 => floor_f64(x),
        c_longdouble => @compileError("TODO support floor for c_longdouble"),
        else => @compileError("Invalid type for floor: " ++ @typeName(@typeOf(x))),
    }
}

fn floor_f32(x: f32) -> f32 {
    var i = @bitCast(u32, x);
    const e = i32((i >> 23) & 0xff) -% 0x7f;
    if (e >= 23)
        return x;
    if (e >= 0) {
        const m = @bitCast(u32, 0x007fffff >> e);
        if ((i & m) == 0)
            return x;
        if (i >> 31 != 0)
            i +%= m;
        i &= ~m;
    } else {
        if (i >> 31 == 0)
            return 0;
        if (i <<% 1 != 0)
            return -1.0;
    }
    return @bitCast(f32, i);
}

fn floor_f64(x: f64) -> f64 {
    const DBL_EPSILON = 2.22044604925031308085e-16;
    const toint = 1.0 / DBL_EPSILON;

    var i = @bitCast(u64, x);
    const e = (i >> 52) & 0x7ff;

    if (e >= 0x3ff +% 52 or x == 0)
        return x;
    // y = int(x) - x, where int(x) is an integer neighbor of x
    const y = {
        @setFloatMode(this, builtin.FloatMode.Strict);
        if (i >> 63 != 0) {
            x - toint + toint - x
        } else {
            x + toint - toint - x
        }
    };
    // special case because of non-nearest rounding modes
    if (e <= 0x3ff - 1) {
        if (i >> 63 != 0)
            return -1.0;
        return 0.0;
    }
    if (y > 0)
        return x + y - 1;
    return x + y;
}

test "math.floor" {
    assert(floor(f32(1.234)) == 1.0);
    assert(floor(f32(-1.234)) == -2.0);
    assert(floor(f32(999.0)) == 999.0);
    assert(floor(f32(-999.0)) == -999.0);

    assert(floor(f64(1.234)) == 1.0);
    assert(floor(f64(-1.234)) == -2.0);
    assert(floor(f64(999.0)) == 999.0);
    assert(floor(f64(-999.0)) == -999.0);
}

/// Returns the absolute value of the integer parameter.
/// Result is an unsigned integer.
pub fn absCast(x: var) -> @IntType(false, @typeOf(x).bit_count) {
    const uint = @IntType(false, @typeOf(x).bit_count);
    if (x >= 0)
        return uint(x);

    return uint(-(x + 1)) + 1;
}

test "math.absCast" {
    assert(absCast(i32(-999)) == 999);
    assert(@typeOf(absCast(i32(-999))) == u32);

    assert(absCast(i32(999)) == 999);
    assert(@typeOf(absCast(i32(999))) == u32);

    assert(absCast(i32(@minValue(i32))) == -@minValue(i32));
    assert(@typeOf(absCast(i32(@minValue(i32)))) == u32);
}

/// Returns the negation of the integer parameter.
/// Result is a signed integer.
error Overflow;
pub fn negateCast(x: var) -> %@IntType(true, @typeOf(x).bit_count) {
    if (@typeOf(x).is_signed)
        return negate(x);

    const int = @IntType(true, @typeOf(x).bit_count);
    if (x > -@minValue(int))
        return error.Overflow;

    if (x == -@minValue(int))
        return @minValue(int);

    return -int(x);
}

test "math.negateCast" {
    assert(%%negateCast(u32(999)) == -999);
    assert(@typeOf(%%negateCast(u32(999))) == i32);

    assert(%%negateCast(u32(-@minValue(i32))) == @minValue(i32));
    assert(@typeOf(%%negateCast(u32(-@minValue(i32)))) == i32);

    if (negateCast(u32(@maxValue(i32) + 10))) |_| unreachable else |err| assert(err == error.Overflow);
}

test "math" {
    _ = @import("frexp.zig");
}


pub fn approxEq(comptime T: type, x: T, y: T, epsilon: T) -> bool {
    comptime assert(@typeId(T) == builtin.TypeId.Float);
    absFloat(x - y) < epsilon
}
