#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {
class LosslessCastTest : public ::testing::Test {
protected:
    Type u8 = UInt(8);
    Type u16 = UInt(16);
    Type u32 = UInt(32);
    // Type u64 = UInt(64);
    Type i8 = Int(8);
    Type i16 = Int(16);
    Type i32 = Int(32);
    Type i64 = Int(64);
    Type u8x = UInt(8, 4);
    Type u16x = UInt(16, 4);
    Type u32x = UInt(32, 4);
    Expr var_i32 = Variable::make(Int(32), "x");
    Expr var_u8 = Variable::make(u8, "x");
    Expr var_u16 = Variable::make(u16, "x");
    Expr var_u8x = Variable::make(u8x, "x");

    static void check_lossless_cast(const Type &t, const Expr &in, const Expr &correct) {
        // TODO: write a matcher for Expr equality
        Expr result = lossless_cast(t, in);
        EXPECT_TRUE(equal(lossless_cast(t, in), correct))
            << "lossless_cast(" << t << ", " << in << ") = " << result;
    }
};

// TODO: move fuzz test to fuzz testing category
class LosslessCastFuzzTest : public ::testing::Test {
protected:
    static constexpr int kSize = 1024;
    Buffer<uint8_t> buf_u8{kSize, "buf_u8"};
    Buffer<int8_t> buf_i8{kSize, "buf_i8"};
    Var x{"x"};

    Expr random_expr(std::mt19937 &rng) const {
        std::vector<Expr> exprs;
        // Add some atoms
        exprs.push_back(cast<uint8_t>((uint8_t)rng()));
        exprs.push_back(cast<int8_t>((int8_t)rng()));
        exprs.push_back(cast<uint8_t>((uint8_t)rng()));
        exprs.push_back(cast<int8_t>((int8_t)rng()));
        exprs.push_back(buf_u8(x));
        exprs.push_back(buf_i8(x));

        // Make random combinations of them
        while (true) {
            Expr e;
            int i1 = rng() % exprs.size();
            int i2 = rng() % exprs.size();
            int i3 = rng() % exprs.size();
            int op = rng() % 8;
            Expr e1 = exprs[i1];
            Expr e2 = cast(e1.type(), exprs[i2]);
            Expr e3 = cast(e1.type().with_code(halide_type_uint), exprs[i3]);
            bool may_widen = e1.type().bits() < 64;
            Expr e2_narrow = exprs[i2];
            bool may_widen_right = e2_narrow.type() == e1.type().narrow();
            switch (op) {
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
                switch (rng() % 20) {
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
            if (e.type().bits() == 64 && (e.as<Cast>() == nullptr || ((rng() & 7) == 0))) {
                return e;
            }

            exprs.push_back(e);
        }
    }

    static bool definitely_has_ub(Expr e) {
        struct HasOverflow : IRVisitor {
            bool found = false;

            using IRVisitor::visit;
            void visit(const Call *op) override {
                if (op->is_intrinsic({Call::signed_integer_overflow})) {
                    found = true;
                }
                IRVisitor::visit(op);
            }
        } has_overflow;

        e = simplify(e);
        e.accept(&has_overflow);

        return has_overflow.found;
    }

    static bool might_have_ub(Expr e) {
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
};
}  // namespace

TEST_F(LosslessCastTest, CastU8ToI32) {
    Expr e = cast(u8, var_i32);
    check_lossless_cast(i32, e, cast(i32, e));
}

TEST_F(LosslessCastTest, CastU16ToI8NotLossless) {
    check_lossless_cast(u16, cast(i8, var_u16), Expr());
}

TEST_F(LosslessCastTest, CastU16ToI16NotLossless) {
    check_lossless_cast(u16, cast(i16, var_u16), Expr());
}

TEST_F(LosslessCastTest, CastU8ToU32CanCastToU16) {
    check_lossless_cast(u16, cast(u32, var_u8), cast(u16, var_u8));
}

TEST_F(LosslessCastTest, VectorReduceU16CastLossless) {
    Expr e = VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1);
    check_lossless_cast(u16, e, cast(u16, e));
}

TEST_F(LosslessCastTest, VectorReduceU32ToU16Optimized) {
    check_lossless_cast(
        u16,
        VectorReduce::make(VectorReduce::Add, cast(u32x, var_u8x), 1),
        VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1));
}

TEST_F(LosslessCastTest, SubtractionMayUnderflowNotLossless) {
    check_lossless_cast(u16, cast(u32, var_u8) - 16, Expr());
}

TEST_F(LosslessCastTest, AdditionNoOverflowLossless) {
    check_lossless_cast(u16, cast(u32, var_u8) + 16, cast(u16, var_u8) + 16);
}

