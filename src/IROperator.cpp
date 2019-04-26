#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

#include "CSE.h"
#include "Debug.h"
#include "IREquality.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Var.h"

namespace Halide {

// Evaluate a float polynomial efficiently, taking instruction latency
// into account. The high order terms come first. n is the number of
// terms, which is the degree plus one.
namespace {
Expr evaluate_polynomial(const Expr &x, float *coeff, int n) {
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
    if (!s.defined()) return true;
    const Evaluate *e = s.as<Evaluate>();
    return e && is_const(e->value);
}

class ExprIsPure : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) override {
        if (!op->is_pure()) {
            result = false;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Div *op) override {
        if (!op->type.is_float() && (!is_const(op->b) || is_zero(op->b))) {
            // Division by zero is a side-effect
            result = false;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Mod *op) override {
        if (!op->type.is_float() && (!is_const(op->b) || is_zero(op->b))) {
            // Mod by zero is a side-effect
            result = false;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) override {
        if (!op->image.defined() && !op->param.defined()) {
            // It's a load from an internal buffer, which could
            // mutate.
            result = false;
        } else {
            IRVisitor::visit(op);
        }
    }
public:
    bool result = true;
};

bool is_pure(const Expr &e) {
    ExprIsPure pure;
    e.accept(&pure);
    return pure.result;
}

const int64_t *as_const_int(const Expr &e) {
    if (!e.defined()) {
        return nullptr;
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return as_const_int(b->value);
    } else if (const IntImm *i = e.as<IntImm>()) {
        return &(i->value);
    } else {
        return nullptr;
    }
}

const uint64_t *as_const_uint(const Expr &e) {
    if (!e.defined()) {
        return nullptr;
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return as_const_uint(b->value);
    } else if (const UIntImm *i = e.as<UIntImm>()) {
        return &(i->value);
    } else {
        return nullptr;
    }
}

const double *as_const_float(const Expr &e) {
    if (!e.defined()) {
        return nullptr;
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return as_const_float(b->value);
    } else if (const FloatImm *f = e.as<FloatImm>()) {
        return &(f->value);
    } else {
        return nullptr;
    }
}

bool is_const_power_of_two_integer(const Expr &e, int *bits) {
    if (!(e.type().is_int() || e.type().is_uint())) return false;

    const Broadcast *b = e.as<Broadcast>();
    if (b) return is_const_power_of_two_integer(b->value, bits);

    const Cast *c = e.as<Cast>();
    if (c) return is_const_power_of_two_integer(c->value, bits);

    uint64_t val = 0;

    if (const int64_t *i = as_const_int(e)) {
        if (*i < 0) return false;
        val = (uint64_t)(*i);
    } else if (const uint64_t *u = as_const_uint(e)) {
        val = *u;
    }

    if (val && ((val & (val - 1)) == 0)) {
        *bits = 0;
        for (; val; val >>= 1) {
            if (val == 1) return true;
            (*bits)++;
        }
    }

    return false;
}

bool is_positive_const(const Expr &e) {
    if (const IntImm *i = e.as<IntImm>()) return i->value > 0;
    if (const UIntImm *u = e.as<UIntImm>()) return u->value > 0;
    if (const FloatImm *f = e.as<FloatImm>()) return f->value > 0.0f;
    if (const Cast *c = e.as<Cast>()) {
        return is_positive_const(c->value);
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
    if (const IntImm *i = e.as<IntImm>()) return i->value < 0;
    if (const FloatImm *f = e.as<FloatImm>()) return f->value < 0.0f;
    if (const Cast *c = e.as<Cast>()) {
        return is_negative_const(c->value);
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

bool is_negative_negatable_const(const Expr &e, Type T) {
    if (const IntImm *i = e.as<IntImm>()) {
        return (i->value < 0 && !T.is_min(i->value));
    }
    if (const FloatImm *f = e.as<FloatImm>()) return f->value < 0.0f;
    if (const Cast *c = e.as<Cast>()) {
        return is_negative_negatable_const(c->value, c->type);
    }
    if (const Ramp *r = e.as<Ramp>()) {
        // slightly conservative
        return is_negative_negatable_const(r->base) && is_negative_const(r->stride);
    }
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_negative_negatable_const(b->value);
    }
    return false;
}

bool is_negative_negatable_const(const Expr &e) {
    return is_negative_negatable_const(e, e.type());
}

bool is_undef(const Expr &e) {
    if (const Call *c = e.as<Call>()) return c->is_intrinsic(Call::undef);
    return false;
}

bool is_zero(const Expr &e) {
    if (const IntImm *int_imm = e.as<IntImm>()) return int_imm->value == 0;
    if (const UIntImm *uint_imm = e.as<UIntImm>()) return uint_imm->value == 0;
    if (const FloatImm *float_imm = e.as<FloatImm>()) return float_imm->value == 0.0;
    if (const Cast *c = e.as<Cast>()) return is_zero(c->value);
    if (const Broadcast *b = e.as<Broadcast>()) return is_zero(b->value);
    if (const Call *c = e.as<Call>()) {
        return (c->is_intrinsic(Call::bool_to_mask) || c->is_intrinsic(Call::cast_mask)) &&
               is_zero(c->args[0]);
    }
    return false;
}

bool is_one(const Expr &e) {
    if (const IntImm *int_imm = e.as<IntImm>()) return int_imm->value == 1;
    if (const UIntImm *uint_imm = e.as<UIntImm>()) return uint_imm->value == 1;
    if (const FloatImm *float_imm = e.as<FloatImm>()) return float_imm->value == 1.0;
    if (const Cast *c = e.as<Cast>()) return is_one(c->value);
    if (const Broadcast *b = e.as<Broadcast>()) return is_one(b->value);
    if (const Call *c = e.as<Call>()) {
        return (c->is_intrinsic(Call::bool_to_mask) || c->is_intrinsic(Call::cast_mask)) &&
               is_one(c->args[0]);
    }
    return false;
}

bool is_two(const Expr &e) {
    if (e.type().bits() < 2) return false;
    if (const IntImm *int_imm = e.as<IntImm>()) return int_imm->value == 2;
    if (const UIntImm *uint_imm = e.as<UIntImm>()) return uint_imm->value == 2;
    if (const FloatImm *float_imm = e.as<FloatImm>()) return float_imm->value == 2.0;
    if (const Cast *c = e.as<Cast>()) return is_two(c->value);
    if (const Broadcast *b = e.as<Broadcast>()) return is_two(b->value);
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

Expr make_indeterminate_expression(Type type) {
    static std::atomic<int> counter;
    return Call::make(type, Call::indeterminate_expression, {counter++}, Call::Intrinsic);
}

Expr make_signed_integer_overflow(Type type) {
    static std::atomic<int> counter;
    return Call::make(type, Call::signed_integer_overflow, {counter++}, Call::Intrinsic);
}

Expr const_true(int w) {
    return make_one(UInt(1, w));
}

Expr const_false(int w) {
    return make_zero(UInt(1, w));
}

Expr lossless_cast(Type t, Expr e) {
    if (t == e.type()) {
        return e;
    } else if (t.can_represent(e.type())) {
        return cast(t, std::move(e));
    }

    if (const Cast *c = e.as<Cast>()) {
        if (t.can_represent(c->value.type())) {
            // We can recurse into widening casts.
            return lossless_cast(t, c->value);
        } else {
            return Expr();
        }
    }

    if (const Broadcast *b = e.as<Broadcast>()) {
        Expr v = lossless_cast(t.element_of(), b->value);
        if (v.defined()) {
            return Broadcast::make(v, b->lanes);
        } else {
            return Expr();
        }
    }

    if (const IntImm *i = e.as<IntImm>()) {
        if (t.can_represent(i->value)) {
            return make_const(t, i->value);
        } else {
            return Expr();
        }
    }

    if (const UIntImm *i = e.as<UIntImm>()) {
        if (t.can_represent(i->value)) {
            return make_const(t, i->value);
        } else {
            return Expr();
        }
    }

    if (const FloatImm *f = e.as<FloatImm>()) {
        if (t.can_represent(f->value)) {
            return make_const(t, f->value);
        } else {
            return Expr();
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

void match_types(Expr &a, Expr &b) {
    if (a.type() == b.type()) return;

    user_assert(!a.type().is_handle() && !b.type().is_handle())
        << "Can't do arithmetic on opaque pointer types: "
        << a << ", " << b << "\n";

    // Broadcast scalar to match vector
    if (a.type().is_scalar() && b.type().is_vector()) {
        a = Broadcast::make(std::move(a), b.type().lanes());
    } else if (a.type().is_vector() && b.type().is_scalar()) {
        b = Broadcast::make(std::move(b), a.type().lanes());
    } else {
        internal_assert(a.type().lanes() == b.type().lanes()) << "Can't match types of differing widths";
    }

    Type ta = a.type(), tb = b.type();

    // If type broadcasting has made the types match no additional casts are needed
    if (ta == tb) return;

    if (!ta.is_float() && tb.is_float()) {
        // int(a) * float(b) -> float(b)
        // uint(a) * float(b) -> float(b)
        a = cast(tb, std::move(a));
    } else if (ta.is_float() && !tb.is_float()) {
        b = cast(ta, std::move(b));
    } else if (ta.is_float() && tb.is_float()) {
        // float(a) * float(b) -> float(max(a, b))
        if (ta.bits() > tb.bits()) b = cast(ta, std::move(b));
        else a = cast(tb, std::move(a));
    } else if (ta.is_uint() && tb.is_uint()) {
        // uint(a) * uint(b) -> uint(max(a, b))
        if (ta.bits() > tb.bits()) b = cast(ta, std::move(b));
        else a = cast(tb, std::move(a));
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

    // Give explicit type to immediate constants in expression.
    const IntImm *x_int_imm = x.as<IntImm>();
    const IntImm *y_int_imm = y.as<IntImm>();
    const UIntImm *x_uint_imm = x.as<UIntImm>();
    const UIntImm *y_uint_imm = y.as<UIntImm>();
    bool x_is_imm = x_int_imm || x_uint_imm;
    bool y_is_imm = y_int_imm || y_uint_imm;
    if (x_is_imm && !y_is_imm) {
        x = cast(y.type().element_of(), x);
    } else if (!x_is_imm && y_is_imm) {
        y = cast(x.type().element_of(), y);
    }

    user_assert(y.type().is_int() == x.type().is_int()) << "Arguments to " << op_name
      << " must be both be signed or both be unsigned.\n";

    // Broadcast scalar to match vector
    if (x.type().is_scalar() && y.type().is_vector()) {
        x = Broadcast::make(std::move(x), y.type().lanes());
    } else if (x.type().is_vector() && y.type().is_scalar()) {
        y = Broadcast::make(std::move(y), x.type().lanes());
    } else {
        internal_assert(x.type().lanes() == y.type().lanes()) << "Can't match types of differing widths";
    }

    // Widen or narrow, then bitcast.
    if (y.type().bits() != x.type().bits()) {
        y = cast(y.type().with_bits(x.type().bits()), y);
    }
    if (y.type() != x.type()) {
        y = reinterpret(x.type(), y);
    }
}

// Fast math ops based on those from Syrah (http://github.com/boulos/syrah). Thanks, Solomon!

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

Expr halide_log(Expr x_full) {
    Type type = x_full.type();
    internal_assert(type.element_of() == Float(32));

    Expr nan = Call::make(type, "nan_f32", {}, Call::PureExtern);
    Expr neg_inf = Call::make(type, "neg_inf_f32", {}, Call::PureExtern);

    Expr use_nan = x_full < 0.0f; // log of a negative returns nan
    Expr use_neg_inf = x_full == 0.0f; // log of zero is -inf
    Expr exceptional = use_nan | use_neg_inf;

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
    Expr result = evaluate_polynomial(x1, coeff, sizeof(coeff)/sizeof(coeff[0]));

    result += cast(type, exponent) * logf(2.0);

    result = select(exceptional, select(use_nan, nan, neg_inf), result);

    // This introduces lots of common subexpressions
    result = common_subexpression_elimination(result);

    return result;
}

Expr halide_exp(Expr x_full) {
    Type type = x_full.type();
    internal_assert(type.element_of() == Float(32));

    float ln2_part1 = 0.6931457519f;
    float ln2_part2 = 1.4286067653e-6f;
    float one_over_ln2 = 1.0f/logf(2.0f);

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
    Expr result = evaluate_polynomial(x, coeff, sizeof(coeff)/sizeof(coeff[0]));

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

Expr halide_erf(Expr x_full) {
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
    Expr approx1 = evaluate_polynomial(x, c1, sizeof(c1)/sizeof(c1[0]));

    approx1 = 1.0f - pow(approx1, -16);

    // An odd polynomial tuned for values < 1. Similar to the Taylor
    // expansion of erf.
    float c2[] = {-0.0005553339f,
                  0.0048937243f,
                  -0.0266849239f,
                  0.1127890132f,
                  -0.3761207240f,
                  1.1283789803f};

    Expr approx2 = evaluate_polynomial(x*x, c2, sizeof(c2)/sizeof(c2[0]));
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
            result = y*y*std::move(e);
        } else {
            e = raise_to_integer_power(std::move(e), p >> 1);
            result = e*e;
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
    } else if (!is_one(cond)) {
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
            shape.push_back(0);
        }
        if (i < extents.size()) {
            shape.push_back(extents[i]);
        } else {
            shape.push_back(0);
        }
        if (i < strides.size()) {
            shape.push_back(strides[i]);
        } else {
            shape.push_back(0);
        }
        // per-dimension flags, currently unused.
        shape.push_back(0);
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

Expr strided_ramp_base(Expr e, int stride) {
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

} // namespace Internal

Expr fast_log(Expr x) {
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

    Expr result = evaluate_polynomial(x1, coeff, sizeof(coeff)/sizeof(coeff[0]));
    result = result + cast<float>(exponent) * logf(2);
    result = common_subexpression_elimination(result);
    return result;
}

Expr fast_exp(Expr x_full) {
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
    Expr result = evaluate_polynomial(x, coeff, sizeof(coeff)/sizeof(coeff[0]));

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

Expr stringify(const std::vector<Expr> &args) {
    if (args.empty()) {
        return Expr("");
    }

    return Internal::Call::make(type_of<const char *>(), Internal::Call::stringify,
                                args, Internal::Call::Intrinsic);
}

Expr combine_strings(const std::vector<Expr> &args) {
    // Insert spaces between each expr.
    std::vector<Expr> strings(args.size()*2);
    for (size_t i = 0; i < args.size(); i++) {
        strings[i*2] = args[i];
        if (i < args.size() - 1) {
            strings[i*2+1] = Expr(" ");
        } else {
            strings[i*2+1] = Expr("\n");
        }
    }

    return stringify(strings);
}

Expr print(const std::vector<Expr> &args) {
    Expr combined_string = combine_strings(args);

    // Call halide_print.
    Expr print_call =
        Internal::Call::make(Int(32), "halide_print",
                             {combined_string}, Internal::Call::Extern);

    // Return the first argument.
    Expr result =
        Internal::Call::make(args[0].type(), Internal::Call::return_second,
                             {print_call, args[0]}, Internal::Call::PureIntrinsic);
    return result;
}

Expr print_when(Expr condition, const std::vector<Expr> &args) {
    Expr p = print(args);
    return Internal::Call::make(p.type(),
                                Internal::Call::if_then_else,
                                {std::move(condition), p, args[0]},
                                Internal::Call::PureIntrinsic);
}

namespace Internal {
Expr requirement_failed_error(Expr condition, const std::vector<Expr> &args) {
    return Internal::Call::make(Int(32),
                                "halide_error_requirement_failed",
                                {stringify({condition}), combine_strings(args)},
                                Internal::Call::Extern);
}
}

Expr require(Expr condition, const std::vector<Expr> &args) {
    user_assert(condition.defined()) << "Require of undefined condition.\n";
    user_assert(condition.type().is_bool()) << "Require condition must be a boolean type.\n";
    user_assert(args.at(0).defined()) << "Require of undefined value.\n";

    Expr err = requirement_failed_error(condition, args);

    return Internal::Call::make(args[0].type(),
                                Internal::Call::require,
                                {likely(std::move(condition)), args[0], std::move(err)},
                                Internal::Call::PureIntrinsic);
}

namespace Internal {

Expr memoize_tag_helper(Expr result, const std::vector<Expr> &cache_key_values) {
    Type t = result.type();
    std::vector<Expr> args;
    args.push_back(std::move(result));
    args.insert(args.end(), cache_key_values.begin(), cache_key_values.end());
    return Internal::Call::make(t, Internal::Call::memoize_expr,
                                args, Internal::Call::PureIntrinsic);
}

}  // namespace Internal

Expr saturating_cast(Type t, Expr e) {
    // For float to float, guarantee infinities are always pinned to range.
    if (t.is_float() && e.type().is_float()) {
        if (t.bits() < e.type().bits()) {
            e = cast(t, clamp(std::move(e), t.min(), t.max()));
        } else {
            e = clamp(cast(t, std::move(e)), t.min(), t.max());
        }
    } else if (e.type() != t) {
        // Limits for Int(2^n) or UInt(2^n) are not exactly representable in Float(2^n)
        if (e.type().is_float() && !t.is_float() && t.bits() >= e.type().bits()) {
            e = max(std::move(e), t.min()); // min values turn out to be always representable

            // This line depends on t.max() rounding upward, which should always
            // be the case as it is one less than a representable value, thus
            // the one larger is always the closest.
            e = select(e >= cast(e.type(), t.max()), t.max(), cast(t, e));
        } else {
            Expr min_bound;
            if (!e.type().is_uint()) {
                min_bound = lossless_cast(e.type(), t.min());
            }
            Expr max_bound = lossless_cast(e.type(), t.max());

            if (min_bound.defined() && max_bound.defined()) {
                e = clamp(std::move(e), min_bound, max_bound);
            } else if (min_bound.defined()) {
                e = max(std::move(e), min_bound);
            } else if (max_bound.defined()) {
                e = min(std::move(e), max_bound);
            }
            e = cast(t, std::move(e));
        }
    }
    return e;
}

Expr select(Expr condition, Expr true_value, Expr false_value) {

    if (as_const_int(condition)) {
        // Why are you doing this? We'll preserve the select node until constant folding for you.
        condition = cast(Bool(), std::move(condition));
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

    return Internal::Select::make(std::move(condition), std::move(true_value), std::move(false_value));
}

Tuple tuple_select(const Tuple &condition, const Tuple &true_value, const Tuple &false_value) {
    user_assert(condition.size() == true_value.size() && true_value.size() == false_value.size())
        << "tuple_select() requires all Tuples to have identical sizes.";
    Tuple result(std::vector<Expr>(condition.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = select(condition[i], true_value[i], false_value[i]);
    }
    return result;
}

Tuple tuple_select(const Expr &condition, const Tuple &true_value, const Tuple &false_value) {
    user_assert(true_value.size() == false_value.size())
        << "tuple_select() requires all Tuples to have identical sizes.";
    Tuple result(std::vector<Expr>(true_value.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = select(condition, true_value[i], false_value[i]);
    }
    return result;
}

Expr unsafe_promise_clamped(Expr value, Expr min, Expr max) {
    user_assert(value.defined()) << "unsafe_promise_clamped with undefined value.\n";
    Expr n_min_val = min.defined() ? lossless_cast(value.type(), min) : value.type().min();
    Expr n_max_val = max.defined() ? lossless_cast(value.type(), max) : value.type().max();

    // Min and max are allowed to be undefined with the meaning of no bound on that side.

    return Internal::Call::make(value.type(),
                                Internal::Call::unsafe_promise_clamped,
                                {value, n_min_val, n_max_val},
                                Internal::Call::Intrinsic);
}

}  // namespace Halide
