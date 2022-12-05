#!/usr/bin/python3

# Halide tutorial lesson 14: The Halide type system

# This lesson more precisely describes Halide's type system.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_14_types
# in a shell with the current directory at python_bindings/


import halide as hl


def main():

    # All Exprs have a scalar type, and all Funcs evaluate to one or
    # more scalar types. The scalar types in Halide are unsigned
    # integers of various bit widths, signed integers of the same set
    # of bit widths, floating point numbers in single and double
    # precision, and opaque handles (equivalent to void *). The
    # following array contains all the legal types.

    valid_halide_types = [
        hl.UInt(8), hl.UInt(16), hl.UInt(32), hl.UInt(64),
        hl.Int(8), hl.Int(16), hl.Int(32), hl.Int(64),
        hl.Float(32), hl.Float(64), hl.Handle()]

    # Constructing and inspecting types.
    if True:
        # You can programmatically examine the properties of a Halide
        # type. This is useful when you write a C++ function that has
        # hl.Expr arguments and you wish to check their types:
        assert hl.UInt(8).bits() == 8
        assert hl.Int(8).is_int()

        # You can also programmatically construct Types as a function of other
        # Types.
        t = hl.UInt(8)
        t = t.with_bits(t.bits() * 2)
        assert t == hl.UInt(16)

        # The Type struct is also capable of representing vector types,
        # but this is reserved for Halide's internal use. You should
        # vectorize code by using hl.Func::vectorize, not by attempting to
        # construct vector expressions directly. You may encounter vector
        # types if you programmatically manipulate lowered Halide code,
        # but this is an advanced topic (see
        # hl.Func::add_custom_lowering_pass).

        # You can query any Halide hl.Expr for its type. An hl.Expr
        # representing a hl.Var has type hl.Int(32):
        x = hl.Var("x")
        assert hl.Expr(x).type() == hl.Int(32)

        # Most transcendental functions in Halide hl.cast their inputs to a
        # hl.Float(32) and return a hl.Float(32):
        assert hl.sin(x).type() == hl.Float(32)

        # You can hl.cast an hl.Expr from one Type to another using the hl.cast
        # operator:
        assert hl.cast(hl.UInt(8), x).type() == hl.UInt(8)

        # You can also query any defined hl.Func for the types it produces.
        f1 = hl.Func("f1")
        f1[x] = hl.cast(hl.UInt(8), x)
        assert f1.types()[0] == hl.UInt(8)

        f2 = hl.Func("f2")
        f2[x] = (x, hl.sin(x))
        assert f2.types()[0] == hl.Int(32) and f2.types()[1] == hl.Float(32)

    # Type promotion rules.
    if True:
        # When you combine Exprs of different types (e.g. using '+',
        # '*', etc), Halide uses a system of type promotion
        # rules. These differ to C's rules. To demonstrate these
        # we'll make some Exprs of each type.
        x = hl.Var("x")
        u8 = hl.cast(hl.UInt(8), x)
        u16 = hl.cast(hl.UInt(16), x)
        u32 = hl.cast(hl.UInt(32), x)
        u64 = hl.cast(hl.UInt(64), x)
        s8 = hl.cast(hl.Int(8), x)
        s16 = hl.cast(hl.Int(16), x)
        s32 = hl.cast(hl.Int(32), x)
        s64 = hl.cast(hl.Int(64), x)
        f32 = hl.cast(hl.Float(32), x)
        f64 = hl.cast(hl.Float(64), x)

        # The rules are as follows, and are applied in the order they are
        # written below.

        # 1) It is an error to hl.cast or use arithmetic operators on Exprs of
        # type hl.Handle().

        # 2) If the types are the same, then no type conversions occur.
        for t in valid_halide_types:
            # Skip the handle type.
            if t.is_handle():
                continue
            e = hl.cast(t, x)
            assert (e + e).type() == e.type()

        # 3) If one type is a float but the other is not, then the
        # non-float argument is promoted to a float (possibly causing a
        # loss of precision for large integers).
        assert (u8 + f32).type() == hl.Float(32)
        assert (f32 + s64).type() == hl.Float(32)
        assert (u16 + f64).type() == hl.Float(64)
        assert (f64 + s32).type() == hl.Float(64)

        # 4) If both types are float, then the narrower argument is
        # promoted to the wider bit-width.
        assert (f64 + f32).type() == hl.Float(64)

        # The rules above handle all the floating-point cases. The
        # following three rules handle the integer cases.

        # 5) If one of the expressions is an integer constant, then it is
        # coerced to the type of the other expression.
        assert (u32 + 3).type() == hl.UInt(32)
        assert (3 + s16).type() == hl.Int(16)

        # If this rule would cause the integer to overflow, then Halide
        # will trigger an error, e.g. uncommenting the following line
        # will cause this program to terminate with an error.
        # hl.Expr bad = u8 + 257

        # 6) If both types are unsigned integers, or both types are
        # signed integers, then the narrower argument is promoted to
        # wider type.
        assert (u32 + u8).type() == hl.UInt(32)
        assert (s16 + s64).type() == hl.Int(64)

        # 7) If one type is signed and the other is unsigned, both
        # arguments are promoted to a signed integer with the greater of
        # the two bit widths.
        assert (u8 + s32).type() == hl.Int(32)
        assert (u32 + s8).type() == hl.Int(32)

        # Note that this may silently overflow the unsigned type in the
        # case where the bit widths are the same.
        assert (u32 + s32).type() == hl.Int(32)

        if False:  # evaluate<X> not yet exposed to python
            # When an unsigned hl.Expr is converted to a wider signed type in
            # this way, it is first widened to a wider unsigned type
            # (zero-extended), and then reinterpreted as a signed
            # integer. I.e. casting the hl.UInt(8) value 255 to an hl.Int(32)
            # produces 255, not -1.
            # int32_t result32 =
            # evaluate<int>(hl.cast<int32_t>(hl.cast<uint8_t>(255)))
            assert result32 == 255

            # When a signed type is explicitly converted to a wider unsigned
            # type with the hl.cast operator (the type promotion rules will
            # never do this automatically), it is first converted to the
            # wider signed type (sign-extended), and then reinterpreted as
            # an unsigned integer. I.e. casting the hl.Int(8) value -1 to a
            # hl.UInt(16) produces 65535, not 255.
            # uint16_t result16 =
            # evaluate<uint16_t>(hl.cast<uint16_t>(hl.cast<int8_t>(-1)))
            assert result16 == 65535

    # The type hl.Handle().
    if True:
        # hl.Handle is used to represent opaque pointers. Applying
        # type_of to any pointer type will return hl.Handle()

        # Handles are always stored as 64-bit, regardless of the compilation
        # target.
        assert hl.Handle().bits() == 64

        # The main use of an hl.Expr of type hl.Handle is to pass
        # it through Halide to other external code.

    # Generic code.
    if True:
        # The main explicit use of Type in Halide is to write Halide
        # code parameterized by a Type. In C++ you'd do this with
        # templates. In Halide there's no need - you can inspect and
        # modify the types dynamically at C++ runtime instead. The
        # function defined below averages two expressions of any
        # equal numeric type.
        x = hl.Var("x")
        assert average(hl.cast(hl.Float(32), x), 3.0).type() == hl.Float(32)
        assert average(x, 3).type() == hl.Int(32)
        assert average(hl.cast(hl.UInt(8), x), hl.cast(hl.UInt(8), 3)).type() == hl.UInt(8)

    print("Success!")

    return 0


def average(a, b):

    if type(a) is not hl.Expr:
        a = hl.Expr(a)

    if type(b) is not hl.Expr:
        b = hl.Expr(b)

    "hl.Expr average(hl.Expr a, hl.Expr b)"
    # Types must match.
    assert a.type() == b.type()

    # For floating point types:
    if (a.type().is_float()):
        # The '2' will be promoted to the floating point type due to
        # rule 3 above.
        return (a + b) / 2

    # For integer types, we must compute the intermediate value in a
    # wider type to avoid overflow.
    narrow = a.type()
    wider = narrow.with_bits(narrow.bits() * 2)
    a = hl.cast(wider, a)
    b = hl.cast(wider, b)
    return hl.cast(narrow, (a + b) / 2)


if __name__ == "__main__":
    main()