TEST_F(LosslessCastTest, ConstantMinusVariableMayUnderflow) {
    check_lossless_cast(u16, 16 - cast(u32, var_u8), Expr());
}

TEST_F(LosslessCastTest, ConstantPlusVariableNoOverflow) {
    check_lossless_cast(u16, 16 + cast(u32, var_u8), 16 + cast(u16, var_u8));
}

TEST_F(LosslessCastTest, UnsignedTargetWithSignedAdditionNoOverflow) {
    // Check one where the target type is unsigned but there's a signed addition
    // (that can't overflow)
    check_lossless_cast(
        u32,
        cast(i64, cast(u16, var_u8) + cast(i32, 17)),
        cast(u32, cast(u16, var_u8)) + cast(u32, 17));
}

TEST_F(LosslessCastTest, UnsignedTargetWithSignedSubtractionMayOverflow) {
    // Check one where the target type is unsigned but there's a signed subtract
    // (that can overflow). It's not safe to enter the i16 sub
    check_lossless_cast(u32, cast(i64, cast(i16, 10) - cast(i16, 17)), Expr());
}

TEST_F(LosslessCastTest, ConstantMultiplicationNoOverflow) {
    check_lossless_cast(
        i32,
        cast(i64, 1024) * cast(i64, 1024) * cast(i64, 1024),
        (cast(i32, 1024) * 1024) * 1024);
}

TEST_F(LosslessCastFuzzTest, Fuzz) {
    uint32_t root_seed = time(NULL);
    std::mt19937 seed_generator(root_seed);
    SCOPED_TRACE(testing::Message() << "root_seed = " << root_seed);

    for (int i = 0; i < 1000; i++) {
        auto seed = seed_generator();
        SCOPED_TRACE(testing::Message() << "seed = " << seed);
        std::mt19937 rng{seed};

        buf_u8.fill(rng);
        buf_i8.fill(rng);

        Expr e1 = random_expr(rng);
        Expr simplified = simplify(e1);

        if (might_have_ub(e1) ||
            might_have_ub(simplified) ||
            might_have_ub(lower_intrinsics(simplified))) {
            continue;
        }

        SCOPED_TRACE(testing::Message() << "e1 = " << e1);
        SCOPED_TRACE(testing::Message() << "simplify(e1) = " << simplified);

        Type target;
        std::vector<Type> target_types = {UInt(32), Int(32), UInt(16), Int(16)};
        target = target_types[rng() % target_types.size()];
        SCOPED_TRACE(testing::Message() << "target type = " << target);

        Expr e2 = lossless_cast(target, e1);
        if (!e2.defined()) {
            continue;
        }

        SCOPED_TRACE(testing::Message() << "e2 = " << e2);
        SCOPED_TRACE(testing::Message() << "simplify(e2) = " << simplify(e2));

        ASSERT_FALSE(definitely_has_ub(e2)) << "lossless_cast introduced ub";

        Func f;
        f(x) = {cast<int64_t>(e1), cast<int64_t>(e2)};
        f.vectorize(x, 4, TailStrategy::RoundUp);

        Buffer<int64_t> out1(kSize), out2(kSize);
        Pipeline p(f);

        // Check for signed integer overflow
        // Module m = p.compile_to_module({}, "test");

        p.realize({out1, out2});

        for (int xx = 0; xx < kSize; xx++) {
            ASSERT_EQ(out1(xx), out2(xx))
                << "x = " << xx << "\n"
                << "buf_u8 = " << (int)buf_u8(xx) << "\n"
                << "buf_i8 = " << (int)buf_i8(xx) << "\n"
                << "out1 = " << out1(xx) << "\n"
                << "out2 = " << out2(xx) << "\n";
        }

        // We're also going to test constant_integer_bounds here.
        ConstantInterval bounds = constant_integer_bounds(e1);
        for (int xx = 0; xx < kSize; xx++) {
            ASSERT_FALSE((e1.type().is_int() && !bounds.contains(out1(xx))) ||
                         (e1.type().is_uint() && !bounds.contains((uint64_t)out1(xx))))
                << "x = " << xx << "\n"
                << "buf_u8(x) = " << (int)buf_u8(xx) << "\n"
                << "buf_i8(x) = " << (int)buf_i8(xx) << "\n"
                << "out1(x) = " << out1(xx) << "\n"
                << "Bounds: " << bounds << "\n"
                // If it's still out-of-bounds when the expression is
                // simplified, that'll be easier to debug.
                << "Bounds: " << constant_integer_bounds(simplified) << "\n";
        }
    }
}
