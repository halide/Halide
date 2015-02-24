#include <stdio.h>
#include <Halide.h>

using namespace Halide;

template<typename T>
T extract_int(int32_t i32) {
    return static_cast<T>(i32);
}

template<>
uint64_t extract_int(int32_t i32) {
    // for int32->uint64, assume we want the 32 bits as-is
    // with no sign extension.
    return static_cast<uint64_t>(i32) & 0xffffffff;
}

template<typename T>
T bitwise_or(T lhs, T rhs) {
    return lhs | rhs;
}

template<>
float bitwise_or(float lhs, float rhs) {
    assert(false);
    return 0.0f;
}

template<>
double bitwise_or(double lhs, double rhs) {
    assert(false);
    return 0.0;
}

template<typename T>
T shift_left(T lhs, T rhs) {
    return lhs << rhs;
}

template<>
float shift_left(float lhs, float rhs) {
    assert(false);
    return 0.0f;
}

template<>
double shift_left(double lhs, double rhs) {
    assert(false);
    return 0.0;
}

// Despite the name, this is NOT a generic eval-expr function (ha)...
// It has just barely enough logic to decode the Expr forms that
// we are likely to see from infer_arguments() and friends.
template<typename T>
bool eval_expr(Expr expr, T* value) {
    using Halide::Internal::Call;
    using Halide::Internal::Cast;
    using Halide::Internal::FloatImm;
    using Halide::Internal::IntImm;

    if (!expr.defined() || !expr.type().is_scalar()) {
        return false;
    }
    if (const IntImm* i = expr.as<IntImm>()) {
        *value = extract_int<T>(i->value);
        return true;
    }
    if (const FloatImm* f = expr.as<FloatImm>()) {
        *value = static_cast<T>(f->value);
        return true;
    }
    if (const Cast* c = expr.as<Cast>()) {
        return eval_expr(c->value, value);
    }
    if (const Call* call = expr.as<Call>()) {
        if (call->name == Call::bitwise_or) {
            T lhs, rhs;
            if (!eval_expr(call->args[0], &lhs) || !eval_expr(call->args[1], &rhs)) {
                return false;
            }
            *value = bitwise_or<T>(lhs, rhs);
            return true;
        }
        if (call->name == Call::shift_left) {
            T lhs, rhs;
            if (!eval_expr(call->args[0], &lhs) || !eval_expr(call->args[1], &rhs)) {
                return false;
            }
            *value = shift_left<T>(lhs, rhs);
            return true;
        }
    }
    return false;
}

template<typename T>
bool constant_expr_equals(Expr expr, T expected) {
    T actual;
    if (eval_expr(expr, &actual)) {
        return expected == actual;
    }
    return false;
}

Expr uint64_expr(uint64_t u) {
    using Halide::Internal::Cast;
    Expr lo = Expr((int32_t) u);
    Expr hi = Expr((int32_t) (u >> 32));
    return (Cast::make(UInt(64), hi) << Expr(32)) | Cast::make(UInt(64), lo);
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
        Param<uint64_t> z_unsigned("z_unsigned", 0xdeadbeef, 0x01, uint64_expr(0xf00dcafedeadbeef));

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

        EXPECT(Type::Float, args[2].type.code);
        EXPECT(Type::Int, args[3].type.code);
        EXPECT(Type::UInt, args[4].type.code);
        EXPECT(Type::Int, args[5].type.code);
        EXPECT(Type::UInt, args[6].type.code);

        EXPECT(32, args[2].type.bits);
        EXPECT(32, args[3].type.bits);
        EXPECT(8, args[4].type.bits);
        EXPECT(32, args[5].type.bits);
        EXPECT(64, args[6].type.bits);

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

        EXPECT(Type::Float, args[1].type.code);
        EXPECT(Type::UInt, args[2].type.code);

        EXPECT(32, args[1].type.bits);
        EXPECT(8, args[2].type.bits);
    }


    printf("Success!\n");
    return 0;

}
