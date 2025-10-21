#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <sstream>
#include <utility>

#include "CSE.h"
#include "ConstantBounds.h"
#include "Debug.h"
#include "Func.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Interval.h"
#include "StrictifyFloat.h"
#include "Util.h"
#include "Var.h"

using namespace Halide::Internal;

namespace Halide {

// Evaluate a float polynomial efficiently, taking instruction latency
// into account. The high order terms come first. n is the number of
// terms, which is the degree plus one.
namespace {

Expr evaluate_polynomial(Expr x, float *coeff, int n) {
    internal_assert(n >= 2);

    Expr x2 = x * x;

    Expr even_terms = coeff[0];
    Expr odd_terms = coeff[1];

    for (int i = 2; i < n; i++) {
        if ((i & 1) == 0) {
            if (coeff[i] == 0.0f) {
                even_terms *= x2;
            } else {
                even_terms = even_terms * x2 + coeff[i];
            }
        } else {
            if (coeff[i] == 0.0f) {
                odd_terms *= x2;
            } else {
                odd_terms = odd_terms * x2 + coeff[i];
            }
        }
    }

    if ((n & 1) == 0) {
        return even_terms * std::move(x) + odd_terms;
    } else {
        return odd_terms * std::move(x) + even_terms;
    }
}

Expr stringify(const std::vector<Expr> &args) {
    if (args.empty()) {
        return Expr("");
    }

    return Call::make(type_of<const char *>(), Call::stringify,
                      args, Call::PureIntrinsic);
}

Expr combine_strings(const std::vector<Expr> &args) {
    if (args.empty()) {
        return Expr("");
    }

    // Insert spaces between each expr.
    std::vector<Expr> strings(args.size() * 2);
    for (size_t i = 0; i < args.size(); i++) {
        strings[i * 2] = args[i];
        if (i < args.size() - 1) {
            strings[i * 2 + 1] = Expr(" ");
        } else {
            strings[i * 2 + 1] = Expr("\n");
        }
    }

    // Now combine all adjacent string literals, which is
    // useful to reduce emitted code size when printing
    size_t i = 0;
    while (i < strings.size() - 1) {
        const auto *cur_str = strings[i].as<StringImm>();
        const auto *next_str = strings[i + 1].as<StringImm>();
        if (cur_str && next_str) {
            strings[i] = StringImm::make(cur_str->value + next_str->value);
            strings.erase(strings.begin() + i + 1);
            continue;
        }
        i++;
    }

    return stringify(strings);
}

}  // namespace

namespace Internal {

bool is_const(const Expr &e) {
    if (e.as<IntImm>() ||
        e.as<UIntImm>() ||
        e.as<FloatImm>() ||
        e.as<StringImm>()) {
        return true;
    } else if (const Cast *c = e.as<Cast>()) {
        return is_const(c->value);
    } else if (const Ramp *r = e.as<Ramp>()) {
        return is_const(r->base) && is_const(r->stride);
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return is_const(b->value);
    } else {
        return false;
    }
}

bool is_const(const Expr &e, int64_t value) {
    if (const IntImm *i = e.as<IntImm>()) {
        return i->value == value;
    } else if (const UIntImm *i = e.as<UIntImm>()) {
        return (value >= 0) && (i->value == (uint64_t)value);
    } else if (const FloatImm *i = e.as<FloatImm>()) {
        return i->value == value;
    } else if (const Cast *c = e.as<Cast>()) {
        return is_const(c->value, value);
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return is_const(b->value, value);
    } else {
        return false;
    }
}

bool is_no_op(const Stmt &s) {
    if (!s.defined()) {
        return true;
    }
    const Evaluate *e = s.as<Evaluate>();
    return e && is_pure(e->value);
}

namespace {

class ExprIsPure : public IRGraphVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) override {
        if (!op->is_pure()) {
            result = false;
        } else {
            IRGraphVisitor::visit(op);
        }
    }

    void visit(const Load *op) override {
        if (!op->image.defined() && !op->param.defined()) {
            // It's a load from an internal buffer, which could
            // mutate.
            result = false;
        } else {
            IRGraphVisitor::visit(op);
        }
    }

public:
    bool result = true;
};

}  // namespace

bool is_pure(const Expr &e) {
    ExprIsPure pure;
    e.accept(&pure);
    return pure.result;
}

std::optional<int64_t> as_const_int(const Expr &e) {
    if (!e.defined()) {
        return std::nullopt;
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return as_const_int(b->value);
    } else if (const IntImm *i = e.as<IntImm>()) {
        return i->value;
    } else {
        return std::nullopt;
    }
}

std::optional<uint64_t> as_const_uint(const Expr &e) {
    if (!e.defined()) {
        return std::nullopt;
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return as_const_uint(b->value);
    } else if (const UIntImm *i = e.as<UIntImm>()) {
        return i->value;
    } else {
        return std::nullopt;
    }
}

std::optional<double> as_const_float(const Expr &e) {
    if (!e.defined()) {
        return std::nullopt;
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return as_const_float(b->value);
    } else if (const FloatImm *f = e.as<FloatImm>()) {
        return f->value;
    } else {
        return std::nullopt;
    }
}

std::optional<int> is_const_power_of_two_integer(const Expr &e) {
    if (!(e.type().is_int() || e.type().is_uint())) {
        return std::nullopt;
    }

    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_const_power_of_two_integer(b->value);
    } else if (const Cast *c = e.as<Cast>()) {
        return is_const_power_of_two_integer(c->value);
    } else if (auto i = as_const_int(e)) {
        return is_const_power_of_two_integer(*i);
    } else if (auto u = as_const_uint(e)) {
        return is_const_power_of_two_integer(*u);
    } else {
        return std::nullopt;
    }
}

std::optional<int> is_const_power_of_two_integer(uint64_t val) {
    if (val && ((val & (val - 1)) == 0)) {
        return ctz64(val);
    } else {
        return std::nullopt;
    }
}

std::optional<int> is_const_power_of_two_integer(int64_t val) {
    return val < 0 ? std::nullopt : is_const_power_of_two_integer((uint64_t)val);
}

bool is_positive_const(const Expr &e) {
    if (const IntImm *i = e.as<IntImm>()) {
        return i->value > 0;
    }
    if (const UIntImm *u = e.as<UIntImm>()) {
        return u->value > 0;
    }
    if (const FloatImm *f = e.as<FloatImm>()) {
        return f->value > 0.0f;
    }
    if (const Cast *c = e.as<Cast>()) {
        Type to = c->type;
        Type from = c->value.type();
        if (!to.is_int_or_uint() || to.can_represent(from)) {
            // Either the cast does not lose information, or it's a
            // non-integral cast, so no overflow behavior to worry about.
            return is_positive_const(c->value);
        }
    }
    if (const Ramp *r = e.as<Ramp>()) {
        // slightly conservative
        return is_positive_const(r->base) && is_positive_const(r->stride);
    }
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_positive_const(b->value);
    }
    return false;
}

bool is_negative_const(const Expr &e) {
    if (const IntImm *i = e.as<IntImm>()) {
        return i->value < 0;
    }
    if (const FloatImm *f = e.as<FloatImm>()) {
        return f->value < 0.0f;
    }
    if (const Cast *c = e.as<Cast>()) {
        Type to = c->type;
        Type from = c->value.type();
        if (to.is_uint()) {
            // Early out.
            return false;
        }
        if (!to.is_int_or_uint() || to.can_represent(from)) {
            // Either the cast does not lose information, or it's a
            // non-integral cast, so no overflow behavior to worry about.
            return is_negative_const(c->value);
        }
    }
    if (const Ramp *r = e.as<Ramp>()) {
        // slightly conservative
        return is_negative_const(r->base) && is_negative_const(r->stride);
    }
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_negative_const(b->value);
    }
    return false;
}

bool is_undef(const Expr &e) {
    if (const Call *c = e.as<Call>()) {
        return c->is_intrinsic(Call::undef);
    }
    return false;
}

bool is_const_zero(const Expr &e) {
    if (const IntImm *int_imm = e.as<IntImm>()) {
        return int_imm->value == 0;
    }
    if (const UIntImm *uint_imm = e.as<UIntImm>()) {
        return uint_imm->value == 0;
    }
    if (const FloatImm *float_imm = e.as<FloatImm>()) {
        return float_imm->value == 0.0;
    }
    if (const Cast *c = e.as<Cast>()) {
        return is_const_zero(c->value);
    }
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_const_zero(b->value);
    }
    if (const Call *c = e.as<Call>()) {
        return (c->is_intrinsic(Call::bool_to_mask) || c->is_intrinsic(Call::cast_mask)) &&
               is_const_zero(c->args[0]);
    }
    return false;
}

bool is_const_one(const Expr &e) {
    if (const IntImm *int_imm = e.as<IntImm>()) {
        return int_imm->value == 1;
    }
    if (const UIntImm *uint_imm = e.as<UIntImm>()) {
        return uint_imm->value == 1;
    }
    if (const FloatImm *float_imm = e.as<FloatImm>()) {
        return float_imm->value == 1.0;
    }
    if (const Cast *c = e.as<Cast>()) {
        return is_const_one(c->value);
    }
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_const_one(b->value);
    }
    if (const Call *c = e.as<Call>()) {
        return (c->is_intrinsic(Call::bool_to_mask) || c->is_intrinsic(Call::cast_mask)) &&
               is_const_one(c->args[0]);
    }
    return false;
}

namespace {

template<typename T>
Expr make_const_helper(Type t, T val) {
    if (t.is_vector()) {
        return Broadcast::make(make_const(t.element_of(), val), t.lanes());
    } else if (t.is_int()) {
        return IntImm::make(t, (int64_t)val);
    } else if (t.is_uint()) {
        return UIntImm::make(t, (uint64_t)val);
    } else if (t.is_float()) {
        return FloatImm::make(t, (double)val);
    } else {
        internal_error << "Can't make a constant of type " << t << "\n";
        return Expr();
    }
}

}  // namespace

Expr make_const(Type t, int64_t val) {
    return make_const_helper(t, val);
}

Expr make_const(Type t, uint64_t val) {
    return make_const_helper(t, val);
}

Expr make_const(Type t, double val) {
    return make_const_helper(t, val);
}

Expr make_bool(bool val, int w) {
    return make_const(UInt(1, w), val);
}

Expr make_zero(Type t) {
    if (t.is_handle()) {
        return reinterpret(t, make_zero(UInt(64)));
    } else {
        return make_const(t, 0);
    }
}

Expr make_one(Type t) {
    return make_const(t, 1);
}

Expr make_two(Type t) {
    return make_const(t, 2);
}

Expr make_signed_integer_overflow(Type type) {
    static std::atomic<int> counter{0};
    return Call::make(type, Call::signed_integer_overflow, {counter++}, Call::Intrinsic);
}

bool is_signed_integer_overflow(const Expr &expr) {
    const Call *call = expr.as<Call>();
    return call && call->is_intrinsic(Call::signed_integer_overflow);
}

Expr const_true(int w) {
    return make_one(UInt(1, w));
}

Expr const_false(int w) {
    return make_zero(UInt(1, w));
}

