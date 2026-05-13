#include "fuzz_helpers.h"
#include <Halide.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

constexpr int size = 1024;
Buffer<uint8_t> buf_u8(size, "buf_u8");
Buffer<int8_t> buf_i8(size, "buf_i8");
Var x{"x"};

Expr random_expr(FuzzingContext &fuzz) {
    std::vector<Expr> exprs;
    // Add some atoms
    exprs.push_back(cast<uint8_t>(fuzz.ConsumeIntegral<uint8_t>()));
    exprs.push_back(cast<int8_t>(fuzz.ConsumeIntegral<int8_t>()));
    exprs.push_back(cast<uint8_t>(fuzz.ConsumeIntegral<uint8_t>()));
    exprs.push_back(cast<int8_t>(fuzz.ConsumeIntegral<int8_t>()));
    exprs.push_back(buf_u8(x));
    exprs.push_back(buf_i8(x));

    // Make random combinations of them
    while (true) {
        Expr e;
        Expr e1 = fuzz.PickValueInVector(exprs);

        Expr e2 = fuzz.PickValueInVector(exprs);
        Expr e2_narrow = e2;
        e2 = cast(e1.type(), e2);

        Expr e3 = cast(e1.type().with_code(halide_type_uint), fuzz.PickValueInVector(exprs));
        bool may_widen = e1.type().bits() < 64;
        bool may_widen_right = e2_narrow.type() == e1.type().narrow();
        switch (fuzz.ConsumeIntegralInRange(0, 7)) {
        case 0:
            if (may_widen) {
                e = cast(e1.type().widen(), e1);
            }
            break;
        case 1:
            if (may_widen) {
                e = cast(Int(e1.type().bits() * 2), e1);
            }
            break;
        case 2:
            e = e1 + e2;
            break;
        case 3:
            e = e1 - e2;
            break;
        case 4:
            e = e1 * e2;
            break;
        case 5:
            e = e1 / e2;
            break;
        case 6:
            // Introduce some lets
            e = common_subexpression_elimination(e1);
            break;
        case 7:
            switch (fuzz.ConsumeIntegralInRange(0, 19)) {
            case 0:
                if (may_widen) {
                    e = widening_add(e1, e2);
                }
                break;
            case 1:
                if (may_widen) {
                    e = widening_sub(e1, e2);
                }
                break;
            case 2:
                if (may_widen) {
                    e = widening_mul(e1, e2);
                }
                break;
            case 3:
                e = halving_add(e1, e2);
                break;
            case 4:
                e = rounding_halving_add(e1, e2);
                break;
            case 5:
                e = halving_sub(e1, e2);
                break;
            case 6:
                e = saturating_add(e1, e2);
                break;
            case 7:
                e = saturating_sub(e1, e2);
                break;
            case 8:
                e = count_leading_zeros(e1);
                break;
            case 9:
                e = count_trailing_zeros(e1);
                break;
            case 10:
                if (may_widen) {
                    e = rounding_mul_shift_right(e1, e2, e3);
                }
                break;
            case 11:
                if (may_widen) {
                    e = mul_shift_right(e1, e2, e3);
                }
                break;
            case 12:
                if (may_widen_right) {
                    e = widen_right_add(e1, e2_narrow);
                }
                break;
            case 13:
                if (may_widen_right) {
                    e = widen_right_sub(e1, e2_narrow);
                }
                break;
            case 14:
                if (may_widen_right) {
                    e = widen_right_mul(e1, e2_narrow);
                }
                break;
            case 15:
                e = e1 << e2;
                break;
            case 16:
                e = e1 >> e2;
                break;
            case 17:
                e = rounding_shift_right(e1, e2);
                break;
            case 18:
                e = rounding_shift_left(e1, e2);
                break;
            case 19:
                e = ~e1;
                break;
            }
        }

        if (!e.defined()) {
            continue;
        }

        // Stop when we get to 64 bits, but probably don't stop on a cast,
        // because that'll just get trivially stripped.
        if (e.type().bits() == 64 && (e.as<Cast>() == nullptr || fuzz.ConsumeIntegralInRange(0, 7) == 0)) {
            return e;
        }

        exprs.push_back(e);
    }
}

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
    buf_u8.fill(fuzz);
    buf_i8.fill(fuzz);

    Expr e1 = random_expr(fuzz);
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
