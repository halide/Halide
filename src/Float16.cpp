#include "Float16.h"
#include "Error.h"
#include "LLVM_Headers.h"

using namespace Halide;

// These helper functions are not members of float16_t because
// it would expose the implementation
namespace {
llvm::APFloat::roundingMode
getLLVMAPFRoundingMode(Halide::RoundingMode mode) {
    switch (mode) {
    case RoundingMode::TowardZero:
        return llvm::APFloat::roundingMode::rmTowardZero;
    case RoundingMode::ToNearestTiesToEven:
        return llvm::APFloat::roundingMode::rmNearestTiesToEven;
    case RoundingMode::ToNearestTiesToAway:
        return llvm::APFloat::roundingMode::rmNearestTiesToAway;
    case RoundingMode::TowardPositiveInfinity:
        return llvm::APFloat::roundingMode::rmTowardPositive;
    case RoundingMode::TowardNegativeInfinity:
        return llvm::APFloat::roundingMode::rmTowardNegative;
    default:
        internal_error << "Invalid rounding mode :" << (int)mode << "\n";
    }
    llvm_unreachable("Could not get LLVM rounding mode");
}

float16_t toFP16(llvm::APFloat v) {
    uint64_t bits = v.bitcastToAPInt().getZExtValue();
    internal_assert(bits <= 0xFFFF) << "Invalid bits for float16_t\n";
    return float16_t::make_from_bits((uint16_t) bits);
}

llvm::APFloat toLLVMAPF(float16_t v) {
    llvm::APInt bitRepr(16, (uint64_t)v.to_bits(), /*isSigned=*/false);
#if LLVM_VERSION >= 40
    llvm::APFloat repr(llvm::APFloat::IEEEhalf(), bitRepr);
#else
    llvm::APFloat repr(llvm::APFloat::IEEEhalf, bitRepr);
#endif
    // use assert to avoid cost of conversion in release builds
    assert(v.to_bits() == toFP16(repr).to_bits() && "Converting to APFloat and back failed");
    return repr;
}

template <typename T>
void checkConversion(llvm::APFloat::opStatus status,
                     T value,
                     const char *typeName,
                     llvm::APFloat result) {
    // Check the exceptions
    internal_assert(!(status & llvm::APFloat::opStatus::opInvalidOp)) << "Unexpected invalid op\n";
    internal_assert(!(status & llvm::APFloat::opStatus::opDivByZero)) << "Unexpected div by zero\n";
    if (status & llvm::APFloat::opStatus::opOverflow) {
        user_error << "Converting " << value << " of type " << typeName <<
                   " to float16_t results in overflow (Result \"" << toFP16(result).to_decimal_string() << "\")\n";
    }
    if (status & llvm::APFloat::opStatus::opUnderflow) {
        internal_assert(status & llvm::APFloat::opStatus::opInexact) << "Underflow was flagged but inexact was not\n";
        // We don't emit a warning here because we will emit another warning
        // when handling ``opInexact``. APFloat mimics the default
        // exception handling behaviour for underflow in IEEE754 (7.5 Underflow)
        // where a flag is only raised if the result is inexact.
    }

    if (status & llvm::APFloat::opStatus::opInexact) {
        user_warning << "Converting " << value << " of type " << typeName <<
                     " to float16_t cannot be done exactly (Result \"" <<
                     toFP16(result).to_hex_string() <<
                     "\" which is approximately \"" <<
                     toFP16(result).to_decimal_string() << "\" in decimal)\n";
    }
}

template <typename T>
uint16_t getBitsFrom(T value, RoundingMode roundingMode, const char *typeName) {
    llvm::APFloat convertedValue(value);
    bool losesInfo = false;
    llvm::APFloat::opStatus status = convertedValue.convert(
#if LLVM_VERSION >= 40
        llvm::APFloat::IEEEhalf(),
#else
        llvm::APFloat::IEEEhalf,
#endif
        getLLVMAPFRoundingMode(roundingMode),
        &losesInfo);
    checkConversion(status, value, typeName, convertedValue);
    return toFP16(convertedValue).to_bits();
}

template <>
uint16_t getBitsFrom(const char *value, RoundingMode roundingMode, const char *typeName) {
#if LLVM_VERSION >= 40
    llvm::APFloat convertedValue(llvm::APFloat::IEEEhalf());
#else
    llvm::APFloat convertedValue(llvm::APFloat::IEEEhalf);
#endif
    // TODO: Sanitize value
    llvm::APFloat::opStatus status = convertedValue.convertFromString(value,
        getLLVMAPFRoundingMode(roundingMode));

    checkConversion(status, value, typeName, convertedValue);
    return toFP16(convertedValue).to_bits();
}

template <>
uint16_t getBitsFrom(int64_t value, RoundingMode roundingMode, const char *typeName) {
#if LLVM_VERSION >= 40
    llvm::APFloat convertedValue(llvm::APFloat::IEEEhalf());
#else
    llvm::APFloat convertedValue(llvm::APFloat::IEEEhalf);
#endif
#if LLVM_VERSION >= 50
    // A comment in LLVM's APFloat.h indicates we should perhaps use
    // llvm::APInt::WordType directly. However this type matches the
    // prototype of the method it is passed to below, so it seems more
    // correct. This code will likely have to change again.
    llvm::APFloatBase::integerPart asIP = value;
#else
    llvm::integerPart asIP = value;
#endif
    llvm::APFloat::opStatus status = convertedValue.convertFromSignExtendedInteger(
        &asIP,
        /*srcCount=*/1, // All bits are contained within a single int64_t
        /*isSigned=*/true,
        getLLVMAPFRoundingMode(roundingMode));
    checkConversion(status, value, typeName, convertedValue);
    return toFP16(convertedValue).to_bits();
}

}  //end anonymous namespace
// End helper functions

