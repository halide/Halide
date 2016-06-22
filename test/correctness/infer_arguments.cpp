#include <stdio.h>
#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

template<typename T>
bool constant_expr_equals(Expr expr, T expected) {
    return (expr.type() == type_of<T>() &&
            is_one(simplify(expr == Expr(expected))));
}

int main(int argc, char **argv) {

#define EXPECT(expect, actual) \
    if (expect != actual) { std::cout << "Failure, expected " << #expect << " for " << #actual << ", got " << actual << " instead.\n"; return -1; }

    {
        ImageParam input1(UInt(8), 3, "input1");
        ImageParam input2(UInt(8), 2, "input2");
        Param<int32_t> height("height");
        Param<int32_t> width("width");
        Param<uint8_t> thresh("thresh");
        Param<float> frac("frac", 22.5f, 11.25f, 1e30f);
        // Named so that it will come last.
        const uint64_t kU64 = 0xf00dcafedeadbeef;
        Param<uint64_t> z_unsigned("z_unsigned", 0xdeadbeef, 0x01, Expr(kU64));

        Var x("x"), y("y"), c("c");

        Func f("f");
        f(x, y, c) = frac * (input1(clamp(x, 0, height), clamp(y, 0, width), c) +
                             min(thresh, input2(x, y))) + (0 * z_unsigned);

        std::vector<Argument> args = f.infer_arguments();
        EXPECT(7, args.size());

        Argument input1_arg = args[0];
        Argument input2_arg = args[1];
        Argument frac_arg = args[2];
        Argument height_arg = args[3];
        Argument thresh_arg = args[4];
        Argument width_arg = args[5];
        Argument z_unsigned_arg = args[6];

        EXPECT("input1", input1_arg.name);
        EXPECT("input2", input2_arg.name);
        EXPECT("frac", frac_arg.name);
        EXPECT("height", height_arg.name);
        EXPECT("thresh", thresh_arg.name);
        EXPECT("width", width_arg.name);
        EXPECT("z_unsigned", z_unsigned_arg.name);

        EXPECT(true, input1_arg.is_buffer());
        EXPECT(true, input2_arg.is_buffer());
        EXPECT(false, frac_arg.is_buffer());
        EXPECT(false, height_arg.is_buffer());
        EXPECT(false, thresh_arg.is_buffer());
        EXPECT(false, width_arg.is_buffer());
        EXPECT(false, z_unsigned_arg.is_buffer());

        // All Scalar Arguments have a defined default when coming from
        // infer_arguments.
        EXPECT(false, input1_arg.def.defined());
        EXPECT(false, input2_arg.def.defined());
        EXPECT(true, frac_arg.def.defined());
        EXPECT(true, constant_expr_equals<float>(frac_arg.def, 22.5f));
        EXPECT(true, height_arg.def.defined());
        EXPECT(true, thresh_arg.def.defined());
        EXPECT(true, width_arg.def.defined());
        EXPECT(true, z_unsigned_arg.def.defined());
        EXPECT(true, constant_expr_equals<uint64_t>(z_unsigned_arg.def, 0xdeadbeef));

        EXPECT(false, input1_arg.min.defined());
        EXPECT(false, input2_arg.min.defined());
        EXPECT(true, frac_arg.min.defined());
        EXPECT(true, constant_expr_equals<float>(frac_arg.min, 11.25f));
        EXPECT(false, height_arg.min.defined());
        EXPECT(false, thresh_arg.min.defined());
        EXPECT(false, width_arg.min.defined());
        EXPECT(true, z_unsigned_arg.min.defined());
        EXPECT(true, constant_expr_equals<uint64_t>(z_unsigned_arg.min, 0x1));

        EXPECT(false, input1_arg.max.defined());
        EXPECT(false, input2_arg.max.defined());
        EXPECT(true, frac_arg.max.defined());
        EXPECT(true, constant_expr_equals<float>(frac_arg.max, 1e30f));
        EXPECT(false, height_arg.max.defined());
        EXPECT(false, thresh_arg.max.defined());
        EXPECT(false, width_arg.max.defined());
        EXPECT(true, z_unsigned_arg.max.defined());
        EXPECT(true, constant_expr_equals<uint64_t>(z_unsigned_arg.max, 0xf00dcafedeadbeef));

        EXPECT(3, input1_arg.dimensions);
        EXPECT(2, input2_arg.dimensions);
        EXPECT(0, frac_arg.dimensions);
        EXPECT(0, height_arg.dimensions);
        EXPECT(0, thresh_arg.dimensions);
        EXPECT(0, width_arg.dimensions);
        EXPECT(0, z_unsigned_arg.dimensions);

        EXPECT(Type::Float, frac_arg.type.code());
        EXPECT(Type::Int, height_arg.type.code());
        EXPECT(Type::UInt, thresh_arg.type.code());
        EXPECT(Type::Int, width_arg.type.code());
        EXPECT(Type::UInt, z_unsigned_arg.type.code());

        EXPECT(32, frac_arg.type.bits());
        EXPECT(32, height_arg.type.bits());
        EXPECT(8, thresh_arg.type.bits());
        EXPECT(32, width_arg.type.bits());
        EXPECT(64, z_unsigned_arg.type.bits());

        Func f_a("f_a"), f_b("f_b");
        f_a(x, y, c) = input1(x, y, c) * frac;
        f_b(x, y, c) = input1(x, y, c) + thresh;
        Func f_tuple("f_tuple");
        f_tuple(x, y, c) = Tuple(f_a(x, y, c), f_b(x, y, c));

        args = f_tuple.infer_arguments();
        EXPECT(3, args.size());

        input1_arg = args[0];
        frac_arg = args[1];
        thresh_arg = args[2];

        EXPECT("input1", input1_arg.name);
        EXPECT("frac", frac_arg.name);
        EXPECT("thresh", thresh_arg.name);

        EXPECT(true, input1_arg.is_buffer());
        EXPECT(false, frac_arg.is_buffer());
        EXPECT(false, thresh_arg.is_buffer());

        EXPECT(3, input1_arg.dimensions);
        EXPECT(0, frac_arg.dimensions);
        EXPECT(0, thresh_arg.dimensions);

        EXPECT(Type::Float, frac_arg.type.code());
        EXPECT(Type::UInt, thresh_arg.type.code());

        EXPECT(32, frac_arg.type.bits());
        EXPECT(8, thresh_arg.type.bits());
    }


    printf("Success!\n");
    return 0;

}
