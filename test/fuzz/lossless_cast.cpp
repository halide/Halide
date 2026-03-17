#include "fuzz_helpers.h"
#include "random_expr_generator.h"
#include <Halide.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

bool definitely_has_ub(Expr e) {
    e = simplify(e);

    class HasOverflow : public IRVisitor {
        void visit(const Call *op) override {
            if (op->is_intrinsic({Call::signed_integer_overflow})) {
                found = true;
            }
            IRVisitor::visit(op);
        }

    public:
        bool found = false;
    } has_overflow;
    e.accept(&has_overflow);

    return has_overflow.found;
}

bool might_have_ub(Expr e) {
    class MightOverflow : public IRVisitor {
        std::map<Expr, ConstantInterval, ExprCompare> cache;

        using IRVisitor::visit;

        bool no_overflow_int(const Type &t) {
            return t.is_int() && t.bits() >= 32;
        }

        ConstantInterval bounds(const Expr &e) {
            return constant_integer_bounds(e, Scope<ConstantInterval>::empty_scope(), &cache);
        }

        void visit(const Add *op) override {
            if (no_overflow_int(op->type) &&
                !op->type.can_represent(bounds(op->a) + bounds(op->b))) {
                found = true;
            } else {
                IRVisitor::visit(op);
            }
        }

        void visit(const Sub *op) override {
            if (no_overflow_int(op->type) &&
                !op->type.can_represent(bounds(op->a) - bounds(op->b))) {
                found = true;
            } else {
                IRVisitor::visit(op);
            }
        }

        void visit(const Mul *op) override {
            if (no_overflow_int(op->type) &&
                !op->type.can_represent(bounds(op->a) * bounds(op->b))) {
                found = true;
            } else {
                IRVisitor::visit(op);
            }
        }

        void visit(const Div *op) override {
            if (no_overflow_int(op->type) &&
                (bounds(op->a) / bounds(op->b)).contains(-1)) {
                found = true;
            } else {
                IRVisitor::visit(op);
            }
        }

        void visit(const Cast *op) override {
            if (no_overflow_int(op->type) &&
                !op->type.can_represent(bounds(op->value))) {
                found = true;
            } else {
                IRVisitor::visit(op);
            }
        }

        void visit(const Call *op) override {
            if (op->is_intrinsic({Call::shift_left,
                                  Call::shift_right,
                                  Call::rounding_shift_left,
                                  Call::rounding_shift_right,
                                  Call::widening_shift_left,
                                  Call::widening_shift_right,
                                  Call::mul_shift_right,
                                  Call::rounding_mul_shift_right})) {
                auto shift_bounds = bounds(op->args.back());
                if (!(shift_bounds > -op->type.bits() && shift_bounds < op->type.bits())) {
                    found = true;
                }
            } else if (op->is_intrinsic({Call::signed_integer_overflow})) {
                found = true;
            }
            IRVisitor::visit(op);
        }

    public:
        bool found = false;
    } checker;

    e.accept(&checker);

    return checker.found;
}

}  // namespace

FUZZ_TEST(lossless_cast, FuzzingContext &fuzz) {
    constexpr int size = 1024;
    Buffer<uint8_t> buf_u8(size, "buf_u8");
    Buffer<int8_t> buf_i8(size, "buf_i8");
    Var x{"x"};

    buf_u8.fill(fuzz);
    buf_i8.fill(fuzz);

    RandomExpressionGenerator reg{
        fuzz,
        {
            buf_u8(x),
            buf_i8(x),
            cast<uint8_t>(fuzz.ConsumeIntegral<uint8_t>()),
            cast<int8_t>(fuzz.ConsumeIntegral<int8_t>()),
            cast<uint8_t>(fuzz.ConsumeIntegral<uint8_t>()),
            cast<int8_t>(fuzz.ConsumeIntegral<int8_t>()),
        }};
    // Scalar integer types only, no bool. TODO: Int64 fails
    reg.fuzz_types = {UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32)};
    // Scalar only, disable vector-specific operations
    reg.gen_broadcast_of_vector = false;
    reg.gen_ramp_of_vector = false;
    reg.gen_shuffles = false;
    reg.gen_vector_reduce = false;
    reg.gen_reinterpret = false;

    constexpr int depth = 5;
    Expr e1 = reg.random_expr(reg.random_type(), depth);

    Expr simplified = simplify(e1);

    if (might_have_ub(e1) ||
        might_have_ub(simplified) ||
        might_have_ub(lower_intrinsics(simplified))) {
        return 0;
    }

    // We're also going to test constant_integer_bounds here.
    ConstantInterval bounds = constant_integer_bounds(e1);

    std::vector<Type> target_types = {UInt(32), Int(32), UInt(16), Int(16)};
    Type target = fuzz.PickValueInVector(target_types);
    Expr e2 = lossless_cast(target, e1);

    if (!e2.defined()) {
        return 0;
    }

    if (definitely_has_ub(e2)) {
        std::cerr << "lossless_cast introduced ub:\n"
                  << "e1 = " << e1 << "\n"
                  << "e2 = " << e2 << "\n"
                  << "simplify(e1) = " << simplify(e1) << "\n"
                  << "simplify(e2) = " << simplify(e2) << "\n";
        return 1;
    }

    Func f;
    f(x) = {cast<int64_t>(e1), cast<int64_t>(e2)};
    f.vectorize(x, 4, TailStrategy::RoundUp);

    Buffer<int64_t> out1(size), out2(size);
    Pipeline p(f);

    // Check for signed integer overflow
    // Module m = p.compile_to_module({}, "test");

    p.realize({out1, out2});

    for (int x = 0; x < size; x++) {
        if (out1(x) != out2(x)) {
            std::cerr
                << "lossless_cast failure\n"
                << "x = " << x << "\n"
                << "buf_u8 = " << (int)buf_u8(x) << "\n"
                << "buf_i8 = " << (int)buf_i8(x) << "\n"
                << "out1 = " << out1(x) << "\n"
                << "out2 = " << out2(x) << "\n"
                << "Original: " << e1 << "\n"
                << "Lossless cast: " << e2 << "\n";
            return 1;
        }
    }

    for (int x = 0; x < size; x++) {
        if ((e1.type().is_int() && !bounds.contains(out1(x))) ||
            (e1.type().is_uint() && !bounds.contains((uint64_t)out1(x)))) {
            Expr simplified = simplify(e1);
            std::cerr
                << "constant_integer_bounds failure\n"
                << "x = " << x << "\n"
                << "buf_u8 = " << (int)buf_u8(x) << "\n"
                << "buf_i8 = " << (int)buf_i8(x) << "\n"
                << "out1 = " << out1(x) << "\n"
                << "Expression: " << e1 << "\n"
                << "Bounds: " << bounds << "\n"
                << "Simplified: " << simplified << "\n"
                // If it's still out-of-bounds when the expression is
                // simplified, that'll be easier to debug.
                << "Bounds: " << constant_integer_bounds(simplified) << "\n";
            return 1;
        }
    }

    return 0;
}