Expr lossless_cast(Type t,
                   Expr e,
                   const Scope<ConstantInterval> &scope,
                   std::map<Expr, ConstantInterval, ExprCompare> *cache) {
    if (!e.defined() || t == e.type()) {
        return e;
    } else if (t.can_represent(e.type())) {
        return cast(t, std::move(e));
    } else if (const Cast *c = e.as<Cast>()) {
        if (c->type.can_represent(c->value.type())) {
            return lossless_cast(t, c->value, scope, cache);
        }
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        Expr v = lossless_cast(t.element_of(), b->value, scope, cache);
        if (v.defined()) {
            return Broadcast::make(v, b->lanes);
        }
    } else if (const IntImm *i = e.as<IntImm>()) {
        if (t.can_represent(i->value)) {
            return make_const(t, i->value);
        }
    } else if (const UIntImm *i = e.as<UIntImm>()) {
        if (t.can_represent(i->value)) {
            return make_const(t, i->value);
        }
    } else if (const FloatImm *f = e.as<FloatImm>()) {
        if (t.can_represent(f->value)) {
            return make_const(t, f->value);
        }
    } else if (const Shuffle *shuf = e.as<Shuffle>()) {
        std::vector<Expr> vecs;
        for (const auto &vec : shuf->vectors) {
            vecs.emplace_back(lossless_cast(t.with_lanes(vec.type().lanes()), vec, scope, cache));
            if (!vecs.back().defined()) {
                return Expr();
            }
        }
        return Shuffle::make(vecs, shuf->indices);
    } else if (t.is_int_or_uint()) {
        // Check the bounds. If they're small enough, we can throw narrowing
        // casts around e, or subterms.
        ConstantInterval ci = constant_integer_bounds(e, scope, cache);

        if (t.can_represent(ci)) {
            // There are certain IR nodes where if the result is expressible
            // using some type, and the args are expressible using that type,
            // then the operation can just be done in that type.
            if (const Add *op = e.as<Add>()) {
                Expr a = lossless_cast(t, op->a, scope, cache);
                Expr b = lossless_cast(t, op->b, scope, cache);
                if (a.defined() && b.defined()) {
                    return Add::make(a, b);
                }
            } else if (const Sub *op = e.as<Sub>()) {
                Expr a = lossless_cast(t, op->a, scope, cache);
                Expr b = lossless_cast(t, op->b, scope, cache);
                if (a.defined() && b.defined()) {
                    return Sub::make(a, b);
                }
            } else if (const Mul *op = e.as<Mul>()) {
                Expr a = lossless_cast(t, op->a, scope, cache);
                Expr b = lossless_cast(t, op->b, scope, cache);
                if (a.defined() && b.defined()) {
                    return Mul::make(a, b);
                }
            } else if (const Min *op = e.as<Min>()) {
                Expr a = lossless_cast(t, op->a, scope, cache);
                Expr b = lossless_cast(t, op->b, scope, cache);
                if (a.defined() && b.defined()) {
                    return Min::make(a, b);
                }
            } else if (const Max *op = e.as<Max>()) {
                Expr a = lossless_cast(t, op->a, scope, cache);
                Expr b = lossless_cast(t, op->b, scope, cache);
                if (a.defined() && b.defined()) {
                    return Max::make(a, b);
                }
            } else if (const Mod *op = e.as<Mod>()) {
                Expr a = lossless_cast(t, op->a, scope, cache);
                Expr b = lossless_cast(t, op->b, scope, cache);
                if (a.defined() && b.defined()) {
                    return Mod::make(a, b);
                }
            } else if (const Call *op = Call::as_intrinsic(e, {Call::widening_add, Call::widen_right_add})) {
                Expr a = lossless_cast(t, op->args[0], scope, cache);
                Expr b = lossless_cast(t, op->args[1], scope, cache);
                if (a.defined() && b.defined()) {
                    return Add::make(a, b);
                }
            } else if (const Call *op = Call::as_intrinsic(e, {Call::widening_sub, Call::widen_right_sub})) {
                Expr a = lossless_cast(t, op->args[0], scope, cache);
                Expr b = lossless_cast(t, op->args[1], scope, cache);
                if (a.defined() && b.defined()) {
                    return Sub::make(a, b);
                }
            } else if (const Call *op = Call::as_intrinsic(e, {Call::widening_mul, Call::widen_right_mul})) {
                Expr a = lossless_cast(t, op->args[0], scope, cache);
                Expr b = lossless_cast(t, op->args[1], scope, cache);
                if (a.defined() && b.defined()) {
                    return Mul::make(a, b);
                }
            } else if (const Call *op = Call::as_intrinsic(e, {Call::shift_left, Call::widening_shift_left,
                                                               Call::shift_right, Call::widening_shift_right})) {
                Expr a = lossless_cast(t, op->args[0], scope, cache);
                Expr b = lossless_cast(t, op->args[1], scope, cache);
                if (a.defined() && b.defined()) {
                    ConstantInterval cb = constant_integer_bounds(b, scope, cache);
                    if (cb > -t.bits() && cb < t.bits()) {
                        if (op->is_intrinsic({Call::shift_left, Call::widening_shift_left})) {
                            return a << b;
                        } else if (op->is_intrinsic({Call::shift_right, Call::widening_shift_right})) {
                            return a >> b;
                        }
                    }
                }
            } else if (const VectorReduce *op = e.as<VectorReduce>()) {
                if (op->op == VectorReduce::Add ||
                    op->op == VectorReduce::Min ||
                    op->op == VectorReduce::Max) {
                    Expr v = lossless_cast(t.with_lanes(op->value.type().lanes()), op->value, scope, cache);
                    if (v.defined()) {
                        return VectorReduce::make(op->op, v, op->type.lanes());
                    }
                }
            }

            // At this point we know the expression fits in the target type, but
            // what we really want is for the expression to be computed in the
            // target type. So we can add a cast to the target type if we want
            // here, but it only makes sense to do it if the expression type has
            // the same or fewer bits than the target type.
            if (e.type().bits() <= t.bits()) {
                return cast(t, e);
            }
        }
    }

    return Expr();
}

Expr lossless_negate(const Expr &x) {
    if (const Mul *m = x.as<Mul>()) {
        // Check the terms can't multiply to produce the most negative value.
        if (x.type().is_int() &&
            !x.type().can_represent(-constant_integer_bounds(x))) {
            return Expr();
        }

        Expr b = lossless_negate(m->b);
        if (b.defined()) {
            return Mul::make(m->a, b);
        }
        Expr a = lossless_negate(m->a);
        if (a.defined()) {
            return Mul::make(a, m->b);
        }

    } else if (const Call *m = Call::as_intrinsic(x, {Call::widening_mul})) {
        Expr b = lossless_negate(m->args[1]);
        if (b.defined()) {
            return widening_mul(m->args[0], b);
        }
        Expr a = lossless_negate(m->args[0]);
        if (a.defined()) {
            return widening_mul(a, m->args[1]);
        }
    } else if (const IntImm *i = x.as<IntImm>()) {
        if (!i->type.is_min(i->value)) {
            return IntImm::make(i->type, -i->value);
        }
    } else if (const FloatImm *f = x.as<FloatImm>()) {
        return FloatImm::make(f->type, -f->value);
    } else if (const Cast *c = x.as<Cast>()) {
        Expr value = lossless_negate(c->value);
        if (value.defined()) {
            // This logic is only sound if we know the cast can't overflow.
            value = lossless_cast(c->type, value);
            if (value.defined()) {
                return value;
            }
        }
    } else if (const Ramp *r = x.as<Ramp>()) {
        Expr base = lossless_negate(r->base);
        Expr stride = lossless_negate(r->stride);
        // slightly conservative
        if (base.defined() && stride.defined()) {
            return Ramp::make(base, stride, r->lanes);
        }
    } else if (const Broadcast *b = x.as<Broadcast>()) {
        Expr value = lossless_negate(b->value);
        if (value.defined()) {
            return Broadcast::make(value, b->lanes);
        }
    }

    return Expr();
}

void check_representable(Type dst, int64_t x) {
    if (dst.is_handle()) {
        user_assert(dst.can_represent(x))
            << "Integer constant " << x
            << " will be implicitly coerced to type " << dst
            << ", but Halide does not support pointer arithmetic.\n";
    } else {
        user_assert(dst.can_represent(x))
            << "Integer constant " << x
            << " will be implicitly coerced to type " << dst
            << ", which changes its value to " << make_const(dst, x)
            << ".\n";
    }
}

namespace {
void match_lanes(Expr &a, Expr &b) {
    // Broadcast scalar to match vector
    if (a.type().is_scalar() && b.type().is_vector()) {
        a = Broadcast::make(std::move(a), b.type().lanes());
    } else if (a.type().is_vector() && b.type().is_scalar()) {
        b = Broadcast::make(std::move(b), a.type().lanes());
    } else {
        internal_assert(a.type().lanes() == b.type().lanes()) << "Can't match types of differing widths";
    }
}

// Cast to the wider type of the two. Already guaranteed to leave
// signed/unsigned on number of lanes unchanged.
void match_bits(Expr &x, Expr &y) {
    // The signedness doesn't match, so just match the bits.
    if (x.type().bits() < y.type().bits()) {
        x = cast(x.type().with_bits(y.type().bits()), x);
    } else if (y.type().bits() < x.type().bits()) {
        y = cast(y.type().with_bits(x.type().bits()), y);
    }
}
}  // namespace

void match_types(Expr &a, Expr &b) {
    if (a.type() == b.type()) {
        return;
    }

    user_assert(!a.type().is_handle() && !b.type().is_handle())
        << "Can't do arithmetic on opaque pointer types: "
        << a << ", " << b << "\n";

    match_lanes(a, b);

    Type ta = a.type(), tb = b.type();

    // If type broadcasting has made the types match no additional casts are needed
    if (ta == tb) {
        return;
    }

    if (!ta.is_float() && tb.is_float()) {
        // int(a) * float(b) -> float(b)
        // uint(a) * float(b) -> float(b)
        a = cast(tb, std::move(a));
    } else if (ta.is_float() && !tb.is_float()) {
        b = cast(ta, std::move(b));
    } else if (ta.is_float() && tb.is_float()) {
        // float(a) * float(b) -> float(max(a, b))
        if (ta.bits() > tb.bits()) {
            b = cast(ta, std::move(b));
        } else {
            a = cast(tb, std::move(a));
        }
    } else if (ta.is_uint() && tb.is_uint()) {
        // uint(a) * uint(b) -> uint(max(a, b))
        if (ta.bits() > tb.bits()) {
            b = cast(ta, std::move(b));
        } else {
            a = cast(tb, std::move(a));
        }
    } else if (!ta.is_float() && !tb.is_float()) {
        // int(a) * (u)int(b) -> int(max(a, b))
        int bits = std::max(ta.bits(), tb.bits());
        int lanes = a.type().lanes();
        a = cast(Int(bits, lanes), std::move(a));
        b = cast(Int(bits, lanes), std::move(b));
    } else {
        internal_error << "Could not match types: " << ta << ", " << tb << "\n";
    }
}

