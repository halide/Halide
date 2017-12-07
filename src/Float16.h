#ifndef HALIDE_FLOAT16_H
#define HALIDE_FLOAT16_H
#include "runtime/HalideRuntime.h"
#include <stdint.h>
#include <string>
#include "Util.h"

namespace Halide {

/** Class that provides a type that implements half precision
 *  floating point (IEEE754 2008 binary16) in software.
 *
 *  This type is enforced to be 16-bits wide and maintains no state
 *  other than the raw IEEE754 binary16 bits so that it can passed
 *  to code that checks a type's size and used for buffer_t allocation.
 * */
struct float16_t {

    /// \name Constructors
    /// @{

    /** Construct from a float, double, or int using
     * round-to-nearest-ties-to-even. Out-of-range values become +/-
     * infinity.
     */
    // @{
    EXPORT explicit float16_t(float value);
    EXPORT explicit float16_t(double value);
    EXPORT explicit float16_t(int value);
    // @}

    /** Construct a float16_t with the bits initialised to 0. This represents
     * positive zero.*/
    EXPORT float16_t();

    /// @}

    // Use explicit to avoid accidently raising the precision
    /** Cast to float */
    EXPORT explicit operator float() const;
    /** Cast to double */
    EXPORT explicit operator double() const;

    // Be explicit about how the copy constructor is expected to behave
    EXPORT float16_t(const float16_t&) = default;

    // Be explicit about how assignment is expected to behave
    EXPORT float16_t& operator=(const float16_t&) = default;

    /** \name Convenience "constructors"
     */
    /**@{*/

    /** Get a new float16_t that represents zero
     * \param positive if true then returns positive zero otherwise returns
     *        negative zero.
     */
    EXPORT static float16_t make_zero(bool positive);

    /** Get a new float16_t that represents infinity
     * \param positive if true then returns positive infinity otherwise returns
     *        negative infinity.
     */
    EXPORT static float16_t make_infinity(bool positive);

    /** Get a new float16_t that represents NaN (not a number) */
    EXPORT static float16_t make_nan();

    /** Get a new float16_t with the given raw bits
     *
     * \param bits The bits conformant to IEEE754 binary16
     */
    EXPORT static float16_t make_from_bits(uint16_t bits);

    /**@}*/

    /** Return a new float16_t with a negated sign bit*/
    EXPORT float16_t operator-() const;

    /** Arithmetic operators. */
    // @{
    EXPORT float16_t operator+(float16_t rhs) const;
    EXPORT float16_t operator-(float16_t rhs) const;
    EXPORT float16_t operator*(float16_t rhs) const;
    EXPORT float16_t operator/(float16_t rhs) const;
    // @}

    /** Comparison operators */
    // @{
    EXPORT bool operator==(float16_t rhs) const;
    EXPORT bool operator!=(float16_t rhs) const { return !(*this == rhs); }
    EXPORT bool operator>(float16_t rhs) const;
    EXPORT bool operator<(float16_t rhs) const;
    EXPORT bool operator>=(float16_t rhs) const { return (*this > rhs) || (*this == rhs); }
    EXPORT bool operator<=(float16_t rhs) const { return (*this < rhs) || (*this == rhs); }
    // @}

    /** Properties */
    // @{
    EXPORT bool is_nan() const;
    EXPORT bool is_infinity() const;
    EXPORT bool is_negative() const;
    EXPORT bool is_zero() const;
    // @}

    /** Returns the bits that represent this float16_t.
     *
     *  An alternative method to access the bits is to cast a pointer
     *  to this instance as a pointer to a uint16_t.
     **/
    EXPORT uint16_t to_bits() const;

private:
    // The raw bits.
    uint16_t data;
};

static_assert(sizeof(float16_t) == 2, "float16_t should occupy two bytes");

}  // namespace Halide

template<>
HALIDE_ALWAYS_INLINE halide_type_t halide_type_of<Halide::float16_t>() {
    return halide_type_t(halide_type_float, 16);
}

#endif
