#ifndef IR_OPERATOR_H
#define IR_OPERATOR_H

#include "IR.h"

namespace Halide {

inline Expr operator+(Expr a, Expr b) {
    return new Add(a, b);
}
    
inline Expr &operator+=(Expr &a, Expr b) {
    a = new Add(a, b);
    return a;
}

inline Expr operator-(Expr a, Expr b) {
    return new Sub(a, b);
}
    
inline Expr &operator-=(Expr &a, Expr b) {
    a = new Sub(a, b);
    return a;
}

inline Expr operator*(Expr a, Expr b) {
    return new Mul(a, b);
}
    
inline Expr &operator*=(Expr &a, Expr b) {
    a = new Mul(a, b);
    return a;
}

inline Expr operator/(Expr a, Expr b) {
    return new Div(a, b);
}

inline Expr &operator/=(Expr &a, Expr b) {
    a = new Div(a, b);
    return a;
}

inline Expr operator%(Expr a, Expr b) {
    return new Mod(a, b);
}

inline Expr operator>(Expr a, Expr b) {
    return new GT(a, b);
}

inline Expr operator<(Expr a, Expr b) {
    return new LT(a, b);
}

inline Expr operator<=(Expr a, Expr b) {
    return new LE(a, b);
}

inline Expr operator>=(Expr a, Expr b) {
    return new GE(a, b);
}

inline Expr operator==(Expr a, Expr b) {
    return new EQ(a, b);
}

inline Expr operator!=(Expr a, Expr b) {
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
}

}


#endif
