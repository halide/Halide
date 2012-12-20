#ifndef IR_OPERATOR_H
#define IR_OPERATOR_H

#include "IR.h"

namespace Halide {

namespace Internal {
bool is_const(Expr e);
bool is_positive_const(Expr e);
bool is_negative_const(Expr e);
bool is_zero(Expr e);
bool is_one(Expr e);
Expr make_zero(Type t);
Expr make_one(Type t);
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
    return new Cast(t, a);
}

inline Expr operator+(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Add(a, b);
}
    
inline Expr &operator+=(Expr &a, Expr b) {
    a = new Add(a, cast(a.type(), b));
    return a;
}

inline Expr operator-(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Sub(a, b);
}

inline Expr operator-(Expr a) {
    return new Sub(Internal::make_zero(a.type()), a);
}
    
inline Expr &operator-=(Expr &a, Expr b) {
    a = new Sub(a, cast(a.type(), b));
    return a;
}

inline Expr operator*(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Mul(a, b);
}
    
inline Expr &operator*=(Expr &a, Expr b) {
    a = new Mul(a, cast(a.type(), b));
    return a;
}

inline Expr operator/(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Div(a, b);
}

inline Expr &operator/=(Expr &a, Expr b) {
    a = new Div(a, cast(a.type(), b));
    return a;
}

inline Expr operator%(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Mod(a, b);
}

inline Expr operator>(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new GT(a, b);
}

inline Expr operator<(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new LT(a, b);
}

inline Expr operator<=(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new LE(a, b);
}

inline Expr operator>=(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new GE(a, b);
}

inline Expr operator==(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new EQ(a, b);
}

inline Expr operator!=(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new NE(a, b);
}

inline Expr operator&&(Expr a, Expr b) {
    return new And(a, b);
}

inline Expr operator||(Expr a, Expr b) {
    return new Or(a, b);
}

inline Expr operator!(Expr a) {
    return new Not(a);
}

inline Expr max(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Max(a, b);
}

inline Expr min(Expr a, Expr b) {
    Internal::match_types(a, b);
    return new Min(a, b);
}

inline Expr clamp(Expr a, Expr min_val, Expr max_val) {
    min_val = cast(a.type(), min_val);
    max_val = cast(a.type(), max_val);
    return new Max(new Min(a, max_val), min_val);
}

inline Expr abs(Expr a) {
    if (a.type() == Int(8))
        return new Call(Int(8), "abs_i8", vec(a));
    if (a.type() == Int(16)) 
        return new Call(Int(16), "abs_i16", vec(a));
    if (a.type() == Int(32)) 
        return new Call(Int(32), "abs_i32", vec(a));
    if (a.type() == Int(64)) 
        return new Call(Int(64), "abs_i64", vec(a));
    if (a.type() == Float(32)) 
        return new Call(Float(32), "abs_f32", vec(a));
    if (a.type() == Float(64)) 
        return new Call(Float(64), "abs_f64", vec(a));
    assert(false && "Invalid type for abs");
}

inline Expr select(Expr a, Expr b, Expr c) {
    return new Select(a, b, c);
}

inline Expr sin(Expr x) {
    if (x.type() == Float(64)) {
        return new Call(Float(64), "sin_f64", vec(x));
    } else {
        return new Call(Float(32), "sin_f32", vec(cast<float>(x)));
    }
}

inline Expr cos(Expr x) {
    if (x.type() == Float(64)) {
        return new Call(Float(64), "cos_f64", vec(x));
    } else {
        return new Call(Float(32), "cos_f32", vec(cast<float>(x)));
    }
}

inline Expr sqrt(Expr x) {
    if (x.type() == Float(64)) {
        return new Call(Float(64), "sqrt_f64", vec(x));
    } else {
        return new Call(Float(32), "sqrt_f32", vec(cast<float>(x)));
    }
}

inline Expr exp(Expr x) {
    if (x.type() == Float(64)) {
        return new Call(Float(64), "exp_f64", vec(x));
    } else {
        return new Call(Float(32), "exp_f32", vec(cast<float>(x)));
    }
}

inline Expr log(Expr x) {
    if (x.type() == Float(64)) {
        return new Call(Float(64), "log_f64", vec(x));
    } else {
        return new Call(Float(32), "log_f32", vec(cast<float>(x)));
    }
}

inline Expr floor(Expr x) {
    if (x.type() == Float(64)) {
        return new Call(Float(64), "floor_f64", vec(x));
    } else {
        return new Call(Float(32), "floor_f32", vec(cast<float>(x)));
    }
}

inline Expr ceil(Expr x) {
    if (x.type() == Float(64)) {
        return new Call(Float(64), "ceil_f64", vec(x));
    } else {
        return new Call(Float(32), "ceil_f32", vec(cast<float>(x)));
    }
}

inline Expr round(Expr x) {
    if (x.type() == Float(64)) {
        return new Call(Float(64), "round_f64", vec(x));
    } else {
        return new Call(Float(32), "round_f32", vec(cast<float>(x)));
    }
}

inline Expr pow(Expr x, Expr y) {
    if (x.type() == Float(64)) {
        y = cast<double>(y);
        return new Call(Float(64), "pow_f64", vec(x, y));
    } else {
        x = cast<float>(x);
        y = cast<float>(y);
        return new Call(Float(32), "pow_f32", vec(x, y));
    }
}

}


#endif
