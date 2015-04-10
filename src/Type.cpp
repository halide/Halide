#include <sstream>

#include "IR.h"

namespace Halide {

using std::ostringstream;

/** Return an integer which is the maximum value of this type. */
int Type::imax() const {
    if (is_uint()) {
        if (bits == 32) {
            return 0xffffffff;
        } else if (bits < 32) {
            return (int) ((1 << bits) - 1);
        } else {
            internal_error
                << "Can't call Type::imax() on " << (*this)
                << " because value is too large to represent as a signed 32-bit integer\n";
            return 0;
        }
    } else if (is_int()) {
        if (bits == 32) {
            return 0x7fffffff;
        } else if (bits < 32) {
            return (int) ((1 << (bits-1)) - 1);
        } else {
            internal_error
                << "Can't call Type::imax() on " << (*this)
                << " because value is too large to represent as a signed 32-bit integer\n";
            return 0;
        }
    } else {
        internal_error
            << "Can't call Type::imax() on " << (*this)
            << " because value is too large to represent as a signed 32-bit integer\n";
        return 0;
    }
}

/** Return an expression which is the maximum value of this type */
Halide::Expr Type::max() const {
    if (is_vector()) {
        return Internal::Broadcast::make(element_of().max(), width);
    }
    if (is_int() && bits == 32) {
        return imax(); // No explicit cast of scalar i32.
    } else if ((is_int() || is_uint()) && bits <= 32) {
        return Internal::Cast::make(*this, imax());
    } else {
        // Use a run-time call to a math intrinsic (see posix_math.cpp)
        ostringstream ss;
        ss << "maxval_";
        if (is_int()) ss << "s";
        else if (is_uint()) ss << "u";
        else ss << "f";
        ss << bits;
        return Internal::Call::make(*this, ss.str(), std::vector<Expr>(), Internal::Call::Extern);
    }
}

/** Return an integer which is the minimum value of this type */
int Type::imin() const {
    if (is_uint()) {
        return 0;
    } else if (is_int()) {
        if (bits == 32) {
            return 0x80000000;
        } else if (bits < 32) {
            return -(1 << (bits-1));
        } else {
            internal_error
                << "Can't call Type::imin() on " << (*this)
                << " because value is too large to represent as a signed 32-bit integer\n";
            return 0;
        }
    } else {
        internal_error
            << "Can't call Type::imin() on " << (*this)
            << " because value is too large to represent as a signed 32-bit integer\n";
        return 0;
    }
}

/** Return an expression which is the minimum value of this type */
Expr Type::min() const {
    if (is_vector()) {
        return Internal::Broadcast::make(element_of().min(), width);
    }
    if (is_int() && bits == 32) {
        return imin(); // No explicit cast of scalar i32.
    } else if ((is_int() || is_uint()) && bits <= 32) {
        return Internal::Cast::make(*this, imin());
    } else {
        // Use a run-time call to a math intrinsic (see posix_math.cpp)
        ostringstream ss;
        ss << "minval_";
        if (is_int()) ss << "s";
        else if (is_uint()) ss << "u";
        else ss << "f";
        ss << bits;
        return Internal::Call::make(*this, ss.str(), std::vector<Expr>(), Internal::Call::Extern);
    }

}

bool Type::can_represent(Type other) const {
    if (width != other.width) return false;
    if (is_int()) {
        return ((other.is_int() && other.bits <= bits) ||
                (other.is_uint() && other.bits < bits));
    } else if (is_uint()) {
        return other.is_uint() && other.bits <= bits;
    } else if (is_float()) {
        return ((other.is_float() && other.bits <= bits) ||
                (bits == 64 && other.bits <= 32) ||
                (bits == 32 && other.bits <= 16));
    } else {
        return false;
    }
}


}
