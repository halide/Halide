#include "constant_fold.h"

using namespace Halide;
using namespace Halide::Internal;

template<typename Op>
int64_t constant_fold_bin_op(halide_type_t &, int64_t, int64_t) noexcept;

template<typename Op>
uint64_t constant_fold_bin_op(halide_type_t &, uint64_t, uint64_t) noexcept;

template<typename Op>
double constant_fold_bin_op(halide_type_t &, double, double) noexcept;

template<typename Op>
uint64_t constant_fold_cmp_op(int64_t, int64_t) noexcept;

template<typename Op>
uint64_t constant_fold_cmp_op(uint64_t, uint64_t) noexcept;

template<typename Op>
uint64_t constant_fold_cmp_op(double, double) noexcept;

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Add>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    // t.lanes |= ((t.bits >= 32) && add_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    int dead_bits = 64 - t.bits;
    // Drop the high bits then sign-extend them back
    return int64_t((uint64_t(a) + uint64_t(b)) << dead_bits) >> dead_bits;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Add>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    uint64_t ones = (uint64_t)(-1);
    return (a + b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Add>(halide_type_t &t, double a, double b) noexcept {
    return a + b;
}

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Sub>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    // t.lanes |= ((t.bits >= 32) && sub_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    // Drop the high bits then sign-extend them back
    int dead_bits = 64 - t.bits;
    return int64_t((uint64_t(a) - uint64_t(b)) << dead_bits) >> dead_bits;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Sub>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    uint64_t ones = (uint64_t)(-1);
    return (a - b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Sub>(halide_type_t &t, double a, double b) noexcept {
    return a - b;
}

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Mul>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    // t.lanes |= ((t.bits >= 32) && mul_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    int dead_bits = 64 - t.bits;
    // Drop the high bits then sign-extend them back
    return int64_t((uint64_t(a) * uint64_t(b)) << dead_bits) >> dead_bits;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Mul>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    uint64_t ones = (uint64_t)(-1);
    return (a * b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Mul>(halide_type_t &t, double a, double b) noexcept {
    return a * b;
}

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Div>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return div_imp(a, b);
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Div>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return div_imp(a, b);
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Div>(halide_type_t &t, double a, double b) noexcept {
    return div_imp(a, b);
}

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Mod>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return mod_imp(a, b);
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Mod>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return mod_imp(a, b);
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Mod>(halide_type_t &t, double a, double b) noexcept {
    return mod_imp(a, b);
}

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Min>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return std::min(a, b);
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Min>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return std::min(a, b);
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Min>(halide_type_t &t, double a, double b) noexcept {
    return std::min(a, b);
}

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Max>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return std::max(a, b);
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Max>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return std::max(a, b);
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Max>(halide_type_t &t, double a, double b) noexcept {
    return std::max(a, b);
}

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<Or>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return (a | b) & 1;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<Or>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return (a | b) & 1;
}

template<>
HALIDE_ALWAYS_INLINE int64_t constant_fold_bin_op<And>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return a & b & 1;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_bin_op<And>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return a & b & 1;
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<And>(halide_type_t &t, double a, double b) noexcept {
    // Unreachable
    return 0;
}

template<>
HALIDE_ALWAYS_INLINE double constant_fold_bin_op<Or>(halide_type_t &t, double a, double b) noexcept {
    // Unreachable, as it would be a type mismatch.
    return 0;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<LT>(int64_t a, int64_t b) noexcept {
    return a < b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<LT>(uint64_t a, uint64_t b) noexcept {
    return a < b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<LT>(double a, double b) noexcept {
    return a < b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<GT>(int64_t a, int64_t b) noexcept {
    return a > b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<GT>(uint64_t a, uint64_t b) noexcept {
    return a > b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<GT>(double a, double b) noexcept {
    return a > b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<LE>(int64_t a, int64_t b) noexcept {
    return a <= b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<LE>(uint64_t a, uint64_t b) noexcept {
    return a <= b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<LE>(double a, double b) noexcept {
    return a <= b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<GE>(int64_t a, int64_t b) noexcept {
    return a >= b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<GE>(uint64_t a, uint64_t b) noexcept {
    return a >= b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<GE>(double a, double b) noexcept {
    return a >= b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<EQ>(int64_t a, int64_t b) noexcept {
    return a == b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<EQ>(uint64_t a, uint64_t b) noexcept {
    return a == b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<EQ>(double a, double b) noexcept {
    return a == b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<NE>(int64_t a, int64_t b) noexcept {
    return a != b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<NE>(uint64_t a, uint64_t b) noexcept {
    return a != b;
}

template<>
HALIDE_ALWAYS_INLINE uint64_t constant_fold_cmp_op<NE>(double a, double b) noexcept {
    return a != b;
}

class ConstantFold : public IRVisitor {
    using IRVisitor::visit;

    void visit(const IntImm *op) override {
        value.u.i64 = op->value;
    }

    void visit(const UIntImm *op) override {
        value.u.u64 = op->value;
    }

    void visit(const FloatImm *op) override {
        value.u.f64 = op->value;
    }

    template<typename BinOp>
    void visit_bin_op(const BinOp *op) {
        op->a.accept(this);
        auto a = value;
        op->b.accept(this);
        auto b = value;
        Type type = op->type;
        halide_type_t _type = type;
        switch (type.code()) {
            case Type::Int: {
                value.u.i64 = constant_fold_bin_op<BinOp>(_type, a.u.i64, b.u.i64);
                break;
            }
            case Type::UInt: {
                value.u.u64 = constant_fold_bin_op<BinOp>(_type, a.u.u64, b.u.u64);
                break;
            }
            case Type::Float:
            case Type::BFloat: {
                value.u.f64 = constant_fold_bin_op<BinOp>(_type, a.u.f64, b.u.f64);
                break;
            }
            default: {
                // Silent failure. Ask Andrew if we should error out.
                internal_error << "This is bad, what type is this?" << Expr(op) << "\n";
            }
        }
    }

    void visit(const Add *op) override {
        visit_bin_op<Add>(op);
    }

    void visit(const Sub *op) override {
        visit_bin_op<Sub>(op);
    }

    void visit(const Mul *op) override {
        visit_bin_op<Mul>(op);
    }

    void visit(const Div *op) override {
        visit_bin_op<Div>(op);
    }

    void visit(const Mod *op) override {
        visit_bin_op<Mod>(op);
    }

    void visit(const Min *op) override {
        visit_bin_op<Min>(op);
    }

    void visit(const Max *op) override {
        visit_bin_op<Max>(op);
    }

    void visit(const Or *op) override {
        visit_bin_op<Or>(op);
    }

    void visit(const And *op) override {
        visit_bin_op<And>(op);
    }

    template<typename CmpOp>
    void visit_cmp_op(const CmpOp *op) {
        op->a.accept(this);
        auto a = value;
        op->b.accept(this);
        auto b = value;
        Type arg_type = op->a.type();
        halide_type_t _type = op->type;
        switch (arg_type.code()) {
            case Type::Int: {
                value.u.u64 = constant_fold_cmp_op<CmpOp>(a.u.i64, b.u.i64);
                break;
            }
            case Type::UInt: {
                value.u.u64 = constant_fold_cmp_op<CmpOp>(a.u.u64, b.u.u64);
                break;
            }
            case Type::Float:
            case Type::BFloat: {
                value.u.u64 = constant_fold_cmp_op<CmpOp>(a.u.f64, b.u.f64);
                break;
            }
            default: {
                internal_error << "This is bad, what type are the arguments?" << Expr(op) << "\n";
            }
        }

    }

    void visit(const LT *op) override {
        visit_cmp_op<LT>(op);
    }

    void visit(const GT *op) override {
        visit_cmp_op<GT>(op);
    }

    void visit(const LE *op) override {
        visit_cmp_op<LE>(op);
    }

    void visit(const GE *op) override {
        visit_cmp_op<GE>(op);
    }

    void visit(const EQ *op) override {
        visit_cmp_op<EQ>(op);
    }

    void visit(const NE *op) override {
        visit_cmp_op<NE>(op);
    }

    void visit(const Call *op) {
        if (op->name == "can_prove") {
            assert(op->args.size() == 1);
            Expr expr = simplifier->mutate(op->args[0], nullptr);
            value.u.u64 = is_const_one(expr);
            return;
        } else {
            internal_error << "Bad call type in fold: " << Expr(op) << "\n";
        }
    }

public:
    halide_scalar_value_t value;  
    Simplify* simplifier;

    ConstantFold(Simplify* simplify) : simplifier{simplify} {}
};



Expr fold_actual(const Expr &expr, Simplify *simplify) {
    ConstantFold folder(simplify);
    expr.accept(&folder);
    Type type = expr.type();
    switch (type.code()) {
        case Type::Int: {
            return IntImm::make(type, folder.value.u.i64);
        }
        case Type::UInt: {
            return UIntImm::make(type, folder.value.u.u64);
        }
        case Type::Float:
        case Type::BFloat: {
            return FloatImm::make(type, folder.value.u.f64);
        }
        default: {
            assert(false);
            return Expr();
        }
    }
}