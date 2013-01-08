#ifndef HALIDE_IR_OPERATOR_H
#define HALIDE_IR_OPERATOR_H

#include "IR.h"

namespace Halide {

namespace Internal {
bool is_const(Expr e);
bool is_positive_const(Expr e);
bool is_negative_const(Expr e);
bool is_zero(Expr e);
bool is_one(Expr e);
Expr make_const(Type t, int val);
Expr make_zero(Type t);
Expr make_one(Type t);
Expr make_two(Type t);
Expr const_true(int width = 1);
Expr const_false(int width = 1);
void match_types(Expr &a, Expr &b);
}

template<typename T>
inline Expr cast(Expr a) {
    return cast(type_of<T>(), a);
}

inline Expr cast(Type t, Expr a) {
    if (a.type() == t) return a;
    if (t.is_vector()) {
        if (a.type().is_scalar()) {
            return new Internal::Broadcast(cast(t.element_of(), a), t.width);
        } else if (const Internal::Broadcast *b = a.as<Internal::Broadcast>()) {
            assert(b->width == t.width);
            return new Internal::Broadcast(cast(t.element_of(), b->value), t.width);
        }
    }
    return new Internal::Cast(t, a);
}

inline Expr operator+(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::Add(a, b);
}
    
inline Expr &operator+=(Expr &a, Expr b) {
    a = new Internal::Add(a, cast(a.type(), b));
    return a;
}

inline Expr operator-(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::Sub(a, b);
}

inline Expr operator-(Expr a) {
    return new Internal::Sub(Internal::make_zero(a.type()), a);
}
    
inline Expr &operator-=(Expr &a, Expr b) {
    a = new Internal::Sub(a, cast(a.type(), b));
    return a;
}

inline Expr operator*(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::Mul(a, b);
}
    
inline Expr &operator*=(Expr &a, Expr b) {
    a = new Internal::Mul(a, cast(a.type(), b));
    return a;
}

inline Expr operator/(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::Div(a, b);
}

inline Expr &operator/=(Expr &a, Expr b) {
    a = new Internal::Div(a, cast(a.type(), b));
    return a;
}

inline Expr operator%(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::Mod(a, b);
}

inline Expr operator>(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::GT(a, b);
}

inline Expr operator<(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::LT(a, b);
}

inline Expr operator<=(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::LE(a, b);
}

inline Expr operator>=(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::GE(a, b);
}

inline Expr operator==(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::EQ(a, b);
}

inline Expr operator!=(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::NE(a, b);
}

inline Expr operator&&(Expr a, Expr b) {
    return new Internal::And(a, b);
}

inline Expr operator||(Expr a, Expr b) {
    return new Internal::Or(a, b);
}

inline Expr operator!(Expr a) {
    return new Internal::Not(a);
}

inline Expr max(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::Max(a, b);
}

inline Expr min(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Internal::Min(a, b);
}

inline Expr clamp(Expr a, Expr min_val, Expr max_val) {
    min_val = cast(a.type(), min_val);
    max_val = cast(a.type(), max_val);
    return new Internal::Max(new Internal::Min(a, max_val), min_val);
}

inline Expr abs(Expr a) {
    if (a.type() == Int(8))
        return new Internal::Call(Int(8), "abs_i8", vec(a));
    if (a.type() == Int(16)) 
        return new Internal::Call(Int(16), "abs_i16", vec(a));
    if (a.type() == Int(32)) 
        return new Internal::Call(Int(32), "abs_i32", vec(a));
    if (a.type() == Int(64)) 
        return new Internal::Call(Int(64), "abs_i64", vec(a));
    if (a.type() == Float(32)) 
        return new Internal::Call(Float(32), "abs_f32", vec(a));
    if (a.type() == Float(64)) 
        return new Internal::Call(Float(64), "abs_f64", vec(a));
    assert(false && "Invalid type for abs");
}

inline Expr select(Expr a, Expr b, Expr c) {
    return new Internal::Select(a, b, c);
}

inline Expr sin(Expr x) {
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "sin_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "sin_f32", vec(cast<float>(x)));
    }
}

inline Expr cos(Expr x) {
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "cos_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "cos_f32", vec(cast<float>(x)));
    }
}

inline Expr sqrt(Expr x) {
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "sqrt_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "sqrt_f32", vec(cast<float>(x)));
    }
}

inline Expr exp(Expr x) {
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "exp_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "exp_f32", vec(cast<float>(x)));
    }
}

inline Expr log(Expr x) {
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "log_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "log_f32", vec(cast<float>(x)));
    }
}

inline Expr floor(Expr x) {
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "floor_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "floor_f32", vec(cast<float>(x)));
    }
}

inline Expr ceil(Expr x) {
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "ceil_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "ceil_f32", vec(cast<float>(x)));
    }
}

inline Expr round(Expr x) {
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "round_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "round_f32", vec(cast<float>(x)));
    }
}

inline Expr pow(Expr x, Expr y) {
    if (x.type() == Float(64)) {
        y = cast<double>(y);
        return new Internal::Call(Float(64), "pow_f64", vec(x, y));
    } else {
        x = cast<float>(x);
        y = cast<float>(y);
        return new Internal::Call(Float(32), "pow_f32", vec(x, y));
    }
}

}


#endif