void match_types_bitwise(Expr &x, Expr &y, const char *op_name) {
    user_assert(x.defined() && y.defined()) << op_name << " of undefined Expr\n";
    user_assert(x.type().is_int() || x.type().is_uint())
        << "The first argument to " << op_name << " must be an integer or unsigned integer";
    user_assert(y.type().is_int() || y.type().is_uint())
        << "The second argument to " << op_name << " must be an integer or unsigned integer";
    user_assert(y.type().is_int() == x.type().is_int())
        << "Arguments to " << op_name
        << " must be both be signed or both be unsigned.\n"
        << "LHS type: " << x.type() << " RHS type: " << y.type() << "\n"
        << "LHS value: " << x << " RHS value: " << y << "\n";

    // Broadcast scalar to match vector
    if (x.type().is_scalar() && y.type().is_vector()) {
        x = Broadcast::make(std::move(x), y.type().lanes());
    } else if (x.type().is_vector() && y.type().is_scalar()) {
        y = Broadcast::make(std::move(y), x.type().lanes());
    } else {
        internal_assert(x.type().lanes() == y.type().lanes()) << "Can't match types of differing widths";
    }

    // Cast to the wider type of the two.
    match_bits(x, y);
}

// Fast math ops based on those from Syrah (http://github.com/boulos/syrah). Thanks, Solomon!

namespace {
// Factor a float into 2^exponent * reduced, where reduced is between 0.75 and 1.5
void range_reduce_log(const Expr &input, Expr *reduced, Expr *exponent) {
    Type type = input.type();
    Type int_type = Int(32, type.lanes());
    Expr int_version = reinterpret(int_type, input);

    // single precision = SEEE EEEE EMMM MMMM MMMM MMMM MMMM MMMM
    // exponent mask    = 0111 1111 1000 0000 0000 0000 0000 0000
    //                    0x7  0xF  0x8  0x0  0x0  0x0  0x0  0x0
    // non-exponent     = 1000 0000 0111 1111 1111 1111 1111 1111
    //                  = 0x8  0x0  0x7  0xF  0xF  0xF  0xF  0xF
    Expr non_exponent_mask = make_const(int_type, 0x807fffff);

    // Extract a version with no exponent (between 1.0 and 2.0)
    Expr no_exponent = int_version & non_exponent_mask;

    // If > 1.5, we want to divide by two, to normalize back into the
    // range (0.75, 1.5). We can detect this by sniffing the high bit
    // of the mantissa.
    Expr new_exponent = no_exponent >> 22;

    Expr new_biased_exponent = 127 - new_exponent;
    Expr old_biased_exponent = int_version >> 23;
    *exponent = old_biased_exponent - new_biased_exponent;

    Expr blended = (int_version & non_exponent_mask) | (new_biased_exponent << 23);

    *reduced = reinterpret(type, blended);
}
}  // namespace

Expr halide_log(const Expr &x_full) {
    Type type = x_full.type();
    internal_assert(type.element_of() == Float(32));

    Expr nan = Call::make(type, "nan_f32", {}, Call::PureExtern);
    Expr neg_inf = Call::make(type, "neg_inf_f32", {}, Call::PureExtern);

    Expr use_nan = x_full < 0.0f;       // log of a negative returns nan
    Expr use_neg_inf = x_full == 0.0f;  // log of zero is -inf
    Expr exceptional = use_nan || use_neg_inf;

    // Avoid producing nans or infs by generating ln(1.0f) instead and
    // then fixing it later.
    Expr patched = select(exceptional, make_one(type), x_full);
    Expr reduced, exponent;
    range_reduce_log(patched, &reduced, &exponent);

    // Very close to the Taylor series for log about 1, but tuned to
    // have minimum relative error in the reduced domain (0.75 - 1.5).

    float coeff[] = {
        0.05111976432738144643f,
        -0.11793923497136414580f,
        0.14971993724699017569f,
        -0.16862004708254804686f,
        0.19980668101718729313f,
        -0.24991211576292837737f,
        0.33333435275479328386f,
        -0.50000106292873236491f,
        1.0f,
        0.0f};
    Expr x1 = reduced - 1.0f;
    Expr result = evaluate_polynomial(x1, coeff, sizeof(coeff) / sizeof(coeff[0]));

    result += cast(type, exponent) * logf(2.0);

    result = select(exceptional, select(use_nan, nan, neg_inf), result);

    // This introduces lots of common subexpressions
    result = common_subexpression_elimination(result);

    return result;
}

Expr halide_exp(const Expr &x_full) {
    Type type = x_full.type();
    internal_assert(type.element_of() == Float(32));

    float ln2_part1 = 0.6931457519f;
    float ln2_part2 = 1.4286067653e-6f;
    float one_over_ln2 = 1.0f / logf(2.0f);

    Expr scaled = x_full * one_over_ln2;
    Expr k_real = floor(scaled);
    Expr k = cast(Int(32, type.lanes()), k_real);

    Expr x = x_full - k_real * ln2_part1;
    x -= k_real * ln2_part2;

    float coeff[] = {
        0.00031965933071842413f,
        0.00119156835564003744f,
        0.00848988645943932717f,
        0.04160188091348320655f,
        0.16667983794100929562f,
        0.49999899033463041098f,
        1.0f,
        1.0f};
    Expr result = evaluate_polynomial(x, coeff, sizeof(coeff) / sizeof(coeff[0]));

    // Compute 2^k.
    int fpbias = 127;
    Expr biased = k + fpbias;

    Expr inf = Call::make(type, "inf_f32", {}, Call::PureExtern);

    // Shift the bits up into the exponent field and reinterpret this
    // thing as float.
    Expr two_to_the_n = reinterpret(type, biased << 23);
    result *= two_to_the_n;

    // Catch overflow and underflow
    result = select(biased < 255, result, inf);
    result = select(biased > 0, result, make_zero(type));

    // This introduces lots of common subexpressions
    result = common_subexpression_elimination(result);

    return result;
}

Expr halide_erf(const Expr &x_full) {
    user_assert(x_full.type() == Float(32)) << "halide_erf only works for Float(32)";

    // Extract the sign and magnitude.
    Expr sign = select(x_full < 0, -1.0f, 1.0f);
    Expr x = abs(x_full);

    // An approximation very similar to one from Abramowitz and
    // Stegun, but tuned for values > 1. Takes the form 1 - P(x)^-16.
    float c1[] = {0.0000818502f,
                  -0.0000026500f,
                  0.0009353904f,
                  0.0081960206f,
                  0.0430054424f,
                  0.0703310579f,
                  1.0f};
    Expr approx1 = evaluate_polynomial(x, c1, sizeof(c1) / sizeof(c1[0]));

    approx1 = 1.0f - pow(approx1, -16);

    // An odd polynomial tuned for values < 1. Similar to the Taylor
    // expansion of erf.
    float c2[] = {-0.0005553339f,
                  0.0048937243f,
                  -0.0266849239f,
                  0.1127890132f,
                  -0.3761207240f,
                  1.1283789803f};

    Expr approx2 = evaluate_polynomial(x * x, c2, sizeof(c2) / sizeof(c2[0]));
    approx2 *= x;

    // Switch between the two approximations based on the magnitude.
    Expr y = select(x > 1.0f, approx1, approx2);

    Expr result = common_subexpression_elimination(sign * y);

    return result;
}

Expr raise_to_integer_power(Expr e, int64_t p) {
    Expr result;
    if (p == 0) {
        result = make_one(e.type());
    } else if (p == 1) {
        result = std::move(e);
    } else if (p < 0) {
        result = make_one(e.type());
        result /= raise_to_integer_power(std::move(e), -p);
    } else {
        // p is at least 2
        if (p & 1) {
            Expr y = raise_to_integer_power(e, p >> 1);
            result = y * y * std::move(e);
        } else {
            e = raise_to_integer_power(std::move(e), p >> 1);
            result = e * e;
        }
    }
    return result;
}

void split_into_ands(const Expr &cond, std::vector<Expr> &result) {
    if (!cond.defined()) {
        return;
    }
    internal_assert(cond.type().is_bool()) << "Should be a boolean condition\n";
    if (const And *a = cond.as<And>()) {
        split_into_ands(a->a, result);
        split_into_ands(a->b, result);
    } else if (!is_const_one(cond)) {
        result.push_back(cond);
    }
}

Expr BufferBuilder::build() const {
    std::vector<Expr> args(10);
    if (buffer_memory.defined()) {
        args[0] = buffer_memory;
    } else {
        Expr sz = Call::make(Int(32), Call::size_of_halide_buffer_t, {}, Call::Intrinsic);
        args[0] = Call::make(type_of<struct halide_buffer_t *>(), Call::alloca, {sz}, Call::Intrinsic);
    }

    std::string shape_var_name = unique_name('t');
    Expr shape_var = Variable::make(type_of<halide_dimension_t *>(), shape_var_name);
    if (shape_memory.defined()) {
        args[1] = shape_memory;
    } else if (dimensions == 0) {
        args[1] = make_zero(type_of<halide_dimension_t *>());
    } else {
        args[1] = shape_var;
    }

    if (host.defined()) {
        args[2] = host;
    } else {
        args[2] = make_zero(type_of<void *>());
    }

    if (device.defined()) {
        args[3] = device;
    } else {
        args[3] = make_zero(UInt(64));
    }

    if (device_interface.defined()) {
        args[4] = device_interface;
    } else {
        args[4] = make_zero(type_of<struct halide_device_interface_t *>());
    }

    args[5] = (int)type.code();
    args[6] = type.bits();
    args[7] = dimensions;

    std::vector<Expr> shape;
    for (size_t i = 0; i < (size_t)dimensions; i++) {
        if (i < mins.size()) {
            shape.push_back(mins[i]);
        } else {
            shape.emplace_back(0);
        }
        if (i < extents.size()) {
            shape.push_back(extents[i]);
        } else {
            shape.emplace_back(0);
        }
        if (i < strides.size()) {
            shape.push_back(strides[i]);
        } else {
            shape.emplace_back(0);
        }
        // per-dimension flags, currently unused.
        shape.emplace_back(0);
    }
    for (const Expr &e : shape) {
        internal_assert(e.type() == Int(32))
            << "Buffer shape fields must be int32_t:" << e << "\n";
    }
    Expr shape_arg = Call::make(type_of<halide_dimension_t *>(), Call::make_struct, shape, Call::Intrinsic);
    if (shape_memory.defined()) {
        args[8] = shape_arg;
    } else if (dimensions == 0) {
        args[8] = make_zero(type_of<halide_dimension_t *>());
    } else {
        args[8] = shape_var;
    }

    Expr flags = make_zero(UInt(64));
    if (host_dirty.defined()) {
        flags = select(host_dirty,
                       make_const(UInt(64), halide_buffer_flag_host_dirty),
                       make_zero(UInt(64)));
    }
    if (device_dirty.defined()) {
        flags = flags | select(device_dirty,
                               make_const(UInt(64), halide_buffer_flag_device_dirty),
                               make_zero(UInt(64)));
    }
    args[9] = flags;

    Expr e = Call::make(type_of<struct halide_buffer_t *>(), Call::buffer_init, args, Call::Extern);

    if (!shape_memory.defined() && dimensions != 0) {
        e = Let::make(shape_var_name, shape_arg, e);
    }

    return e;
}

