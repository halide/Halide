#include "constant_fold.h"

using namespace Halide;
using namespace Halide::Internal;

template<typename Op>
int64_t constant_fold_bin_op(halide_type_t &, int64_t, int64_t) noexcept;

template<typename Op>
uint64_t constant_fold_bin_op(halide_type_t &, uint64_t, uint64_t) noexcept;

template<typename Op>
double constant_fold_bin_op(halide_type_t &, double, double) noexcept;

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

    void visit(const Add *op) override {
        op->a.accept(this);
        auto a = value;
        op->b.accept(this);
        auto b = value;
        Type type = op->type;
        halide_type_t _type = type;
        switch (type.code()) {
            case Type::Int: {
                value.u.i64 = constant_fold_bin_op<Add>(_type, a.u.i64, b.u.i64);
                break;
            }
            case Type::UInt: {
                value.u.u64 = constant_fold_bin_op<Add>(_type, a.u.u64, b.u.u64);
                break;
            }
            case Type::Float:
            case Type::BFloat: {
                value.u.f64 = constant_fold_bin_op<Add>(_type, a.u.f64, b.u.f64);
                break;
            }
            default: {
                // Silent failure. Ask Andrew if we should error out.
                value.u.u64 = 0;
                break;
            }
        }
    }

    void visit(const Sub *op) override {
        op->a.accept(this);
        auto a = value;
        op->b.accept(this);
        auto b = value;
        Type type = op->type;
        halide_type_t _type = type;
        switch (type.code()) {
            case Type::Int: {
                value.u.i64 = constant_fold_bin_op<Sub>(_type, a.u.i64, b.u.i64);
                break;
            }
            case Type::UInt: {
                value.u.u64 = constant_fold_bin_op<Sub>(_type, a.u.u64, b.u.u64);
                break;
            }
            case Type::Float:
            case Type::BFloat: {
                value.u.f64 = constant_fold_bin_op<Sub>(_type, a.u.f64, b.u.f64);
                break;
            }
            default: {
                // Silent failure. Ask Andrew if we should error out.
                value.u.u64 = 0;
                break;
            }
        }
    }

public:
    halide_scalar_value_t value;  

};


Expr fold_actual(const Expr &expr) {
    ConstantFold folder;
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