#include <iostream>
#include <sstream>
#include <math.h>
#include <algorithm>

#include "IROperator.h"
#include "IRPrinter.h"
#include "Simplify.h"
#include "Debug.h"
#include "CSE.h"

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
        return even_terms * x + odd_terms;
    } else {
        return odd_terms * x + even_terms;
    }
}
}

namespace Internal {

bool is_const(Expr e) {
    if (e.as<IntImm>()) return true;
    if (e.as<FloatImm>()) return true;
    if (e.as<StringImm>()) return true;
    if (const Cast *c = e.as<Cast>()) {
        return is_const(c->value);
    }
    if (const Ramp *r = e.as<Ramp>()) {
        return is_const(r->base) && is_const(r->stride);
    }
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_const(b->value);
    }
    return false;

}

bool is_const(Expr e, int value) {
    if (const IntImm *i = e.as<IntImm>()) return i->value == value;
    if (const FloatImm *i = e.as<FloatImm>()) return i->value == value;
    if (const Cast *c = e.as<Cast>()) return is_const(c->value, value);
    if (const Broadcast *b = e.as<Broadcast>()) return is_const(b->value, value);
    return false;
}

bool is_no_op(Stmt s) {
    if (!s.defined()) return true;
    const Evaluate *e = s.as<Evaluate>();
    return e && is_const(e->value);
}

const int * as_const_int(Expr e) {
    const IntImm *i = e.as<IntImm>();
    return i ? &(i->value) : NULL;
}

const float * as_const_float(Expr e) {
    const FloatImm *f = e.as<FloatImm>();
    return f ? &(f->value) : NULL;
}

bool is_const_power_of_two(Expr e, int *bits) {
    const Broadcast *b = e.as<Broadcast>();
    if (b) return is_const_power_of_two(b->value, bits);

    const Cast *c = e.as<Cast>();
    if (c) return is_const_power_of_two(c->value, bits);

    const IntImm *int_imm = e.as<IntImm>();
    if (int_imm && ((int_imm->value & (int_imm->value - 1)) == 0)) {
        int bit_count = 0;
        int tmp;
        for (tmp = 1; tmp < int_imm->value; tmp *= 2) {
            bit_count++;
        }
        if (tmp == int_imm->value) {
            *bits = bit_count;
            return true;
        }
    }
    return false;
}