Expr strided_ramp_base(const Expr &e, int stride) {
    const Ramp *r = e.as<Ramp>();
    if (r == nullptr) {
        return Expr();
    }

    const IntImm *i = r->stride.as<IntImm>();
    if (i != nullptr && i->value == stride) {
        return r->base;
    }

    return Expr();
}

namespace {

// Replace a specified list of intrinsics with their first arg.
class RemoveIntrinsics : public IRMutator {
    using IRMutator::visit;
    const std::initializer_list<Call::IntrinsicOp> &ops;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(ops)) {
            return mutate(op->args[0]);
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    RemoveIntrinsics(const std::initializer_list<Call::IntrinsicOp> &ops)
        : ops(ops) {
    }
};

}  // namespace

Expr remove_likelies(const Expr &e) {
    return RemoveIntrinsics({Call::likely, Call::likely_if_innermost}).mutate(e);
}

Stmt remove_likelies(const Stmt &s) {
    return RemoveIntrinsics({Call::likely, Call::likely_if_innermost}).mutate(s);
}

Expr remove_promises(const Expr &e) {
    return RemoveIntrinsics({Call::promise_clamped, Call::unsafe_promise_clamped}).mutate(e);
}

Stmt remove_promises(const Stmt &s) {
    return RemoveIntrinsics({Call::promise_clamped, Call::unsafe_promise_clamped}).mutate(s);
}

Expr unwrap_tags(const Expr &e) {
    if (const Call *tag = Call::as_tag(e)) {
        return unwrap_tags(tag->args[0]);
    }
    return e;
}

Expr requirement_failed_error(Expr condition, const std::vector<Expr> &args) {
    return Call::make(Int(32),
                      "halide_error_requirement_failed",
                      {stringify({std::move(condition)}), combine_strings(args)},
                      Call::Extern);
}

Expr memoize_tag_helper(Expr result, const std::vector<Expr> &cache_key_values) {
    Type t = result.type();
    std::vector<Expr> args;
    args.push_back(std::move(result));
    args.insert(args.end(), cache_key_values.begin(), cache_key_values.end());
    return Call::make(t, Call::memoize_expr,
                      args, Call::PureIntrinsic);
}

}  // namespace Internal

