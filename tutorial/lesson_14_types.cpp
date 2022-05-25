// Halide tutorial lesson 14: The Halide type system

// This lesson more precisely describes Halide's type system.

// On linux, you can compile and run it like so:
// g++ lesson_14*.cpp -g -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -lpthread -ldl -o lesson_14 -std=c++17
// LD_LIBRARY_PATH=<path/to/libHalide.so> ./lesson_14

// On os x:
// g++ lesson_14*.cpp -g -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -o lesson_14 -std=c++17
// DYLD_LIBRARY_PATH=<path/to/libHalide.dylib> ./lesson_14

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_14_types
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>
using namespace Halide;

// This function is used to demonstrate generic code at the end of
// this lesson.
Expr average(Expr a, Expr b);

int main(int argc, char **argv) {

    // All Exprs have a scalar type, and all Funcs evaluate to one or
    // more scalar types. The scalar types in Halide are unsigned
    // integers of various bit widths, signed integers of the same set
    // of bit widths, floating point numbers in single and double
    // precision, and opaque handles (equivalent to void *). The
    // following array contains all the legal types.

    Type valid_halide_types[] = {
        UInt(8), UInt(16), UInt(32), UInt(64),
        Int(8), Int(16), Int(32), Int(64),
        Float(32), Float(64), Handle()};

    // Constructing and inspecting types.
    {
        // You can programmatically examine the properties of a Halide
        // type. This is useful when you write a C++ function that has
        // Expr arguments and you wish to check their types:
        assert(UInt(8).bits() == 8);
        assert(Int(8).is_int());

        // You can also programmatically construct Types as a function of other Types.
        Type t = UInt(8);
        t = t.with_bits(t.bits() * 2);
        assert(t == UInt(16));

        // Or construct a Type from a C++ scalar type
        assert(type_of<float>() == Float(32));

        // The Type struct is also capable of representing vector types,
        // but this is reserved for Halide's internal use. You should
        // vectorize code by using Func::vectorize, not by attempting to
        // construct vector expressions directly. You may encounter vector
        // types if you programmatically manipulate lowered Halide code,
        // but this is an advanced topic (see Func::add_custom_lowering_pass).

        // You can query any Halide Expr for its type. An Expr
        // representing a Var has type Int(32):
        Var x;
        assert(Expr(x).type() == Int(32));

        // Most transcendental functions in Halide cast their inputs to a
        // Float(32) and return a Float(32):
        assert(sin(x).type() == Float(32));

        // You can cast an Expr from one Type to another using the cast operator:
        assert(cast(UInt(8), x).type() == UInt(8));

        // This also comes in a template form that takes a C++ type.
        assert(cast<uint8_t>(x).type() == UInt(8));

        // You can also query any defined Func for the types it produces.
        Func f1;
        f1(x) = cast<uint8_t>(x);
        assert(f1.types()[0] == UInt(8));

        Func f2;
        f2(x) = {x, sin(x)};
        assert(f2.types()[0] == Int(32) &&
               f2.types()[1] == Float(32));
    }

    // Type promotion rules.
    {
        // When you combine Exprs of different types (e.g. using '+',
        // '*', etc), Halide uses a system of type promotion
        // rules. These differ to C's rules. To demonstrate these
        // we'll make some Exprs of each type.
        Var x;
        Expr u8 = cast<uint8_t>(x);
        Expr u16 = cast<uint16_t>(x);
        Expr u32 = cast<uint32_t>(x);
        Expr u64 = cast<uint64_t>(x);
        Expr s8 = cast<int8_t>(x);
        Expr s16 = cast<int16_t>(x);
        Expr s32 = cast<int32_t>(x);
        Expr s64 = cast<int64_t>(x);
        Expr f32 = cast<float>(x);
        Expr f64 = cast<double>(x);

        // The rules are as follows, and are applied in the order they are
        // written below.

        // 1) It is an error to cast or use arithmetic operators on Exprs of type Handle().

        // 2) If the types are the same, then no type conversions occur.
        for (Type t : valid_halide_types) {
            // Skip the handle type.
            if (t.is_handle()) continue;
            Expr e = cast(t, x);
            assert((e + e).type() == e.type());
        }

        // 3) If one type is a float but the other is not, then the
        // non-float argument is promoted to a float (possibly causing a
        // loss of precision for large integers).
        assert((u8 + f32).type() == Float(32));
        assert((f32 + s64).type() == Float(32));
        assert((u16 + f64).type() == Float(64));
        assert((f64 + s32).type() == Float(64));

        // 4) If both types are float, then the narrower argument is
        // promoted to the wider bit-width.
        assert((f64 + f32).type() == Float(64));

        // The rules above handle all the floating-point cases. The
        // following three rules handle the integer cases.

        // 5) If one of the arguments is an C++ int, and the other is
        // a Halide::Expr, then the int is coerced to the type of the
        // expression.
        assert((u32 + 3).type() == UInt(32));
        assert((3 + s16).type() == Int(16));

        // If this rule would cause the integer to overflow, then Halide
        // will trigger an error, e.g. uncommenting the following line
        // will cause this program to terminate with an error.
        // Expr bad = u8 + 257;

        // 6) If both types are unsigned integers, or both types are
        // signed integers, then the narrower argument is promoted to
        // wider type.
        assert((u32 + u8).type() == UInt(32));
        assert((s16 + s64).type() == Int(64));

        // 7) If one type is signed and the other is unsigned, both
        // arguments are promoted to a signed integer with the greater of
        // the two bit widths.
        assert((u8 + s32).type() == Int(32));
        assert((u32 + s8).type() == Int(32));

        // Note that this may silently overflow the unsigned type in the
        // case where the bit widths are the same.
        assert((u32 + s32).type() == Int(32));

        // When an unsigned Expr is converted to a wider signed type in
        // this way, it is first widened to a wider unsigned type
        // (zero-extended), and then reinterpreted as a signed
        // integer. I.e. casting the UInt(8) value 255 to an Int(32)
        // produces 255, not -1.
        int32_t result32 = evaluate<int>(cast<int32_t>(cast<uint8_t>(255)));
        assert(result32 == 255);

        // When a signed type is explicitly converted to a wider unsigned
        // type with the cast operator (the type promotion rules will
        // never do this automatically), it is first converted to the
        // wider signed type (sign-extended), and then reinterpreted as
        // an unsigned integer. I.e. casting the Int(8) value -1 to a
        // UInt(16) produces 65535, not 255.
        uint16_t result16 = evaluate<uint16_t>(cast<uint16_t>(cast<int8_t>(-1)));
        assert(result16 == 65535);
    }

    // The type Handle().
    {
        // Handle is used to represent opaque pointers. Applying
        // type_of to any pointer type will return Handle()
        assert(type_of<void *>() == Handle());
        assert(type_of<const char *const **>() == Handle());

        // Handles are always stored as 64-bit, regardless of the compilation
        // target.
        assert(Handle().bits() == 64);

        // The main use of an Expr of type Handle is to pass
        // it through Halide to other external code.
    }

    // Generic code.
    {
        // The main explicit use of Type in Halide is to write Halide
        // code parameterized by a Type. In C++ you'd do this with
        // templates. In Halide there's no need - you can inspect and
        // modify the types dynamically at C++ runtime instead. The
        // function defined below averages two expressions of any
        // equal numeric type.
        Var x;
        assert(average(cast<float>(x), 3.0f).type() == Float(32));
        assert(average(x, 3).type() == Int(32));
        assert(average(cast<uint8_t>(x), cast<uint8_t>(3)).type() == UInt(8));
    }

    printf("Success!\n");

    return 0;
}

Expr average(Expr a, Expr b) {
    // Types must match.
    assert(a.type() == b.type());

    // For floating point types:
    if (a.type().is_float()) {
        // The '2' will be promoted to the floating point type due to
        // rule 3 above.
        return (a + b) / 2;
    }

    // For integer types, we must compute the intermediate value in a
    // wider type to avoid overflow.
    Type narrow = a.type();
    Type wider = narrow.with_bits(narrow.bits() * 2);
    a = cast(wider, a);
    b = cast(wider, b);
    return cast(narrow, (a + b) / 2);
}