bool is_positive_const(Expr e) {
    if (const IntImm *i = e.as<IntImm>()) return i->value > 0;
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

bool is_negative_const(Expr e) {
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

bool is_negative_negatable_const(Expr e, Type T) {
    if (const IntImm *i = e.as<IntImm>()) {
        return i->value < 0 &&
               i->value != T.imin();
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

bool is_negative_negatable_const(Expr e) {
    return is_negative_negatable_const(e, e.type());
}

bool is_zero(Expr e) {
    if (const IntImm *int_imm = e.as<IntImm>()) return int_imm->value == 0;
    if (const FloatImm *float_imm = e.as<FloatImm>()) return float_imm->value == 0.0f;
    if (const Cast *c = e.as<Cast>()) return is_zero(c->value);
    if (const Broadcast *b = e.as<Broadcast>()) return is_zero(b->value);
    return false;
}

bool is_one(Expr e) {
    if (const IntImm *int_imm = e.as<IntImm>()) return int_imm->value == 1;
    if (const FloatImm *float_imm = e.as<FloatImm>()) return float_imm->value == 1.0f;
    if (const Cast *c = e.as<Cast>()) return is_one(c->value);
    if (const Broadcast *b = e.as<Broadcast>()) return is_one(b->value);
    return false;
}

bool is_two(Expr e) {
    if (const IntImm *int_imm = e.as<IntImm>()) return int_imm->value == 2;
    if (const FloatImm *float_imm = e.as<FloatImm>()) return float_imm->value == 2.0f;
    if (const Cast *c = e.as<Cast>()) return is_two(c->value);
    if (const Broadcast *b = e.as<Broadcast>()) return is_two(b->value);
    return false;
}

int int_cast_constant(Type t, int val) {
    // Unsigned of less than 32 bits is masked to select the appropriate bits
    if (t.is_uint()) {
        if (t.bits < 32) {
            val = val & ((((unsigned int) 1) << t.bits) - 1);
        }
    }
    else if (t.is_int()) {
        if (t.bits < 32) {
            // sign extend the lower bits
            val = ((val << (32 - t.bits)) >> (32 - t.bits));
        }
    }
    else {
        internal_error << "Cast of integer to non-integer not available here";
    }
    return val;
}

Expr make_const(Type t, int val) {
    if (t == Int(32)) return val;
    if (t == Float(32)) return (float)val;
    if (t.is_vector()) {
        return Broadcast::make(make_const(t.element_of(), val), t.width);
    }
    // When constructing cast integer constants, use the canonical representation.
    if (t.is_int() || t.is_uint()) {
        val = int_cast_constant(t, val);
    }
    return Cast::make(t, val);
}

Expr make_bool(bool val, int w) {
    return make_const(UInt(1, w), val);
}

Expr make_zero(Type t) {
    return make_const(t, 0);
}

Expr make_one(Type t) {
    return make_const(t, 1);
}

Expr make_two(Type t) {
    return make_const(t, 2);
}

Expr const_true(int w) {
    return make_one(UInt(1, w));
}

Expr const_false(int w) {
    return make_zero(UInt(1, w));
}


void check_representable(Type t, int x) {
    int result = int_cast_constant(t, x);
    user_assert(result == x)
        << "Integer constant " << x
        << " would be converted to " << result
        << " because it will be implicitly coerced to type " << t << "\n";
}

void match_types(Expr &a, Expr &b) {
    user_assert(!a.type().is_handle() && !b.type().is_handle())
        << "Can't do arithmetic on opaque pointer types\n";

    if (a.type() == b.type()) return;

    const int *a_int_imm = as_const_int(a);
    const int *b_int_imm = as_const_int(b);

    // First widen to match
    if (a.type().is_scalar() && b.type().is_vector()) {
        a = Broadcast::make(a, b.type().width);
    } else if (a.type().is_vector() && b.type().is_scalar()) {
        b = Broadcast::make(b, a.type().width);
    } else {
        internal_assert(a.type().width == b.type().width) << "Can't match types of differing widths";
    }

    Type ta = a.type(), tb = b.type();

    if (!ta.is_float() && tb.is_float()) {
        // int(a) * float(b) -> float(b)
        // uint(a) * float(b) -> float(b)
        a = cast(tb, a);
    } else if (ta.is_float() && !tb.is_float()) {
        b = cast(ta, b);
    } else if (ta.is_float() && tb.is_float()) {
        // float(a) * float(b) -> float(max(a, b))
        if (ta.bits > tb.bits) b = cast(ta, b);
        else a = cast(tb, a);
    } else if (!ta.is_float() && b_int_imm) {
        // (u)int(a) * IntImm(b) -> (u)int(a)
        check_representable(ta, *b_int_imm);
        b = cast(ta, b);
    } else if (!tb.is_float() && a_int_imm) {
        check_representable(tb, *a_int_imm);
        a = cast(tb, a);
    } else if (ta.is_uint() && tb.is_uint()) {
        // uint(a) * uint(b) -> uint(max(a, b))
        if (ta.bits > tb.bits) b = cast(ta, b);
        else a = cast(tb, a);
    } else if (!ta.is_float() && !tb.is_float()) {
        // int(a) * (u)int(b) -> int(max(a, b))
        int bits = std::max(ta.bits, tb.bits);
        a = cast(Int(bits), a);
        b = cast(Int(bits), b);
    } else {
        internal_error << "Could not match types: " << ta << ", " << tb << "\n";
    }
}

// Fast math ops based on those from Syrah (http://github.com/boulos/syrah). Thanks, Solomon!

// Factor a float into 2^exponent * reduced, where reduced is between 0.75 and 1.5
void range_reduce_log(Expr input, Expr *reduced, Expr *exponent) {
    Type type = input.type();
    Type int_type = Int(32, type.width);
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

    /*
    // Floats represent exponents using 8 bits, which encode the range
    // from [-127, 128]. Zero maps to 127.

    // The reduced version is between 0.5 and 1.0, which means it has
    // an exponent of -1. Floats encode this as 126.
    int exponent_neg1 = 126 << 23;

    // Grab the exponent bits from the input. We know the sign bit is
    // zero because we're taking a log, and negative inputs are
    // handled elsewhere.
    Expr biased_exponent = int_version >> 23;

    // Add one, to account for the fact that the reduced version has
    // an exponent of -1.
    Expr offset_exponent = biased_exponent + 1;
    *exponent = offset_exponent - 127;

    // Blend the offset_exponent with the original input.
    Expr blended = (int_version & non_exponent_mask) | (exponent_neg1);
    */
}

Expr halide_log(Expr x_full) {
    Type type = x_full.type();
    internal_assert(type.element_of() == Float(32));

    Expr nan = Call::make(type, "nan_f32", std::vector<Expr>(), Call::Extern);
    Expr neg_inf = Call::make(type, "neg_inf_f32", std::vector<Expr>(), Call::Extern);

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
    Expr k = cast(Int(32, type.width), k_real);

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

    Expr inf = Call::make(type, "inf_f32", std::vector<Expr>(), Call::Extern);

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

Expr raise_to_integer_power(Expr e, int p) {
    Expr result;
    if (p == 0) {
        result = make_one(e.type());
    } else if (p == 1) {
        result = e;
    } else if (p < 0) {
        result = make_one(e.type())/raise_to_integer_power(e, -p);
    } else {
        // p is at least 2
        Expr y = raise_to_integer_power(e, p>>1);
        if (p & 1) result = y*y*e;
        else result = y*y;
    }
    return result;
}

Expr int64_to_immediate_expr(int64_t i) {
    Expr lo = Cast::make(Int(64), Cast::make(UInt(32), Expr((int32_t) i)));
    Expr hi = Cast::make(Int(64), Cast::make(Int(32), Expr((int32_t) (i >> 32))));
    return Cast::make(Int(64), simplify((hi << Expr(32)) | lo));
}

bool int64_from_immediate_expr(Expr e, int64_t* value) {
    // This is a deliberately simple parser that is only meant to
    // decode Exprs produced by int64_to_immediate_expr() [or likely
    // simplifications thereof]. Caveat emptor.
    if (!e.defined() || e.type() != type_of<int64_t>()) {
        return false;
    }
    if (const Cast* c = e.as<Cast>()) {
        // We already checked the type above, so we don't need to check it again
        e = c->value;
    }
    // Lo 32 bits are unsigned (to avoid sign extension)
    uint32_t u32;
    if (scalar_from_constant_expr<uint32_t>(e, &u32)) {
        *value = static_cast<int64_t>(u32);
        return true;
    }
    int32_t i32;
    if (scalar_from_constant_expr<int32_t>(e, &i32)) {
        *value = static_cast<int64_t>(i32);
        return true;
    }
    if (const Call* call = e.as<Call>()) {
        if (call->name == Call::shift_left) {
            int64_t lhs, rhs;
            if (!int64_from_immediate_expr(call->args[0], &lhs) ||
                !int64_from_immediate_expr(call->args[1], &rhs)) {
                return false;
            }
            *value = (lhs << rhs);
            return true;
        }
        if (call->name == Call::bitwise_or) {
            int64_t lhs, rhs;
            if (!int64_from_immediate_expr(call->args[0], &lhs) ||
                !int64_from_immediate_expr(call->args[1], &rhs)) {
                return false;
            }
            *value = (lhs | rhs);
            return true;
        }
    }
    return false;
}

}

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

Expr print(const std::vector<Expr> &args) {
    // Insert spaces between each expr.
    std::vector<Expr> print_args(args.size()*2);
    for (size_t i = 0; i < args.size(); i++) {
        print_args[i*2] = args[i];
        if (i < args.size() - 1) {
            print_args[i*2+1] = Expr(" ");
        } else {
            print_args[i*2+1] = Expr("\n");
        }
    }

    // Concat all the args at runtime using stringify.
    Expr combined_string =
        Internal::Call::make(Handle(), Internal::Call::stringify,
                             print_args, Internal::Call::Intrinsic);

    // Call halide_print.
    Expr print_call =
        Internal::Call::make(Int(32), "halide_print",
                             Internal::vec<Expr>(combined_string), Internal::Call::Extern);

    // Return the first argument.
    Expr result =
        Internal::Call::make(args[0].type(), Internal::Call::return_second,
                             Internal::vec<Expr>(print_call, args[0]), Internal::Call::Intrinsic);
    return result;
}

Expr print_when(Expr condition, const std::vector<Expr> &args) {
    Expr p = print(args);
    return Internal::Call::make(p.type(),
                                Internal::Call::if_then_else,
                                Internal::vec<Expr>(condition, p, args[0]),
                                Internal::Call::Intrinsic);
}

Expr memoize_tag(Expr result, const std::vector<Expr> &cache_key_values) {
    std::vector<Expr> args;
    args.push_back(result);
    args.insert(args.end(), cache_key_values.begin(), cache_key_values.end());
    return Internal::Call::make(result.type(), Internal::Call::memoize_expr,
                                args, Internal::Call::Intrinsic);
}

}