namespace Halide {
// The static_asserts checking the size is to make sure
// float16_t can be used as a 16-bits wide POD type.
float16_t::float16_t(float value, RoundingMode roundingMode) {
    static_assert(sizeof(float16_t) == 2, "float16_t is wrong size");
    this->data = getBitsFrom(value, roundingMode, "float");
}

float16_t::float16_t(double value, RoundingMode roundingMode) {
    static_assert(sizeof(float16_t) == 2, "float16_t is wrong size");
    this->data = getBitsFrom(value, roundingMode, "double");
}

float16_t::float16_t(const char *stringRepr, RoundingMode roundingMode) {
    static_assert(sizeof(float16_t) == 2, "float16_t is wrong size");
    this->data = getBitsFrom(stringRepr, roundingMode, "string");
}

float16_t::float16_t() {
    static_assert(sizeof(float16_t) == 2, "float16_t is wrong size");
    this->data = 0;
}

float16_t float16_t::make_from_signed_int(int64_t value, RoundingMode roundingMode) {
    static_assert(sizeof(float16_t) == 2, "float16_t is wrong size");
    float16_t val;
    val.data = getBitsFrom(value, roundingMode, "int64_t");
    return val;
}

float16_t float16_t::make_from_bits(uint16_t rawBits) {
    static_assert(sizeof(float16_t) == 2, "float16_t is wrong size");
    float16_t val;
    val.data = rawBits;
    return val;
}

float16_t::operator float() const {
    llvm::APFloat convertedValue = toLLVMAPF(*this);
    bool losesInfo = false;
    // Converting to a more precise type so the rounding mode does not matter, so
    // just pick any.
#if LLVM_VERSION >= 40 
    convertedValue.convert(llvm::APFloat::IEEEsingle(), llvm::APFloat::rmNearestTiesToEven, &losesInfo);
#else
    convertedValue.convert(llvm::APFloat::IEEEsingle, llvm::APFloat::rmNearestTiesToEven, &losesInfo);
#endif
    internal_assert(!losesInfo) << "Unexpected information loss\n";
    return convertedValue.convertToFloat();
}

float16_t::operator double() const {
    llvm::APFloat convertedValue = toLLVMAPF(*this);
    bool losesInfo = false;
    // Converting to a more precise type so the rounding mode does not matter, so
    // just pick any.
#if LLVM_VERSION >= 40
    convertedValue.convert(llvm::APFloat::IEEEdouble(), llvm::APFloat::rmNearestTiesToEven, &losesInfo);
#else
    convertedValue.convert(llvm::APFloat::IEEEdouble, llvm::APFloat::rmNearestTiesToEven, &losesInfo);
#endif
    internal_assert(!losesInfo) << "Unexpected information loss\n";
    return convertedValue.convertToDouble();
}

float16_t float16_t::make_zero(bool positive) {
#if LLVM_VERSION >= 40
    llvm::APFloat zero = llvm::APFloat::getZero(llvm::APFloat::IEEEhalf(), !positive);
#else
    llvm::APFloat zero = llvm::APFloat::getZero(llvm::APFloat::IEEEhalf, !positive);
#endif
    return toFP16(zero);
}

float16_t float16_t::make_infinity(bool positive) {
#if LLVM_VERSION >= 40
    llvm::APFloat inf = llvm::APFloat::getInf(llvm::APFloat::IEEEhalf(), !positive);
#else
    llvm::APFloat inf = llvm::APFloat::getInf(llvm::APFloat::IEEEhalf, !positive);
#endif
    return toFP16(inf);
}

float16_t float16_t::make_nan() {
#if LLVM_VERSION >= 40
    llvm::APFloat nan = llvm::APFloat::getNaN(llvm::APFloat::IEEEhalf());
#else
    llvm::APFloat nan = llvm::APFloat::getNaN(llvm::APFloat::IEEEhalf);
#endif
    return toFP16(nan);
}

float16_t float16_t::add(float16_t rhs, RoundingMode roundingMode) const {
    llvm::APFloat result = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(rhs);
    // FIXME: Ignoring possible exceptions
    result.add(rhsAPF, getLLVMAPFRoundingMode(roundingMode));
    return toFP16(result);
}

float16_t float16_t::subtract(float16_t rhs, RoundingMode roundingMode) const {
    llvm::APFloat result = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(rhs);
    // FIXME: Ignoring possible exceptions
    result.subtract(rhsAPF, getLLVMAPFRoundingMode(roundingMode));
    return toFP16(result);
}

float16_t float16_t::multiply(float16_t rhs, RoundingMode roundingMode) const {
    llvm::APFloat result = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(rhs);
    // FIXME: Ignoring possible exceptions
    result.multiply(rhsAPF, getLLVMAPFRoundingMode(roundingMode));
    return toFP16(result);
}

float16_t float16_t::divide(float16_t denominator, RoundingMode roundingMode) const {
    llvm::APFloat result = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(denominator);
    // FIXME: Ignoring possible exceptions
    result.divide(rhsAPF, getLLVMAPFRoundingMode(roundingMode));
    return toFP16(result);
}

float16_t float16_t::remainder(float16_t denominator) const {
    llvm::APFloat result = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(denominator);
    // FIXME: Ignoring possible exceptions
    result.remainder(rhsAPF);
    return toFP16(result);
}

float16_t float16_t::mod(float16_t denominator, RoundingMode roundingMode) const {
    llvm::APFloat result = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(denominator);
    // FIXME: Ignoring possible exceptions
    // LLVM removed the rounding mode as the operation is always exact.
    // TODO: change float16_t::mod to no take a rounding mode.
    #if LLVM_VERSION < 38
    result.mod(rhsAPF, getLLVMAPFRoundingMode(roundingMode));
    #else
    result.mod(rhsAPF);
    #endif
    return toFP16(result);
}

float16_t float16_t::operator-() const {
    llvm::APFloat result = toLLVMAPF(*this);
    result.changeSign();
    return toFP16(result);
}

float16_t float16_t::operator+(float16_t rhs) const {
    return this->add(rhs, RoundingMode::ToNearestTiesToEven);
}

float16_t float16_t::operator-(float16_t rhs) const {
    return this->subtract(rhs, RoundingMode::ToNearestTiesToEven);
}

float16_t float16_t::operator*(float16_t rhs) const {
    return this->multiply(rhs, RoundingMode::ToNearestTiesToEven);
}

float16_t float16_t::operator/(float16_t rhs) const {
    return this->divide(rhs, RoundingMode::ToNearestTiesToEven);
}

bool float16_t::operator==(float16_t rhs) const {
    llvm::APFloat lhsAPF = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(rhs);
    return lhsAPF.compare(rhsAPF) == llvm::APFloat::cmpEqual;
}

std::string float16_t::to_hex_string() const {
    // Expected format of result: [-]0xh.hhhp[+-]dd
    // at most 12 characters are needed for half precision
    // + 1 for null terminator
    char buffer[13];
    llvm::APFloat repr = toLLVMAPF(*this);
    // The rounding mode does not matter here when we set hexDigits to 0 which
    // will give the precise representation. So any rounding mode will do.
    unsigned count = repr.convertToHexString(buffer,
                                             /*hexDigits=*/0,
                                             /*upperCase=*/false,
                                             llvm::APFloat::rmNearestTiesToEven);
    internal_assert(count < sizeof(buffer) / sizeof(char)) << "Incorrect buffer size\n";
    std::string result(buffer);
    return result;
}

bool float16_t::operator>(float16_t rhs) const {
    internal_assert(!this->are_unordered(rhs)) << "Cannot compare unorderable values\n";
    llvm::APFloat lhsAPF = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(rhs);
    return lhsAPF.compare(rhsAPF) == llvm::APFloat::cmpGreaterThan;
}

bool float16_t::operator<(float16_t rhs) const {
    internal_assert(!this->are_unordered(rhs)) << "Cannot compare unorderable values\n";
    llvm::APFloat lhsAPF = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(rhs);
    return lhsAPF.compare(rhsAPF) == llvm::APFloat::cmpLessThan;
}

bool float16_t::are_unordered(float16_t rhs) const {
    llvm::APFloat lhsAPF = toLLVMAPF(*this);
    llvm::APFloat rhsAPF = toLLVMAPF(rhs);
    return lhsAPF.compare(rhsAPF) == llvm::APFloat::cmpUnordered;
}

std::string float16_t::to_decimal_string(unsigned int significantDigits) const {
    llvm::APFloat repr = toLLVMAPF(*this);
    llvm::SmallVector<char, 16> result;
    repr.toString(result, /*FormatPrecision=*/significantDigits, /*FormatMaxPadding=*/0);
    return std::string(result.begin(), result.end());
}

bool float16_t::is_nan() const {
    llvm::APFloat repr = toLLVMAPF(*this);
    return repr.isNaN();
}

bool float16_t::is_infinity() const {
    llvm::APFloat repr = toLLVMAPF(*this);
    return repr.isInfinity();
}

bool float16_t::is_negative() const {
    llvm::APFloat repr = toLLVMAPF(*this);
    return repr.isNegative();
}

bool float16_t::is_zero() const {
    llvm::APFloat repr = toLLVMAPF(*this);
    return repr.isZero();
}

uint16_t float16_t::to_bits() const {
    return this->data;
}

}  // namespace halide