Expr widen_right_add(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "widen_right_add of undefined Expr\n"
                                            << a << ", " << b << "\n";
    user_assert(a.type().is_int_or_uint() && b.type().is_int_or_uint())
        << "widen_right_add only defined for integer types, received:\n " << a << "\n " << b << "\n";
    user_assert(b.type().bits() <= 32) << "widen_right_add of large Expr\n"
                                       << a << ", " << b << "\n";
    match_lanes(a, b);
    Type wide_type = b.type().widen();
    user_assert(wide_type == a.type()) << "widen_right_add type mismatch\n " << a << "\n " << b << "\n";
    return Call::make(wide_type, Call::widen_right_add, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr widen_right_mul(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "widen_right_mul of undefined Expr\n"
                                            << a << ", " << b << "\n";
    user_assert(a.type().is_int_or_uint() && b.type().is_int_or_uint())
        << "widen_right_mul only defined for integer types, received:\n " << a << "\n " << b << "\n";
    user_assert(b.type().bits() <= 32) << "widen_right_mul of large Expr\n"
                                       << a << ", " << b << "\n";
    match_lanes(a, b);
    Type wide_type = b.type().widen();
    user_assert(wide_type == a.type()) << "widen_right_mul type mismatch\n " << a << "\n " << b << "\n";
    return Call::make(wide_type, Call::widen_right_mul, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr widen_right_sub(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "widen_right_sub of undefined Expr\n"
                                            << a << ", " << b << "\n";
    user_assert(a.type().is_int_or_uint() && b.type().is_int_or_uint())
        << "widen_right_sub only defined for integer types, received:\n " << a << "\n " << b << "\n";
    user_assert(b.type().bits() <= 32) << "widen_right_sub of large Expr\n"
                                       << a << ", " << b << "\n";
    match_lanes(a, b);
    Type wide_type = b.type().widen();
    user_assert(wide_type == a.type()) << "widen_right_sub type mismatch\n"
                                       << a << ", " << b << "\n";
    return Call::make(wide_type, Call::widen_right_sub, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr widening_add(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "widening_add of undefined Expr\n";
    match_types(a, b);
    Type wide_type = a.type().widen();
    return Call::make(wide_type, Call::widening_add, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr widening_mul(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "widening_mul of undefined Expr\n";
    // Promote float to int if lossless
    if (a.type().is_float() && b.type().is_int_or_uint()) {
        Expr float_b = lossless_cast(a.type(), b);
        user_assert(float_b.defined())
            << "widening_mul: cannot promote RHS of type " << b.type() << " to " << a.type() << ".\n"
            << "Please use an explicit cast.\n";
        b = float_b;
    } else if (b.type().is_float() && a.type().is_int_or_uint()) {
        Expr float_a = lossless_cast(b.type(), a);
        user_assert(float_a.defined())
            << "widening_mul: cannot promote LHS of type " << a.type() << " to " << b.type() << ".\n"
            << "Please use an explicit cast.\n";
        a = float_a;
    }
    // Widening multiplies can have different signs.
    match_bits(a, b);
    match_lanes(a, b);
    Type wide_type = a.type().widen();
    if (wide_type.is_uint() && b.type().is_int()) {
        wide_type = wide_type.with_code(halide_type_int);
    }
    return Call::make(wide_type, Call::widening_mul, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr widening_sub(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "widening_sub of undefined Expr\n";
    match_types(a, b);
    Type wide_type = a.type().widen();
    if (wide_type.is_uint()) {
        // always produce a signed result.
        wide_type = wide_type.with_code(halide_type_int);
    }
    return Call::make(wide_type, Call::widening_sub, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr widening_shift_left(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "widening_shift_left of undefined Expr\n";
    match_lanes(a, b);
    match_bits(a, b);
    Type wide_type = a.type().widen();
    return Call::make(wide_type, Call::widening_shift_left, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr widening_shift_left(Expr a, int b) {
    user_assert(a.defined()) << "widening_shift_left of undefined Expr\n";
    Type bt = a.type().with_code(halide_type_uint);
    if (b >= 0) {
        check_representable(bt, b);
        return widening_shift_left(std::move(a), make_const(bt, b));
    } else {
        check_representable(bt, -b);
        return widening_shift_right(std::move(a), make_const(bt, -b));
    }
}

Expr widening_shift_right(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "widening_shift_right of undefined Expr\n";
    match_lanes(a, b);
    match_bits(a, b);
    Type wide_type = a.type().widen();
    return Call::make(wide_type, Call::widening_shift_right, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr widening_shift_right(Expr a, int b) {
    user_assert(a.defined()) << "widening_shift_right of undefined Expr\n";
    Type bt = a.type().with_code(halide_type_uint);
    if (b >= 0) {
        check_representable(bt, b);
        return widening_shift_right(std::move(a), make_const(bt, b));
    } else {
        check_representable(bt, -b);
        return widening_shift_left(std::move(a), make_const(bt, -b));
    }
}

Expr rounding_shift_right(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "rounding_shift_right of undefined Expr\n";
    match_lanes(a, b);
    match_bits(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::rounding_shift_right, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr rounding_shift_right(Expr a, int b) {
    user_assert(a.defined()) << "rounding_shift_right of undefined Expr\n";
    Type bt = a.type().with_code(halide_type_uint);
    if (b >= 0) {
        check_representable(bt, b);
        return rounding_shift_right(std::move(a), make_const(bt, b));
    } else {
        check_representable(bt, -b);
        return rounding_shift_left(std::move(a), make_const(bt, -b));
    }
}

Expr rounding_shift_left(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "rounding_shift_left of undefined Expr\n";
    match_lanes(a, b);
    match_bits(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::rounding_shift_left, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr rounding_shift_left(Expr a, int b) {
    user_assert(a.defined()) << "rounding_shift_left of undefined Expr\n";
    Type bt = a.type().with_code(halide_type_uint);
    if (b >= 0) {
        check_representable(bt, b);
        return rounding_shift_left(std::move(a), make_const(bt, b));
    } else {
        check_representable(bt, -b);
        return rounding_shift_right(std::move(a), make_const(bt, -b));
    }
}

Expr saturating_add(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "saturating_add of undefined Expr\n";
    match_types(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::saturating_add, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr saturating_sub(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "saturating_sub of undefined Expr\n";
    match_types(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::saturating_sub, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr halving_add(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "halving_add of undefined Expr\n";
    match_types(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::halving_add, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr rounding_halving_add(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "rounding_halving_add of undefined Expr\n";
    match_types(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::rounding_halving_add, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr halving_sub(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "halving_sub of undefined Expr\n";
    match_types(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::halving_sub, {std::move(a), std::move(b)}, Call::PureIntrinsic);
}

Expr mul_shift_right(Expr a, Expr b, Expr q) {
    user_assert(a.defined() && b.defined() && q.defined()) << "mul_shift_right of undefined Expr\n";
    user_assert(q.type().is_uint()) << "mul_shift_right shift must be unsigned\n";
    match_types(a, b);
    match_lanes(a, q);
    match_bits(a, q);
    // q might have widened the bits or lanes of a.
    match_types(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::mul_shift_right, {std::move(a), std::move(b), std::move(q)}, Call::PureIntrinsic);
}

Expr mul_shift_right(Expr a, Expr b, int q) {
    user_assert(a.defined() && b.defined()) << "mul_shift_right of undefined Expr\n";
    Type qt = a.type().with_code(halide_type_uint);
    check_representable(qt, q);
    return mul_shift_right(std::move(a), std::move(b), make_const(qt, q));
}

Expr rounding_mul_shift_right(Expr a, Expr b, Expr q) {
    user_assert(a.defined() && b.defined() && q.defined()) << "rounding_mul_shift_right of undefined Expr\n";
    user_assert(q.type().is_uint()) << "rounding_mul_shift_right shift must be unsigned\n";
    match_types(a, b);
    match_lanes(a, q);
    match_bits(a, q);
    // q might have widened the bits or lanes of a.
    match_types(a, b);
    Type result_type = a.type();
    return Call::make(result_type, Call::rounding_mul_shift_right, {std::move(a), std::move(b), std::move(q)}, Call::PureIntrinsic);
}

Expr rounding_mul_shift_right(Expr a, Expr b, int q) {
    user_assert(a.defined() && b.defined()) << "rounding_mul_shift_right of undefined Expr\n";
    Type qt = a.type().with_code(halide_type_uint);
    check_representable(qt, q);
    return rounding_mul_shift_right(std::move(a), std::move(b), make_const(qt, q));
}

Expr fast_log(const Expr &x) {
    user_assert(x.type() == Float(32)) << "fast_log only works for Float(32)";

    Expr reduced, exponent;
    range_reduce_log(x, &reduced, &exponent);

    Expr x1 = reduced - 1.0f;

    float coeff[] = {
        0.07640318789187280912f,
        -0.16252961013874300811f,
        0.20625219040645212387f,
        -0.25110261010892864775f,
        0.33320464908377461777f,
        -0.49997513376789826101f,
        1.0f,
        0.0f};

    Expr result = evaluate_polynomial(x1, coeff, sizeof(coeff) / sizeof(coeff[0]));
    result = result + cast<float>(exponent) * logf(2);
    result = common_subexpression_elimination(result);
    return result;
}

namespace {

// A vectorizable sine and cosine implementation. Based on syrah fast vector math
// https://github.com/boulos/syrah/blob/master/src/include/syrah/FixedVectorMath.h#L55
Expr fast_sin_cos(const Expr &x_full, bool is_sin) {
    const float two_over_pi = 0.636619746685028076171875f;
    const float pi_over_two = 1.57079637050628662109375f;
    Expr scaled = x_full * two_over_pi;
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);
    Expr k_mod4 = k % 4;
    Expr sin_usecos = is_sin ? ((k_mod4 == 1) || (k_mod4 == 3)) : ((k_mod4 == 0) || (k_mod4 == 2));
    Expr flip_sign = is_sin ? (k_mod4 > 1) : ((k_mod4 == 1) || (k_mod4 == 2));

    // Reduce the angle modulo pi/2.
    Expr x = x_full - k_real * pi_over_two;

    const float sin_c2 = -0.16666667163372039794921875f;
    const float sin_c4 = 8.333347737789154052734375e-3;
    const float sin_c6 = -1.9842604524455964565277099609375e-4;
    const float sin_c8 = 2.760012648650445044040679931640625e-6;
    const float sin_c10 = -2.50293279435709337121807038784027099609375e-8;

    const float cos_c2 = -0.5f;
    const float cos_c4 = 4.166664183139801025390625e-2;
    const float cos_c6 = -1.388833043165504932403564453125e-3;
    const float cos_c8 = 2.47562347794882953166961669921875e-5;
    const float cos_c10 = -2.59630184018533327616751194000244140625e-7;

    Expr outside = select(sin_usecos, 1, x);
    Expr c2 = select(sin_usecos, cos_c2, sin_c2);
    Expr c4 = select(sin_usecos, cos_c4, sin_c4);
    Expr c6 = select(sin_usecos, cos_c6, sin_c6);
    Expr c8 = select(sin_usecos, cos_c8, sin_c8);
    Expr c10 = select(sin_usecos, cos_c10, sin_c10);

    Expr x2 = x * x;
    Expr tri_func = outside * (x2 * (x2 * (x2 * (x2 * (x2 * c10 + c8) + c6) + c4) + c2) + 1);
    return select(flip_sign, -tri_func, tri_func);
}

}  // namespace

Expr fast_sin(const Expr &x_full) {
    return fast_sin_cos(x_full, true);
}

Expr fast_cos(const Expr &x_full) {
    return fast_sin_cos(x_full, false);
}

Expr fast_exp(const Expr &x_full) {
    user_assert(x_full.type() == Float(32)) << "fast_exp only works for Float(32)";

    Expr scaled = x_full / logf(2.0);
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);
    Expr x = x_full - k_real * logf(2.0);

    float coeff[] = {
        0.01314350012789660196f,
        0.03668965196652099192f,
        0.16873890085469545053f,
        0.49970514590562437052f,
        1.0f,
        1.0f};
    Expr result = evaluate_polynomial(x, coeff, sizeof(coeff) / sizeof(coeff[0]));

    // Compute 2^k.
    int fpbias = 127;
    Expr biased = clamp(k + fpbias, 0, 255);

    // Shift the bits up into the exponent field and reinterpret this
    // thing as float.
    Expr two_to_the_n = reinterpret<float>(biased << 23);
    result *= two_to_the_n;
    result = common_subexpression_elimination(result);
    return result;
}

Expr print(const std::vector<Expr> &args) {
    Expr combined_string = combine_strings(args);

    // Call halide_print.
    Expr print_call =
        Call::make(Int(32), "halide_print",
                   {combined_string}, Call::Extern);

    // Return the first argument.
    Expr result =
        Call::make(args[0].type(), Call::return_second,
                   {print_call, args[0]}, Call::PureIntrinsic);
    return result;
}

Expr print_when(Expr condition, const std::vector<Expr> &args) {
    Expr p = print(args);
    return Call::make(p.type(),
                      Call::if_then_else,
                      {std::move(condition), p, args[0]},
                      Call::PureIntrinsic);
}

Expr require(Expr condition, const std::vector<Expr> &args) {
    user_assert(condition.defined()) << "Require of undefined condition.\n";
    user_assert(condition.type().is_bool()) << "Require condition must be a boolean type.\n";
    user_assert(args.at(0).defined()) << "Require of undefined value.\n";

    Expr err = requirement_failed_error(condition, args);

    return Call::make(args[0].type(),
                      Call::require,
                      {likely(std::move(condition)), args[0], std::move(err)},
                      Call::Intrinsic);
}

Expr saturating_cast(Type t, Expr e) {
    return Call::make(t, Call::saturating_cast, {std::move(e)}, Call::PureIntrinsic);
}

Expr select(Expr condition, Expr true_value, Expr false_value) {
    if (as_const_int(condition)) {
        // Why are you doing this? We'll preserve the select node until constant folding for you.
        condition = cast(Bool(true_value.type().lanes()), std::move(condition));
    }

    // Coerce int literals to the type of the other argument
    if (as_const_int(true_value)) {
        true_value = cast(false_value.type(), std::move(true_value));
    }
    if (as_const_int(false_value)) {
        false_value = cast(true_value.type(), std::move(false_value));
    }

    user_assert(condition.type().is_bool())
        << "The first argument to a select must be a boolean:\n"
        << "  " << condition << " has type " << condition.type() << "\n";

    user_assert(true_value.type() == false_value.type())
        << "The second and third arguments to a select do not have a matching type:\n"
        << "  " << true_value << " has type " << true_value.type() << "\n"
        << "  " << false_value << " has type " << false_value.type() << "\n";

    if (true_value.type().is_vector() && condition.type().is_scalar()) {
        condition = Broadcast::make(std::move(condition), true_value.type().lanes());
    }

    return Select::make(std::move(condition), std::move(true_value), std::move(false_value));
}

Tuple select(const Tuple &condition, const Tuple &true_value, const Tuple &false_value) {
    user_assert(condition.size() == true_value.size() && true_value.size() == false_value.size())
        << "select() on Tuples requires all Tuples to have identical sizes.";
    Tuple result(std::vector<Expr>(condition.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = select(condition[i], true_value[i], false_value[i]);
    }
    return result;
}

Tuple select(const Expr &condition, const Tuple &true_value, const Tuple &false_value) {
    user_assert(true_value.size() == false_value.size())
        << "select() on Tuples requires all Tuples to have identical sizes.";
    Tuple result(std::vector<Expr>(true_value.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = select(condition, true_value[i], false_value[i]);
    }
    return result;
}

Expr select(const Expr &condition, const FuncRef &true_value, const FuncRef &false_value) {
    return select(condition, (Expr)true_value, (Expr)false_value);
}

Expr mux(const Expr &id, const std::vector<Expr> &values) {
    user_assert(!values.empty()) << "mux() requires a non-empty vector of values";
    if (values.size() == 1) {
        // Useful in generic code where the size of the values vector
        // might be degenerate.
        return values[0];
    }

    // Check if all the values have the same type.
    Type t = values[0].type();
    for (int i = 1; i < (int)values.size(); i++) {
        user_assert(values[i].type() == t) << "mux() requires all the values to have the same type.";
    }
    std::vector<Expr> result{id};
    result.insert(result.end(), values.begin(), values.end());
    return Call::make(t, Call::mux, result, Call::PureIntrinsic);
}

Expr mux(const Expr &id, const Tuple &tup) {
    return mux(id, tup.as_vector());
}

Expr mux(const Expr &id, const std::initializer_list<Expr> &values) {
    return mux(id, std::vector<Expr>(values));
}

Expr mux(const Expr &id, const std::initializer_list<FuncRef> &values) {
    std::vector<Expr> exprs(values.size());
    for (size_t i = 0; i < values.size(); i++) {
        exprs[i] = Expr(*(values.begin() + i));
    }
    return mux(id, exprs);
}

Tuple mux(const Expr &id, const std::initializer_list<Tuple> &values) {
    return mux(id, std::vector<Tuple>(values));
}

Tuple mux(const Expr &id, const std::vector<Tuple> &values) {
    user_assert(!values.empty()) << "mux() requires a non-empty vector of values";
    std::vector<Expr> result(values[0].size());
    for (size_t i = 0; i < result.size(); i++) {
        std::vector<Expr> elems(values.size());
        for (size_t j = 0; j < values.size(); j++) {
            elems[j] = values[j][i];
        }
        result[i] = mux(id, elems);
    }
    return Tuple{result};
}

namespace {
void cast_bounds_for_promise_clamped(const Expr &value, const Expr &min, const Expr &max, Expr &casted_min, Expr &casted_max, const char *call_name) {
    {
        Expr n_min_val = lossless_cast(value.type(), min);
        if (min.defined()) {
            user_assert(n_min_val.defined())
                << call_name << " min argument (type " << min.node_type() << " " << min.type() << ") could not be cast losslessly to " << value.type();
        }
        casted_min = n_min_val.defined() ? n_min_val : value.type().min();
    }
    {
        Expr n_max_val = lossless_cast(value.type(), max);
        if (max.defined()) {
            user_assert(n_max_val.defined())
                << call_name << " max argument (type " << max.node_type() << " " << max.type() << ") could not be cast losslessly to " << value.type();
        }
        casted_max = n_max_val.defined() ? n_max_val : value.type().max();
    }
}
}  // namespace

Expr unsafe_promise_clamped(const Expr &value, const Expr &min, const Expr &max) {
    user_assert(value.defined()) << "unsafe_promise_clamped with undefined value.\n";
    Expr n_min_val, n_max_val;
    cast_bounds_for_promise_clamped(value, min, max, n_min_val, n_max_val, "unsafe_promise_clamped");

    // Min and max are allowed to be undefined with the meaning of no bound on that side.
    return Call::make(value.type(),
                      Call::unsafe_promise_clamped,
                      {value, n_min_val, n_max_val},
                      Call::PureIntrinsic);
}

namespace Internal {
Expr promise_clamped(const Expr &value, const Expr &min, const Expr &max) {
    internal_assert(value.defined()) << "promise_clamped with undefined value.\n";
    Expr n_min_val, n_max_val;
    cast_bounds_for_promise_clamped(value, min, max, n_min_val, n_max_val, "promise_clamped");

    // Min and max are allowed to be undefined with the meaning of no bound on that side.
    return Call::make(value.type(),
                      Call::promise_clamped,
                      {value, n_min_val, n_max_val},
                      Call::PureIntrinsic);
}
}  // namespace Internal

Expr operator+(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator+ of undefined Expr\n";
    match_types(a, b);
    return Add::make(std::move(a), std::move(b));
}

Expr operator+(Expr a, int b) {
    user_assert(a.defined()) << "operator+ of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return Add::make(std::move(a), make_const(t, b));
}

Expr operator+(int a, Expr b) {
    user_assert(b.defined()) << "operator+ of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return Add::make(make_const(t, a), std::move(b));
}

Expr &operator+=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator+= of undefined Expr\n";
    Type t = a.type();
    a = Add::make(std::move(a), cast(t, std::move(b)));
    return a;
}

Expr operator-(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator- of undefined Expr\n";
    match_types(a, b);
    return Sub::make(std::move(a), std::move(b));
}

Expr operator-(Expr a, int b) {
    user_assert(a.defined()) << "operator- of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return Sub::make(std::move(a), make_const(t, b));
}

Expr operator-(int a, Expr b) {
    user_assert(b.defined()) << "operator- of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return Sub::make(make_const(t, a), std::move(b));
}

Expr operator-(Expr a) {
    user_assert(a.defined()) << "operator- of undefined Expr\n";
    Type t = a.type();
    return Sub::make(make_zero(t), std::move(a));
}

Expr &operator-=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator-= of undefined Expr\n";
    Type t = a.type();
    a = Sub::make(std::move(a), cast(t, std::move(b)));
    return a;
}

Expr operator*(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator* of undefined Expr\n";
    match_types(a, b);
    return Mul::make(std::move(a), std::move(b));
}

Expr operator*(Expr a, int b) {
    user_assert(a.defined()) << "operator* of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return Mul::make(std::move(a), make_const(t, b));
}

Expr operator*(int a, Expr b) {
    user_assert(b.defined()) << "operator* of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return Mul::make(make_const(t, a), std::move(b));
}

Expr &operator*=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator*= of undefined Expr\n";
    Type t = a.type();
    a = Mul::make(std::move(a), cast(t, std::move(b)));
    return a;
}

Expr operator/(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator/ of undefined Expr\n";
    match_types(a, b);
    return Div::make(std::move(a), std::move(b));
}

Expr &operator/=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator/= of undefined Expr\n";
    Type t = a.type();
    a = Div::make(std::move(a), cast(t, std::move(b)));
    return a;
}

Expr operator/(Expr a, int b) {
    user_assert(a.defined()) << "operator/ of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return Div::make(std::move(a), make_const(t, b));
}

Expr operator/(int a, Expr b) {
    user_assert(b.defined()) << "operator- of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return Div::make(make_const(t, a), std::move(b));
}

Expr operator%(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator% of undefined Expr\n";
    match_types(a, b);
    return Mod::make(std::move(a), std::move(b));
}

Expr operator%(Expr a, int b) {
    user_assert(a.defined()) << "operator% of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return Mod::make(std::move(a), make_const(t, b));
}

Expr operator%(int a, Expr b) {
    user_assert(b.defined()) << "operator% of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return Mod::make(make_const(t, a), std::move(b));
}

Expr operator>(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator> of undefined Expr\n";
    match_types(a, b);
    return GT::make(std::move(a), std::move(b));
}

Expr operator>(Expr a, int b) {
    user_assert(a.defined()) << "operator> of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return GT::make(std::move(a), make_const(t, b));
}

Expr operator>(int a, Expr b) {
    user_assert(b.defined()) << "operator> of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return GT::make(make_const(t, a), std::move(b));
}

Expr operator<(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator< of undefined Expr\n";
    match_types(a, b);
    return LT::make(std::move(a), std::move(b));
}

Expr operator<(Expr a, int b) {
    user_assert(a.defined()) << "operator< of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return LT::make(std::move(a), make_const(t, b));
}

Expr operator<(int a, Expr b) {
    user_assert(b.defined()) << "operator< of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return LT::make(make_const(t, a), std::move(b));
}

Expr operator<=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator<= of undefined Expr\n";
    match_types(a, b);
    return LE::make(std::move(a), std::move(b));
}

Expr operator<=(Expr a, int b) {
    user_assert(a.defined()) << "operator<= of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return LE::make(std::move(a), make_const(t, b));
}

Expr operator<=(int a, Expr b) {
    user_assert(b.defined()) << "operator<= of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return LE::make(make_const(t, a), std::move(b));
}

Expr operator>=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator>= of undefined Expr\n";
    match_types(a, b);
    return GE::make(std::move(a), std::move(b));
}

Expr operator>=(const Expr &a, int b) {
    user_assert(a.defined()) << "operator>= of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return GE::make(a, make_const(t, b));
}

Expr operator>=(int a, const Expr &b) {
    user_assert(b.defined()) << "operator>= of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return GE::make(make_const(t, a), b);
}

Expr operator==(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator== of undefined Expr\n";
    match_types(a, b);
    return EQ::make(std::move(a), std::move(b));
}

Expr operator==(Expr a, int b) {
    user_assert(a.defined()) << "operator== of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return EQ::make(std::move(a), make_const(t, b));
}

Expr operator==(int a, Expr b) {
    user_assert(b.defined()) << "operator== of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return EQ::make(make_const(t, a), std::move(b));
}

Expr operator!=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator!= of undefined Expr\n";
    match_types(a, b);
    return NE::make(std::move(a), std::move(b));
}

Expr operator!=(Expr a, int b) {
    user_assert(a.defined()) << "operator!= of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return NE::make(std::move(a), make_const(t, b));
}

Expr operator!=(int a, Expr b) {
    user_assert(b.defined()) << "operator!= of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return NE::make(make_const(t, a), std::move(b));
}

Expr operator&&(Expr a, Expr b) {
    match_types(a, b);
    return And::make(std::move(a), std::move(b));
}

Expr operator&&(Expr a, bool b) {
    internal_assert(a.defined()) << "operator&& of undefined Expr\n";
    internal_assert(a.type().is_bool()) << "operator&& of Expr of type " << a.type() << "\n";
    if (b) {
        return a;
    } else {
        return make_zero(a.type());
    }
}

Expr operator&&(bool a, Expr b) {
    return std::move(b) && a;
}

Expr operator||(Expr a, Expr b) {
    match_types(a, b);
    return Or::make(std::move(a), std::move(b));
}

Expr operator||(Expr a, bool b) {
    internal_assert(a.defined()) << "operator|| of undefined Expr\n";
    internal_assert(a.type().is_bool()) << "operator|| of Expr of type " << a.type() << "\n";
    if (b) {
        return make_one(a.type());
    } else {
        return a;
    }
}

Expr operator||(bool a, Expr b) {
    return std::move(b) || a;
}

Expr operator!(Expr a) {
    return Not::make(std::move(a));
}

Expr max(Expr a, Expr b) {
    user_assert(a.defined() && b.defined())
        << "max of undefined Expr\n";
    match_types(a, b);
    return Max::make(std::move(a), std::move(b));
}

Expr max(Expr a, int b) {
    user_assert(a.defined()) << "max of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return Max::make(std::move(a), make_const(t, b));
}

Expr max(int a, Expr b) {
    user_assert(b.defined()) << "max of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return Max::make(make_const(t, a), std::move(b));
}

Expr min(Expr a, Expr b) {
    user_assert(a.defined() && b.defined())
        << "min of undefined Expr\n";
    match_types(a, b);
    return Min::make(std::move(a), std::move(b));
}

Expr min(Expr a, int b) {
    user_assert(a.defined()) << "max of undefined Expr\n";
    Type t = a.type();
    check_representable(t, b);
    return Min::make(std::move(a), make_const(t, b));
}

Expr min(int a, Expr b) {
    user_assert(b.defined()) << "max of undefined Expr\n";
    Type t = b.type();
    check_representable(t, a);
    return Min::make(make_const(t, a), std::move(b));
}

Expr cast(Type t, Expr a) {
    user_assert(a.defined()) << "cast of undefined Expr\n";
    if (a.type() == t) {
        return a;
    }

    if (t.is_handle() && !a.type().is_handle()) {
        user_error << "Can't cast \"" << a << "\" to a handle. "
                   << "The only legal cast from scalar types to a handle is: "
                   << "reinterpret(Handle(), cast<uint64_t>(" << a << "));\n";
    } else if (a.type().is_handle() && !t.is_handle()) {
        user_error << "Can't cast handle \"" << a << "\" to type " << t << ". "
                   << "The only legal cast from handles to scalar types is: "
                   << "reinterpret(UInt(64), " << a << ");\n";
    }

    // Fold constants early
    if (auto i = as_const_int(a)) {
        return make_const(t, *i);
    }
    if (auto u = as_const_uint(a)) {
        return make_const(t, *u);
    }
    if (auto f = as_const_float(a)) {
        return make_const(t, *f);
    }

    if (t.is_vector()) {
        if (a.type().is_scalar()) {
            return Broadcast::make(cast(t.element_of(), std::move(a)), t.lanes());
        } else if (const Broadcast *b = a.as<Broadcast>()) {
            if (b->lanes == t.lanes()) {
                return Broadcast::make(cast(t.element_of(), b->value), t.lanes());
            }
            // else fall thru: we could have a situation like
            //
            //   a=x3(ramp(x, y, 2))  # type=uint32x6
            //   t=uint1x6
            //
            // this should be legal to cast, but requiring b->lanes == t.lanes
            // would make it fail. Just fall through and let the Cast IR node
            // deal with possible errors. (https://github.com/halide/Halide/issues/7556)
        }
    }
    return Cast::make(t, std::move(a));
}

Expr clamp(Expr a, const Expr &min_val, const Expr &max_val) {
    user_assert(a.defined() && min_val.defined() && max_val.defined())
        << "clamp of undefined Expr\n";
    Expr n_min_val = lossless_cast(a.type(), min_val);
    user_assert(n_min_val.defined())
        << "Type mismatch in call to clamp. First argument ("
        << a << ") has type " << a.type() << ", but second argument ("
        << min_val << ") has type " << min_val.type() << ". Use an explicit cast.\n";
    Expr n_max_val = lossless_cast(a.type(), max_val);
    user_assert(n_max_val.defined())
        << "Type mismatch in call to clamp. First argument ("
        << a << ") has type " << a.type() << ", but third argument ("
        << max_val << ") has type " << max_val.type() << ". Use an explicit cast.\n";
    return Max::make(Min::make(std::move(a), std::move(n_max_val)), std::move(n_min_val));
}

Expr abs(Expr a) {
    user_assert(a.defined())
        << "abs of undefined Expr\n";
    Type t = a.type();
    if (t.is_uint()) {
        user_warning << "Warning: abs of an unsigned type is a no-op\n";
        return a;
    }
    return Call::make(t.with_code(t.is_int() ? Type::UInt : t.code()),
                      Call::abs, {std::move(a)}, Call::PureIntrinsic);
}

Expr absd(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "absd of undefined Expr\n";
    match_types(a, b);
    Type t = a.type();

    if (t.is_float()) {
        // Floats can just use abs.
        return abs(std::move(a) - std::move(b));
    }

    // The argument may be signed, but the return type is unsigned.
    return Call::make(t.with_code(t.is_int() ? Type::UInt : t.code()),
                      Call::absd, {std::move(a), std::move(b)},
                      Call::PureIntrinsic);
}

Expr sin(Expr x) {
    user_assert(x.defined()) << "sin of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "sin_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "sin_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "sin_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr asin(Expr x) {
    user_assert(x.defined()) << "asin of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "asin_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "asin_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "asin_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr cos(Expr x) {
    user_assert(x.defined()) << "cos of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "cos_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "cos_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "cos_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr acos(Expr x) {
    user_assert(x.defined()) << "acos of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "acos_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "acos_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "acos_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr tan(Expr x) {
    user_assert(x.defined()) << "tan of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "tan_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "tan_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "tan_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr atan(Expr x) {
    user_assert(x.defined()) << "atan of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "atan_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "atan_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "atan_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr atan2(Expr y, Expr x) {
    user_assert(x.defined() && y.defined()) << "atan2 of undefined Expr\n";

    if (y.type() == Float(64)) {
        x = cast<double>(x);
        return Call::make(Float(64), "atan2_f64", {std::move(y), std::move(x)}, Call::PureExtern);
    } else if (y.type() == Float(16)) {
        x = cast<float16_t>(x);
        return Call::make(Float(16), "atan2_f16", {std::move(y), std::move(x)}, Call::PureExtern);
    } else {
        y = cast<float>(y);
        x = cast<float>(x);
        return Call::make(Float(32), "atan2_f32", {std::move(y), std::move(x)}, Call::PureExtern);
    }
}

Expr sinh(Expr x) {
    user_assert(x.defined()) << "sinh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "sinh_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "sinh_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "sinh_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr asinh(Expr x) {
    user_assert(x.defined()) << "asinh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "asinh_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "asinh_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "asinh_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr cosh(Expr x) {
    user_assert(x.defined()) << "cosh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "cosh_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "cosh_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "cosh_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr acosh(Expr x) {
    user_assert(x.defined()) << "acosh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "acosh_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "acosh_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "acosh_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr tanh(Expr x) {
    user_assert(x.defined()) << "tanh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "tanh_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "tanh_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "tanh_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr atanh(Expr x) {
    user_assert(x.defined()) << "atanh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "atanh_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "atanh_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "atanh_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr sqrt(Expr x) {
    user_assert(x.defined()) << "sqrt of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "sqrt_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "sqrt_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "sqrt_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr hypot(const Expr &x, const Expr &y) {
    return sqrt(x * x + y * y);
}

Expr exp(Expr x) {
    user_assert(x.defined()) << "exp of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "exp_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "exp_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "exp_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr log(Expr x) {
    user_assert(x.defined()) << "log of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Call::make(Float(64), "log_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Call::make(Float(16), "log_f16", {std::move(x)}, Call::PureExtern);
    } else {
        return Call::make(Float(32), "log_f32", {cast<float>(std::move(x))}, Call::PureExtern);
    }
}

Expr pow(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "pow of undefined Expr\n";

    if (auto i = as_const_int(y)) {
        return raise_to_integer_power(std::move(x), *i);
    }

    if (x.type() == Float(64)) {
        y = cast<double>(std::move(y));
        return Call::make(Float(64), "pow_f64", {std::move(x), std::move(y)}, Call::PureExtern);
    } else if (x.type() == Float(16)) {
        y = cast<float16_t>(std::move(y));
        return Call::make(Float(16), "pow_f16", {std::move(x), std::move(y)}, Call::PureExtern);
    } else {
        x = cast<float>(std::move(x));
        y = cast<float>(std::move(y));
        return Call::make(Float(32), "pow_f32", {std::move(x), std::move(y)}, Call::PureExtern);
    }
}

Expr erf(const Expr &x) {
    user_assert(x.defined()) << "erf of undefined Expr\n";
    user_assert(x.type() == Float(32)) << "erf only takes float arguments\n";
    return halide_erf(x);
}

Expr fast_pow(Expr x, Expr y) {
    if (auto i = as_const_int(y)) {
        return raise_to_integer_power(std::move(x), *i);
    }

    x = cast<float>(std::move(x));
    y = cast<float>(std::move(y));
    return select(x == 0.0f, 0.0f, fast_exp(fast_log(x) * std::move(y)));
}

Expr fast_inverse(Expr x) {
    user_assert(x.defined()) << "fast_inverse of undefined Expr\n";
    Type t = x.type();
    if (t == Float(32)) {
        return Call::make(t, "fast_inverse_f32", {std::move(x)}, Call::PureExtern);
    } else if (t == Float(16)) {
        return Call::make(t, "fast_inverse_f16", {std::move(x)}, Call::PureExtern);
    } else {
        user_error << "fast_inverse only takes float16 or float32 arguments\n";
        return Expr();
    }
}

Expr fast_inverse_sqrt(Expr x) {
    user_assert(x.defined()) << "fast_inverse_sqrt of undefined Expr\n";
    Type t = x.type();
    if (t == Float(32)) {
        return Call::make(t, "fast_inverse_sqrt_f32", {std::move(x)}, Call::PureExtern);
    } else if (t == Float(16)) {
        return Call::make(t, "fast_inverse_sqrt_f16", {std::move(x)}, Call::PureExtern);
    } else {
        user_error << "fast_inverse_sqrt only takes float16 or float32 arguments\n";
        return Expr();
    }
}

Expr floor(Expr x) {
    user_assert(x.defined()) << "floor of undefined Expr\n";
    Type t = x.type();
    if (t.element_of() == Float(64)) {
        return Call::make(t, "floor_f64", {std::move(x)}, Call::PureExtern);
    } else if (t.element_of() == Float(16)) {
        return Call::make(t, "floor_f16", {std::move(x)}, Call::PureExtern);
    } else {
        t = Float(32, t.lanes());
        return Call::make(t, "floor_f32", {cast(t, std::move(x))}, Call::PureExtern);
    }
}

Expr ceil(Expr x) {
    user_assert(x.defined()) << "ceil of undefined Expr\n";
    Type t = x.type();
    if (t.element_of() == Float(64)) {
        return Call::make(t, "ceil_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type().element_of() == Float(16)) {
        return Call::make(t, "ceil_f16", {std::move(x)}, Call::PureExtern);
    } else {
        t = Float(32, t.lanes());
        return Call::make(t, "ceil_f32", {cast(t, std::move(x))}, Call::PureExtern);
    }
}

Expr round(Expr x) {
    user_assert(x.defined()) << "round of undefined Expr\n";
    Type t = x.type();
    if (!t.is_float()) {
        x = cast<float>(x);
        t = x.type();
    }
    return Call::make(t, Call::round, {std::move(x)}, Call::PureIntrinsic);
}

Expr trunc(Expr x) {
    user_assert(x.defined()) << "trunc of undefined Expr\n";
    Type t = x.type();
    if (t.element_of() == Float(64)) {
        return Call::make(t, "trunc_f64", {std::move(x)}, Call::PureExtern);
    } else if (t.element_of() == Float(16)) {
        return Call::make(t, "trunc_f16", {std::move(x)}, Call::PureExtern);
    } else {
        t = Float(32, t.lanes());
        return Call::make(t, "trunc_f32", {cast(t, std::move(x))}, Call::PureExtern);
    }
}

Expr is_nan(Expr x) {
    user_assert(x.defined()) << "is_nan of undefined Expr\n";
    user_assert(x.type().is_float()) << "is_nan only works for float";
    Type t = Bool(x.type().lanes());
    if (!is_const(x)) {
        x = strict_float(x);
    }
    if (x.type().element_of() == Float(64)) {
        return Call::make(t, "is_nan_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type().element_of() == Float(16)) {
        return Call::make(t, "is_nan_f16", {std::move(x)}, Call::PureExtern);
    } else {
        Type ft = Float(32, t.lanes());
        return Call::make(t, "is_nan_f32", {cast(ft, std::move(x))}, Call::PureExtern);
    }
}

Expr is_inf(Expr x) {
    user_assert(x.defined()) << "is_inf of undefined Expr\n";
    user_assert(x.type().is_float()) << "is_inf only works for float";
    Type t = Bool(x.type().lanes());
    if (!is_const(x)) {
        x = strict_float(x);
    }
    if (x.type().element_of() == Float(64)) {
        return Call::make(t, "is_inf_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type().element_of() == Float(16)) {
        return Call::make(t, "is_inf_f16", {std::move(x)}, Call::PureExtern);
    } else {
        Type ft = Float(32, t.lanes());
        return Call::make(t, "is_inf_f32", {cast(ft, std::move(x))}, Call::PureExtern);
    }
}

Expr is_finite(Expr x) {
    user_assert(x.defined()) << "is_finite of undefined Expr\n";
    user_assert(x.type().is_float()) << "is_finite only works for float";
    Type t = Bool(x.type().lanes());
    if (!is_const(x)) {
        x = strict_float(x);
    }
    if (x.type().element_of() == Float(64)) {
        return Call::make(t, "is_finite_f64", {std::move(x)}, Call::PureExtern);
    } else if (x.type().element_of() == Float(16)) {
        return Call::make(t, "is_finite_f16", {std::move(x)}, Call::PureExtern);
    } else {
        Type ft = Float(32, t.lanes());
        return Call::make(t, "is_finite_f32", {cast(ft, std::move(x))}, Call::PureExtern);
    }
}

Expr fract(const Expr &x) {
    user_assert(x.defined()) << "fract of undefined Expr\n";
    return x - trunc(x);
}

Expr reinterpret(Type t, Expr e) {
    return Reinterpret::make(t, std::move(e));
}

Expr operator&(Expr x, Expr y) {
    match_types_bitwise(x, y, "bitwise and");
    Type t = x.type();
    if (t.is_bool()) {
        return std::move(x) && std::move(y);
    }
    return Call::make(t, Call::bitwise_and, {std::move(x), std::move(y)}, Call::PureIntrinsic);
}

Expr operator&(Expr x, int y) {
    Type t = x.type();
    check_representable(t, y);
    if (t.is_bool()) {
        return std::move(x) && make_const(t, y);
    }
    return Call::make(t, Call::bitwise_and, {std::move(x), make_const(t, y)}, Call::PureIntrinsic);
}

Expr operator&(int x, Expr y) {
    Type t = y.type();
    check_representable(t, x);
    if (t.is_bool()) {
        return make_const(t, x) && std::move(y);
    }
    return Call::make(t, Call::bitwise_and, {make_const(t, x), std::move(y)}, Call::PureIntrinsic);
}

Expr operator|(Expr x, Expr y) {
    match_types_bitwise(x, y, "bitwise or");
    Type t = x.type();
    if (t.is_bool()) {
        return std::move(x) || std::move(y);
    }
    return Call::make(t, Call::bitwise_or, {std::move(x), std::move(y)}, Call::PureIntrinsic);
}

Expr operator|(Expr x, int y) {
    Type t = x.type();
    check_representable(t, y);
    if (t.is_bool()) {
        return std::move(x) || make_const(t, y);
    }
    return Call::make(t, Call::bitwise_or, {std::move(x), make_const(t, y)}, Call::PureIntrinsic);
}

Expr operator|(int x, Expr y) {
    Type t = y.type();
    check_representable(t, x);
    if (t.is_bool()) {
        return make_const(t, x) || std::move(y);
    }
    return Call::make(t, Call::bitwise_or, {make_const(t, x), std::move(y)}, Call::PureIntrinsic);
}

Expr operator^(Expr x, Expr y) {
    match_types_bitwise(x, y, "bitwise xor");
    Type t = x.type();
    if (t.is_bool()) {
        return std::move(x) != std::move(y);
    }
    return Call::make(t, Call::bitwise_xor, {std::move(x), std::move(y)}, Call::PureIntrinsic);
}

Expr operator^(Expr x, int y) {
    Type t = x.type();
    check_representable(t, y);
    if (t.is_bool()) {
        return std::move(x) != make_const(t, y);
    }
    return Call::make(t, Call::bitwise_xor, {std::move(x), make_const(t, y)}, Call::PureIntrinsic);
}

Expr operator^(int x, Expr y) {
    Type t = y.type();
    check_representable(t, x);
    if (t.is_bool()) {
        return make_const(t, x) != std::move(y);
    }
    return Call::make(t, Call::bitwise_xor, {make_const(t, x), std::move(y)}, Call::PureIntrinsic);
}

Expr operator~(Expr x) {
    user_assert(x.defined()) << "bitwise not of undefined Expr\n";
    user_assert(x.type().is_int() || x.type().is_uint())
        << "Argument to bitwise not must be an integer or unsigned integer";
    Type t = x.type();
    return Call::make(t, Call::bitwise_not, {std::move(x)}, Call::PureIntrinsic);
}

Expr operator<<(Expr x, Expr y) {
    match_lanes(x, y);
    match_bits(x, y);
    Type t = x.type();
    return Call::make(t, Call::shift_left, {std::move(x), std::move(y)}, Call::PureIntrinsic);
}

Expr operator<<(Expr x, int y) {
    Type t = x.type().with_code(halide_type_uint);
    if (y >= 0) {
        check_representable(t, y);
        return std::move(x) << make_const(t, y);
    } else {
        check_representable(t, -y);
        return std::move(x) >> make_const(t, -y);
    }
}

Expr operator>>(Expr x, Expr y) {
    match_lanes(x, y);
    match_bits(x, y);
    Type t = x.type();
    return Call::make(t, Call::shift_right, {std::move(x), std::move(y)}, Call::PureIntrinsic);
}

Expr operator>>(Expr x, int y) {
    Type t = x.type().with_code(halide_type_uint);
    if (y >= 0) {
        check_representable(t, y);
        return std::move(x) >> make_const(t, y);
    } else {
        check_representable(t, -y);
        return std::move(x) << make_const(t, -y);
    }
}

Expr lerp(Expr zero_val, Expr one_val, Expr weight) {
    user_assert(zero_val.defined()) << "lerp with undefined zero value";
    user_assert(one_val.defined()) << "lerp with undefined one value";
    user_assert(weight.defined()) << "lerp with undefined weight";

    // We allow integer constants through, so that you can say things
    // like lerp(0, cast<uint8_t>(x), alpha) and produce an 8-bit
    // result. Note that lerp(0.0f, cast<uint8_t>(x), alpha) will
    // produce an error, as will lerp(0.0f, cast<double>(x),
    // alpha). lerp(0, cast<float>(x), alpha) is also allowed and will
    // produce a float result.
    if (as_const_int(zero_val)) {
        zero_val = cast(one_val.type(), std::move(zero_val));
    }
    if (as_const_int(one_val)) {
        one_val = cast(zero_val.type(), std::move(one_val));
    }

    user_assert(zero_val.type() == one_val.type())
        << "Can't lerp between " << zero_val << " of type " << zero_val.type()
        << " and " << one_val << " of different type " << one_val.type() << "\n";
    user_assert((weight.type().is_uint() || weight.type().is_float()))
        << "A lerp weight must be an unsigned integer or a float, but "
        << "lerp weight " << weight << " has type " << weight.type() << ".\n";
    user_assert((zero_val.type().is_float() || zero_val.type().bits() <= 32))
        << "Lerping between 64-bit integers is not supported\n";
    // Compilation error for constant weight that is out of range for integer use
    // as this seems like an easy to catch gotcha.
    if (!zero_val.type().is_float()) {
        if (auto const_weight = as_const_float(weight)) {
            user_assert(*const_weight >= 0.0 && *const_weight <= 1.0)
                << "Floating-point weight for lerp with integer arguments is "
                << *const_weight << ", which is not in the range [0.0, 1.0].\n";
        }
    }
    Type t = zero_val.type();
    return Call::make(t, Call::lerp,
                      {std::move(zero_val), std::move(one_val), std::move(weight)},
                      Call::PureIntrinsic);
}

Expr popcount(Expr x) {
    user_assert(x.defined()) << "popcount of undefined Expr\n";
    Type t = x.type();
    user_assert(t.is_uint() || t.is_int())
        << "Argument to popcount must be an integer\n";
    return Call::make(t, Call::popcount,
                      {std::move(x)}, Call::PureIntrinsic);
}

Expr count_leading_zeros(Expr x) {
    user_assert(x.defined()) << "count leading zeros of undefined Expr\n";
    Type t = x.type();
    user_assert(t.is_uint() || t.is_int())
        << "Argument to count_leading_zeros must be an integer\n";
    return Call::make(t, Call::count_leading_zeros,
                      {std::move(x)}, Call::PureIntrinsic);
}

Expr count_trailing_zeros(Expr x) {
    user_assert(x.defined()) << "count trailing zeros of undefined Expr\n";
    Type t = x.type();
    user_assert(t.is_uint() || t.is_int())
        << "Argument to count_trailing_zeros must be an integer\n";
    return Call::make(t, Call::count_trailing_zeros,
                      {std::move(x)}, Call::PureIntrinsic);
}

Expr div_round_to_zero(Expr x, Expr y) {
    user_assert(x.defined()) << "div_round_to_zero of undefined dividend\n";
    user_assert(y.defined()) << "div_round_to_zero of undefined divisor\n";
    match_types(x, y);
    if (x.type().is_uint()) {
        return std::move(x) / std::move(y);
    }
    user_assert(x.type().is_int()) << "First argument to div_round_to_zero is not an integer: " << x << "\n";
    user_assert(y.type().is_int()) << "Second argument to div_round_to_zero is not an integer: " << y << "\n";
    Type t = x.type();
    return Call::make(t, Call::div_round_to_zero,
                      {std::move(x), std::move(y)},
                      Call::PureIntrinsic);
}

Expr mod_round_to_zero(Expr x, Expr y) {
    user_assert(x.defined()) << "mod_round_to_zero of undefined dividend\n";
    user_assert(y.defined()) << "mod_round_to_zero of undefined divisor\n";
    match_types(x, y);
    if (x.type().is_uint()) {
        return std::move(x) % std::move(y);
    }
    user_assert(x.type().is_int()) << "First argument to mod_round_to_zero is not an integer: " << x << "\n";
    user_assert(y.type().is_int()) << "Second argument to mod_round_to_zero is not an integer: " << y << "\n";
    Type t = x.type();
    return Call::make(t, Call::mod_round_to_zero,
                      {std::move(x), std::move(y)},
                      Call::PureIntrinsic);
}

namespace {

std::atomic<int> random_number_counter = 0;

}  // namespace

namespace Internal {

void reset_random_counters() {
    random_number_counter = 0;
    random_variable_counter = 0;
}

}  // namespace Internal

Expr random_float(Expr seed) {
    const int id = random_number_counter++;

    std::vector<Expr> args;
    if (seed.defined()) {
        user_assert(seed.type() == Int(32))
            << "The seed passed to random_float must have type Int(32), but instead is "
            << seed << " of type " << seed.type() << "\n";
        args.push_back(std::move(seed));
    }
    args.emplace_back(id);

    // This is (surprisingly) pure - it's a fixed psuedo-random
    // function of its inputs.
    return Call::make(Float(32), Call::random,
                      args, Call::PureIntrinsic);
}

Expr random_uint(Expr seed) {
    const int id = random_number_counter++;

    std::vector<Expr> args;
    if (seed.defined()) {
        user_assert(seed.type() == Int(32) || seed.type() == UInt(32))
            << "The seed passed to random_int must have type Int(32) or UInt(32), but instead is "
            << seed << " of type " << seed.type() << "\n";
        args.push_back(std::move(seed));
    }
    args.emplace_back(id);

    return Call::make(UInt(32), Call::random,
                      args, Call::PureIntrinsic);
}

Expr random_int(Expr seed) {
    return reinterpret<int32_t>(random_uint(std::move(seed)));
}

Expr likely(Expr e) {
    Type t = e.type();
    return Call::make(t, Call::likely,
                      {std::move(e)}, Call::PureIntrinsic);
}

Expr likely_if_innermost(Expr e) {
    Type t = e.type();
    return Call::make(t, Call::likely_if_innermost,
                      {std::move(e)}, Call::PureIntrinsic);
}

Expr strict_float(const Expr &e) {
    return strictify_float(e);
}

Expr undef(Type t) {
    return Call::make(t, Call::undef,
                      std::vector<Expr>(),
                      Call::Intrinsic);
}

namespace Internal {
Expr unreachable(Type t) {
    return Call::make(t, Call::unreachable,
                      std::vector<Expr>(),
                      Call::Intrinsic);
}
}  // namespace Internal

namespace {
Expr make_scatter_gather(const std::vector<Expr> &args) {
    // There's currently no difference in the IR between a gather and
    // a scatter. They're distinct just to make code more readable.
    return Call::make(args[0].type(), Call::scatter_gather, args, Call::Intrinsic);
}
}  // namespace

Expr scatter(const std::vector<Expr> &args) {
    return make_scatter_gather(args);
}

Expr gather(const std::vector<Expr> &args) {
    return make_scatter_gather(args);
}

Expr extract_bits(Type t, const Expr &e, const Expr &lsb) {
    return Call::make(t, Call::extract_bits, {e, lsb}, Call::Intrinsic);
}

Expr concat_bits(const std::vector<Expr> &e) {
    user_assert(!e.empty()) << "concat_bits requires at least one argument\n";
    user_assert((e.size() & (e.size() - 1)) == 0) << "concat_bits received " << e.size() << " arguments, which is not a power of two.\n";
    Type t = e[0].type();
    for (size_t i = 1; i < e.size(); i++) {
        user_assert(e[i].type() == t) << "All arguments to concat_bits must have the same type\n";
    }
    return Call::make(t.with_bits(t.bits() * (int)e.size()), Call::concat_bits, e, Call::Intrinsic);
}

Expr target_arch_is(Target::Arch arch) {
    return Call::make(Bool(), Call::target_arch_is, {Expr((int)arch)}, Call::PureIntrinsic);
}

Expr target_os_is(Target::OS os) {
    return Call::make(Bool(), Call::target_os_is, {Expr((int)os)}, Call::PureIntrinsic);
}

Expr target_bits() {
    return Call::make(Int(32), Call::target_bits, {}, Call::PureIntrinsic);
}

Expr target_has_feature(Target::Feature feat) {
    return Call::make(Bool(), Call::target_has_feature, {Expr((int)feat)}, Call::PureIntrinsic);
}

Expr target_natural_vector_size(Type t) {
    return Call::make(Int(32), Call::target_natural_vector_size, {make_zero(t.element_of())}, Call::PureIntrinsic);
}

}  // namespace Halide
