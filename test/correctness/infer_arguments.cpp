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
    if (expect != actual) { printf("Failure, expected %s for %s\n", #expect, #actual); return -1; }

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

        EXPECT("input1", args[0].name);
        EXPECT("input2", args[1].name);
        EXPECT("frac", args[2].name);
        EXPECT("height", args[3].name);
        EXPECT("thresh", args[4].name);
        EXPECT("width", args[5].name);
        EXPECT("z_unsigned", args[6].name);

        EXPECT(true, args[0].is_buffer());
        EXPECT(true, args[1].is_buffer());
        EXPECT(false, args[2].is_buffer());
        EXPECT(false, args[3].is_buffer());
        EXPECT(false, args[4].is_buffer());
        EXPECT(false, args[5].is_buffer());
        EXPECT(false, args[6].is_buffer());

        // All Scalar Arguments have a defined default when coming from
        // infer_arguments.
        EXPECT(false, args[0].def.defined());
        EXPECT(false, args[1].def.defined());
        EXPECT(true, args[2].def.defined());
        EXPECT(true, constant_expr_equals<float>(args[2].def, 22.5f));
        EXPECT(true, args[3].def.defined());
        EXPECT(true, args[4].def.defined());
        EXPECT(true, args[5].def.defined());
        EXPECT(true, args[6].def.defined());
        EXPECT(true, constant_expr_equals<uint64_t>(args[6].def, 0xdeadbeef));

        EXPECT(false, args[0].min.defined());
        EXPECT(false, args[1].min.defined());
        EXPECT(true, args[2].min.defined());
        EXPECT(true, constant_expr_equals<float>(args[2].min, 11.25f));
        EXPECT(false, args[3].min.defined());
        EXPECT(false, args[4].min.defined());
        EXPECT(false, args[5].min.defined());
        EXPECT(true, args[6].min.defined());
        EXPECT(true, constant_expr_equals<uint64_t>(args[6].min, 0x1));

        EXPECT(false, args[0].max.defined());
        EXPECT(false, args[1].max.defined());
        EXPECT(true, args[2].max.defined());
        EXPECT(true, constant_expr_equals<float>(args[2].max, 1e30f));
        EXPECT(false, args[3].max.defined());
        EXPECT(false, args[4].max.defined());
        EXPECT(false, args[5].max.defined());
        EXPECT(true, args[6].max.defined());
        EXPECT(true, constant_expr_equals<uint64_t>(args[6].max, 0xf00dcafedeadbeef));

        EXPECT(3, args[0].dimensions);
        EXPECT(2, args[1].dimensions);
        EXPECT(0, args[2].dimensions);
        EXPECT(0, args[3].dimensions);
        EXPECT(0, args[4].dimensions);
        EXPECT(0, args[5].dimensions);
        EXPECT(0, args[6].dimensions);

        EXPECT(Type::Float, args[2].type.code());
        EXPECT(Type::Int, args[3].type.code());
        EXPECT(Type::UInt, args[4].type.code());
        EXPECT(Type::Int, args[5].type.code());
        EXPECT(Type::UInt, args[6].type.code());

        EXPECT(32, args[2].type.bits());
        EXPECT(32, args[3].type.bits());
        EXPECT(8, args[4].type.bits());
        EXPECT(32, args[5].type.bits());
        EXPECT(64, args[6].type.bits());

        Func f_a("f_a"), f_b("f_b");
        f_a(x, y, c) = input1(x, y, c) * frac;
        f_b(x, y, c) = input1(x, y, c) + thresh;
        Func f_tuple("f_tuple");
        f_tuple(x, y, c) = Tuple(f_a(x, y, c), f_b(x, y, c));

        args = f_tuple.infer_arguments();
        EXPECT(3, args.size());

        EXPECT("input1", args[0].name);
        EXPECT("frac", args[1].name);
        EXPECT("thresh", args[2].name);

        EXPECT(true, args[0].is_buffer());
        EXPECT(false, args[1].is_buffer());
        EXPECT(false, args[2].is_buffer());

        EXPECT(3, args[0].dimensions);
        EXPECT(0, args[1].dimensions);
        EXPECT(0, args[2].dimensions);

        EXPECT(Type::Float, args[1].type.code());
        EXPECT(Type::UInt, args[2].type.code());

        EXPECT(32, args[1].type.bits());
        EXPECT(8, args[2].type.bits());
    }


    printf("Success!\n");
    return 0;

}
