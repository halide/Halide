#include "IR.h"
#include "Util.h"

/** \file
 * Utilities on halide types
 */

namespace Halide {

/** Return an integer which is the maximum value of this type. */
int Type::imax() const {
    if (is_uint()) {
        if (bits <= 32) {
            // For 32 bits, this may overflow but the value will still be correct
            return (int) ((1 << bits) - 1);
        } else {
            assert(0 && "max of Type: Type is too large");
            return 0;
        }
    } else if (is_int()) {
        if (bits <= 32) {
            return (int) ((1 << (bits-1)) - 1);
        } else {
            assert(0 && "max of Type: Type is too large");
            return 0;
        }
    } else {
        assert(0 && "max of Type: Not available for floating point types");
        return 0;
    }        
}

/** Return an expression which is the maximum value of this type */
Halide::Expr Type::max() const {
    if (is_int() && bits == 32) {
        return imax(); // No explicit cast of i32.
    }
    else if (is_int() || is_uint()) {
        return new Internal::Cast(*this, imax());
    }
    return Expr(); // Unknown maximum for floating types
}

/** Return an integer which is the minimum value of this type */
int Type::imin() const {
    if (is_uint()) {
        return 0;
    } else if (is_int()) {
        if (bits <= 32) {
            return -(1 << (bits-1));
        } else {
            assert(0 && "min of Type: Type is too large");
            return 0;
        }
    } else {
        assert(0 && "min of Type: Not available for floating point types");
        return 0;
    }        
}

/** Return an expression which is the minimum value of this type */
Expr Type::min() const {
    if (is_int() && bits == 32) {
        return imin(); // No explicit cast of i32.
    }
    else if (is_int() || is_uint()) {
        return new Internal::Cast(*this, imin());
    }
    return Expr(); // Unknown minimum for floating types
}

}
