#ifndef HALIDE_FLOAT16_H
#define HALIDE_FLOAT16_H

#include "runtime/HalideRuntime.h"
#include <cstdint>
#include <string>

namespace Halide {

/** Class that provides a type that implements half precision
 *  floating point (IEEE754 2008 binary16) in software.
 *
 *  This type is enforced to be 16-bits wide and maintains no state
 *  other than the raw IEEE754 binary16 bits so that it can passed
 *  to code that checks a type's size and used for halide_buffer_t allocation.
 * */
struct float16_t {

    static const int mantissa_bits = 10;
    static const uint16_t sign_mask = 0x8000;
    static const uint16_t exponent_mask = 0x7c00;
    static const uint16_t mantissa_mask = 0x03ff;

    /// \name Constructors
    /// @{

    /** Construct from a float, double, or int using
     * round-to-nearest-ties-to-even. Out-of-range values become +/-
     * infinity.
     */
    // @{
    explicit float16_t(float value);
    explicit float16_t(double value);
    explicit float16_t(int value);
    // @}

    /** Construct a float16_t with the bits initialised to 0. This represents
     * positive zero.*/
    float16_t() = default;

    /// @}

    // Use explicit to avoid accidently raising the precision
    /** Cast to float */
    explicit operator float() const;
    /** Cast to double */
    explicit operator double() const;
    /** Cast to int */
    explicit operator int() const;

    /** Get a new float16_t that represents a special value */
    // @{
    static float16_t make_zero();
    static float16_t make_negative_zero();
    static float16_t make_infinity();
    static float16_t make_negative_infinity();
    static float16_t make_nan();
    // @}

    /** Get a new float16_t with the given raw bits
     *
     * \param bits The bits conformant to IEEE754 binary16
     */
    static float16_t make_from_bits(uint16_t bits);

    /** Return a new float16_t with a negated sign bit*/
    float16_t operator-() const;

    /** Arithmetic operators. */
    // @{
    float16_t operator+(float16_t rhs) const;
    float16_t operator-(float16_t rhs) const;
    float16_t operator*(float16_t rhs) const;
    float16_t operator/(float16_t rhs) const;
    float16_t operator+=(float16_t rhs) {
        return (*this = *this + rhs);
    }
    float16_t operator-=(float16_t rhs) {
        return (*this = *this - rhs);
    }
    float16_t operator*=(float16_t rhs) {
        return (*this = *this * rhs);
    }
    float16_t operator/=(float16_t rhs) {
        return (*this = *this / rhs);
    }
    // @}

    /** Comparison operators */
    // @{
    bool operator==(float16_t rhs) const;
    bool operator!=(float16_t rhs) const {
        return !(*this == rhs);
    }
    bool operator>(float16_t rhs) const;
    bool operator<(float16_t rhs) const;
    bool operator>=(float16_t rhs) const {
        return (*this > rhs) || (*this == rhs);
    }
    bool operator<=(float16_t rhs) const {
        return (*this < rhs) || (*this == rhs);
    }
    // @}

    /** Properties */
    // @{
    bool is_nan() const;
    bool is_infinity() const;
    bool is_negative() const;
    bool is_zero() const;
    // @}

    /** Returns the bits that represent this float16_t.
     *
     *  An alternative method to access the bits is to cast a pointer
     *  to this instance as a pointer to a uint16_t.
     **/
    uint16_t to_bits() const;

private:
    // The raw bits.
    uint16_t data = 0;
};

static_assert(sizeof(float16_t) == 2, "float16_t should occupy two bytes");

}  // namespace Halide

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<Halide::float16_t>() {
    return halide_type_t(halide_type_float, 16);
}

namespace Halide {

/** Class that provides a type that implements half precision
 *  floating point using the bfloat16 format.
 *
 *  This type is enforced to be 16-bits wide and maintains no state
 *  other than the raw bits so that it can passed to code that checks
 *  a type's size and used for halide_buffer_t allocation. */
struct bfloat16_t {

    static const int mantissa_bits = 7;
    static const uint16_t sign_mask = 0x8000;
    static const uint16_t exponent_mask = 0x7f80;
    static const uint16_t mantissa_mask = 0x007f;

    static const bfloat16_t zero, negative_zero, infinity, negative_infinity, nan;

    /// \name Constructors
    /// @{

    /** Construct from a float, double, or int using
     * round-to-nearest-ties-to-even. Out-of-range values become +/-
     * infinity.
     */
    // @{
    explicit bfloat16_t(float value);
    explicit bfloat16_t(double value);
    explicit bfloat16_t(int value);
    // @}

    /** Construct a bfloat16_t with the bits initialised to 0. This represents
     * positive zero.*/
    bfloat16_t() = default;

    /// @}

    // Use explicit to avoid accidently raising the precision
    /** Cast to float */
    explicit operator float() const;
    /** Cast to double */
    explicit operator double() const;
    /** Cast to int */
    explicit operator int() const;

    /** Get a new bfloat16_t that represents a special value */
    // @{
    static bfloat16_t make_zero();
    static bfloat16_t make_negative_zero();
    static bfloat16_t make_infinity();
    static bfloat16_t make_negative_infinity();
    static bfloat16_t make_nan();
    // @}

    /** Get a new bfloat16_t with the given raw bits
     *
     * \param bits The bits conformant to IEEE754 binary16
     */
    static bfloat16_t make_from_bits(uint16_t bits);

    /** Return a new bfloat16_t with a negated sign bit*/
    bfloat16_t operator-() const;

    /** Arithmetic operators. */
    // @{
    bfloat16_t operator+(bfloat16_t rhs) const;
    bfloat16_t operator-(bfloat16_t rhs) const;
    bfloat16_t operator*(bfloat16_t rhs) const;
    bfloat16_t operator/(bfloat16_t rhs) const;
    bfloat16_t operator+=(bfloat16_t rhs) {
        return (*this = *this + rhs);
    }
    bfloat16_t operator-=(bfloat16_t rhs) {
        return (*this = *this - rhs);
    }
    bfloat16_t operator*=(bfloat16_t rhs) {
        return (*this = *this * rhs);
    }
    bfloat16_t operator/=(bfloat16_t rhs) {
        return (*this = *this / rhs);
    }
    // @}

    /** Comparison operators */
    // @{
    bool operator==(bfloat16_t rhs) const;
    bool operator!=(bfloat16_t rhs) const {
        return !(*this == rhs);
    }
    bool operator>(bfloat16_t rhs) const;
    bool operator<(bfloat16_t rhs) const;
    bool operator>=(bfloat16_t rhs) const {
        return (*this > rhs) || (*this == rhs);
    }
    bool operator<=(bfloat16_t rhs) const {
        return (*this < rhs) || (*this == rhs);
    }
    // @}

    /** Properties */
    // @{
    bool is_nan() const;
    bool is_infinity() const;
    bool is_negative() const;
    bool is_zero() const;
    // @}

    /** Returns the bits that represent this bfloat16_t.
     *
     *  An alternative method to access the bits is to cast a pointer
     *  to this instance as a pointer to a uint16_t.
     **/
    uint16_t to_bits() const;

private:
    // The raw bits.
    uint16_t data = 0;
};

static_assert(sizeof(bfloat16_t) == 2, "bfloat16_t should occupy two bytes");

}  // namespace Halide

template<>
HALIDE_ALWAYS_INLINE constexpr halide_type_t halide_type_of<Halide::bfloat16_t>() {
    return halide_type_t(halide_type_bfloat, 16);
}

#endif
