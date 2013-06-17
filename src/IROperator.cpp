#include "IROperator.h"
#include "IRPrinter.h"
#include "Simplify.h"
#include <iostream>
#include <math.h>

namespace Halide { 
namespace Internal {

bool is_const(Expr e) {
    if (e.as<IntImm>()) return true;
    if (e.as<FloatImm>()) return true;
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

const int * EXPORT as_const_int(Expr e) {
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
    if (int_imm) {
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
        assert(0 && "Cast of integer to non-integer not available here");
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



void match_types(Expr &a, Expr &b) {
    if (a.type() == b.type()) return;

    // First widen to match
    if (a.type().is_scalar() && b.type().is_vector()) {
        a = Broadcast::make(a, b.type().width);
    } else if (a.type().is_vector() && b.type().is_scalar()) {
        b = Broadcast::make(b, a.type().width);
    } else {
        assert(a.type().width == b.type().width && "Can't match types of differing widths");
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
    } else if (!ta.is_float() && !tb.is_float() && is_const(b)) {
        // (u)int(a) * (u)intImm(b) -> (u)int(a)
        b = cast(ta, b); 
    } else if (!tb.is_float() && !ta.is_float() && is_const(a)) {
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
        std::cerr << "Could not match types: " << ta << ", " << tb << std::endl;
        assert(false && "Failed type coercion");    
    }
}

// Fast math ops based on those from Syrah (http://github.com/boulos/syrah). Thanks, Solomon!

// Factor a float into 2^exponent * reduced, where reduced is between 0.75 and 1.5
void range_reduce_log(Expr input, Expr *reduced, Expr *exponent) {
    Expr int_version = reinterpret<int>(input);

    // single precision = SEEE EEEE EMMM MMMM MMMM MMMM MMMM MMMM
    // exponent mask    = 0111 1111 1000 0000 0000 0000 0000 0000
    //                    0x7  0xF  0x8  0x0  0x0  0x0  0x0  0x0
    // non-exponent     = 1000 0000 0111 1111 1111 1111 1111 1111
    //                  = 0x8  0x0  0x7  0xF  0xF  0xF  0xF  0xF
    int non_exponent_mask = 0x807fffff;

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

    *reduced = reinterpret<float>(blended);

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
    assert(x_full.type() == Float(32));

    if (is_const(x_full)) {
        x_full = simplify(x_full);
        const float * f = as_const_float(x_full);
        if (f) {
            return logf(*f);
        }
    }

    Expr nan = Call::make(Float(32), "nan_f32", std::vector<Expr>(), Call::Extern);
    Expr neg_inf = Call::make(Float(32), "neg_inf_f32", std::vector<Expr>(), Call::Extern);

    Expr use_nan = x_full < 0.0f; // log of a negative returns nan
    Expr use_neg_inf = x_full == 0.0f; // log of zero is -inf
    Expr exceptional = use_nan | use_neg_inf;

    // Avoid producing nans or infs by generating ln(1.0f) instead and
    // then fixing it later.
    Expr patched = select(exceptional, 1.0f, x_full);
    Expr reduced, exponent;
    range_reduce_log(patched, &reduced, &exponent);
   
    // Very close to the Taylor series for log about 1, but tuned to
    // have minimum relative error in the reduced domain (0.75 - 1.5).
    
    Expr x1 = reduced - 1.0f;
    Expr result = 0.0f;
    result = x1 * result + 0.05111976432738144643f;
    result = x1 * result + -0.11793923497136414580f;
    result = x1 * result + 0.14971993724699017569f;
    result = x1 * result + -0.16862004708254804686f;
    result = x1 * result + 0.19980668101718729313f;
    result = x1 * result + -0.24991211576292837737f;
    result = x1 * result + 0.33333435275479328386f;
    result = x1 * result + -0.50000106292873236491f;
    result = x1 * x1 * result + x1;

    result += cast<float>(exponent) * logf(2.0);

    return select(exceptional, select(use_nan, nan, neg_inf), result);
}

Expr halide_exp(Expr x_full) {
    assert(x_full.type() == Float(32));

    if (is_const(x_full)) {
        x_full = simplify(x_full);
        const float * f = as_const_float(x_full);
        if (f) {
            return logf(*f);
        }
    }

    float ln2_part1 = 0.6931457519f;
    float ln2_part2 = 1.4286067653e-6f;
    float one_over_ln2 = 1.0/logf(2.0);

    Expr scaled = x_full * one_over_ln2;
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);

    Expr x = x_full - k_real * ln2_part1;
    x -= k_real * ln2_part2;

    Expr result = 0.0f;
    result = x * result + 0.00031965933071842413f;
    result = x * result + 0.00119156835564003744f;
    result = x * result + 0.00848988645943932717f;
    result = x * result + 0.04160188091348320655f;
    result = x * result + 0.16667983794100929562f;
    result = x * result + 0.49999899033463041098f;
    result = x * result + 1.0f;
    result = x * result + 1.0f;

    // Compute 2^k.
    int fpbias = 127;
    Expr biased = k + fpbias;

    Expr inf = Call::make(Float(32), "inf_f32", std::vector<Expr>(), Call::Extern);
    
    // Shift the bits up into the exponent field and reinterpret this
    // thing as float.
    Expr two_to_the_n = reinterpret<float>(biased << 23);
    result *= two_to_the_n;    

    // Catch overflow and underflow
    result = select(biased < 255, result, inf);
    result = select(biased > 0, result, 0.0f);

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

}

Expr fast_log(Expr x) {
    assert(x.type() == Float(32) && "fast_log only works for Float(32)");

    Expr reduced, exponent;
    range_reduce_log(x, &reduced, &exponent);

    Expr x1 = reduced - 1.0f;
    Expr result = 0.0f;

    result = x1 * result + 0.07640318789187280912f;
    result = x1 * result + -0.16252961013874300811f;
    result = x1 * result + 0.20625219040645212387f;
    result = x1 * result + -0.25110261010892864775f;
    result = x1 * result + 0.33320464908377461777f;
    result = x1 * result + -0.49997513376789826101f;
    result *= x1 * x1;
    result += x1;

    return result + cast<float>(exponent) * logf(2);
}

Expr fast_exp(Expr x_full) {
    assert(x_full.type() == Float(32) && "fast_exp only works for Float(32)");

    Expr scaled = x_full / logf(2.0);
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);
    Expr x = x_full - k_real * logf(2.0);

    Expr result = 0.0f;
    result = x * result + 0.01314350012789660196f;
    result = x * result + 0.03668965196652099192f;
    result = x * result + 0.16873890085469545053f;
    result = x * result + 0.49970514590562437052f;
    result = x * result + 1.0f;
    result = x * result + 1.0f;

    // Compute 2^k.
    int fpbias = 127;
    Expr biased = clamp(k + fpbias, 0, 255);

    // Shift the bits up into the exponent field and reinterpret this
    // thing as float.
    Expr two_to_the_n = reinterpret<float>(biased << 23);
    result *= two_to_the_n;

    return result;
}

}
