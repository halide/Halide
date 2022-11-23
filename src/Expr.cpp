#include "Expr.h"
#include "IROperator.h"  // for lossless_cast()

namespace Halide {
namespace Internal {

const IntImm *IntImm::make(Type t, int64_t value) {
    internal_assert(t.is_int() && t.is_scalar())
        << "IntImm must be a scalar Int\n";
    internal_assert(t.bits() >= 1 && t.bits() <= 64)
        << "IntImm must have between 1 and 64 bits\n";

    // Normalize the value by dropping the high bits.
    // Since left-shift of negative value is UB in C++, cast to uint64 first;
    // it's unlikely any compilers we care about will misbehave, but UBSan will complain.
    value = (int64_t)(((uint64_t)value) << (64 - t.bits()));

    // Then sign-extending to get them back
    value >>= (64 - t.bits());

    IntImm *node = new IntImm;
    node->type = t;
    node->value = value;
    return node;
}

const UIntImm *UIntImm::make(Type t, uint64_t value) {
    internal_assert(t.is_uint() && t.is_scalar())
        << "UIntImm must be a scalar UInt\n";
    internal_assert(t.bits() >= 1 && t.bits() <= 64)
        << "UIntImm must have between 1 and 64 bits\n";

    // Normalize the value by dropping the high bits
    value <<= (64 - t.bits());
    value >>= (64 - t.bits());

    UIntImm *node = new UIntImm;
    node->type = t;
    node->value = value;
    return node;
}

const FloatImm *FloatImm::make(Type t, double value) {
    internal_assert(t.is_float() && t.is_scalar())
        << "FloatImm must be a scalar Float\n";
    FloatImm *node = new FloatImm;
    node->type = t;
    switch (t.bits()) {
    case 16:
        if (t.is_bfloat()) {
            node->value = (double)((bfloat16_t)value);
        } else {
            node->value = (double)((float16_t)value);
        }
        break;
    case 32:
        node->value = (float)value;
        break;
    case 64:
        node->value = value;
        break;
    default:
        internal_error << "FloatImm must be 16, 32, or 64-bit\n";
    }

    return node;
}

const StringImm *StringImm::make(const std::string &val) {
    StringImm *node = new StringImm;
    node->type = type_of<const char *>();
    node->value = val;
    return node;
}

/** Check if for_type executes for loop iterations in parallel and unordered. */
bool is_unordered_parallel(ForType for_type) {
    return (for_type == ForType::Parallel ||
            for_type == ForType::GPUBlock ||
            for_type == ForType::GPUThread);
}

/** Returns true if for_type executes for loop iterations in parallel. */
bool is_parallel(ForType for_type) {
    return (is_unordered_parallel(for_type) ||
            for_type == ForType::Vectorized ||
            for_type == ForType::GPULane);
}

}  // namespace Internal

Range::Range(const Expr &min_in, const Expr &extent_in)
    : min(lossless_cast(Int(32), min_in)), extent(lossless_cast(Int(32), extent_in)) {
    if (min_in.defined() && !min.defined()) {
        user_error << "Range min is not representable as an int32: " << min_in;
    }
    if (extent_in.defined() && !extent.defined()) {
        user_error << "Range extent is not representable as an int32: " << extent_in;
    }
}

}  // namespace Halide
