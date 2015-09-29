#include <sstream>
#include <cfloat>
#include "IR.h"

namespace Halide {

using std::ostringstream;

/** Return an integer which is the maximum value of this type. */
int64_t Type::imax() const {
    if (is_uint()) {
        if (bits == 64) {
            return -1;
        } else {
            return (1LL << bits) - 1;
        }
    } else if (is_int()) {
        return (1LL << (bits-1)) - 1;
    } else {
        internal_error
            << "Can't call Type::imax() on " << (*this)
            << " because value is too large to represent as a signed 64-bit integer\n";
        return 0;
    }
}

/** Return an expression which is the maximum value of this type */
Halide::Expr Type::max() const {
    if (is_vector()) {
        return Internal::Broadcast::make(element_of().max(), width);
    } else if (is_int()) {
        return Internal::IntImm::make(*this, imax());
    } else if (is_uint()) {
        return Internal::UIntImm::make(*this, imax());
    } else {
        internal_assert(is_float());
        if (bits == 16) {
            return Internal::FloatImm::make(*this, 65504.0);
        } else if (bits == 32) {
            return Internal::FloatImm::make(*this, FLT_MAX);
        } else if (bits == 64) {
            return Internal::FloatImm::make(*this, DBL_MAX);
        } else {
            internal_error
                << "Unknown float type: " << (*this) << "\n";
            return 0;
        }
    }
}

/** Return an integer which is the minimum value of this type */
int64_t Type::imin() const {
    if (is_uint()) {
        return 0;
    } else if (is_int()) {
        if (bits == 64) {
            return 1LL << 63;
        } else {
            return -(1LL << (bits-1));
        }
    } else {
        internal_error
            << "Can't call Type::imin() on " << (*this)
            << " because value is too large to represent as a signed 64-bit integer\n";
        return 0;
    }
}

/** Return an expression which is the minimum value of this type */
Halide::Expr Type::min() const {
    if (is_vector()) {
        return Internal::Broadcast::make(element_of().min(), width);
    } else if (is_int()) {
        return Internal::IntImm::make(*this, imin());
    } else if (is_uint()) {
        return Internal::UIntImm::make(*this, imin());
    } else {
        internal_assert(is_float());
        if (bits == 16) {
            return Internal::FloatImm::make(*this, -65504.0);
        } else if (bits == 32) {
            return Internal::FloatImm::make(*this, -FLT_MAX);
        } else if (bits == 64) {
            return Internal::FloatImm::make(*this, -DBL_MAX);
        } else {
            internal_error
                << "Unknown float type: " << (*this) << "\n";
            return 0;
        }
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

bool Type::can_represent(int64_t x) const {
    if (is_int()) {
        return x >= imin() && x <= imax();
    } else if (is_uint()) {
        return x >= 0 && (uint64_t)x <= (uint64_t)(imax());
    } else if (is_float()) {
        switch (bits) {
        case 16:
            return (int64_t)(float)(float16_t)(float)x == x;
        case 32:
            return (int64_t)(float)x == x;
        case 64:
            return (int64_t)(double)x == x;
        default:
            return false;
        }
    } else {
        return false;
    }
}

bool Type::can_represent(uint64_t x) const {
    if (is_int() || is_uint()) {
        return x <= (uint64_t)(imax());
    } else if (is_float()) {
        switch (bits) {
        case 16:
            return (uint64_t)(float)(float16_t)(float)x == x;
        case 32:
            return (uint64_t)(float)x == x;
        case 64:
            return (uint64_t)(double)x == x;
        default:
            return false;
        }
    } else {
        return false;
    }
}

bool Type::can_represent(double x) const {
    if (is_int()) {
        int64_t i = x;
        return (x >= imin()) && (x <= imax()) && (x == (double)i);
    } else if (is_uint()) {
        uint64_t u = x;
        return (x >= 0) && (x <= (uint64_t)imax()) && (x == (double)u);
    } else if (is_float()) {
        switch (bits) {
        case 16:
            return (double)(float16_t)x == x;
        case 32:
            return (double)(float)x == x;
        case 64:
            return true;
        default:
            return false;
        }
    } else {
        return false;
    }
}


}
