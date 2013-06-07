#ifndef HALIDE_SIMPLIFY_H
#define HALIDE_SIMPLIFY_H

/** \file
 * Methods for simplifying halide statements and expressions 
 */

#include "IR.h"
#include <cmath>

namespace Halide { 
namespace Internal {

/** Perform a a wide range of simplifications to expressions and
 * statements, including constant folding, substituting in trivial
 * values, arithmetic rearranging, etc.
 */
// @{
Stmt simplify(Stmt);
Expr simplify(Expr);
// @}     
   
/** Implementations of division and mod that are specific to Halide.
 * Use these implementations; do not use native C division or mod to simplify
 * Halide expressions. */
template<typename T>
inline T mod_imp(T a, T b) {
    T rem = a % b;
    Type t = type_of<T>();
    if (t.is_int()) {
        rem = rem + (rem != 0 && (rem ^ b) < 0 ? b : 0);
    }
    return rem;
}
// Special cases for float, double.
template<> inline float mod_imp<float>(float a, float b) { 
    float f = a - b * (floorf(a / b));
    // The remainder has the same sign as b.
    return f; 
}
template<> inline double mod_imp<double>(double a, double b) {
    double f = a - b * (std::floor(a / b));
    return f; 
}

// Division that rounds the quotient down for integers.
template<typename T>
inline T div_imp(T a, T b) {
    Type t = type_of<T>();
    T quotient;
    if (t.is_int()) {
        T axorb = a ^ b;
        T post = a != 0 ? ((axorb) >> (t.bits-1)) : 0;
        T pre = a < 0 ? -post : post;
        T num = a + pre;
        T q = num / b;
        quotient = q + post;
    } else {
        quotient = a / b;
    }
    return quotient; 
}

void simplify_test();

}
}

#endif
