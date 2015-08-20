#include "Halide.h"
#include <stdio.h>
#include <cmath>

using namespace Halide;

// FIXME: Why aren't we using a unit test framework for this?
void h_assert(bool condition, const char* msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

int main() {
    // Special constants

    // +ve Zero
    {
        // Try constructing +ve zero in different ways and check they all represent
        // the same float16_t
        const float16_t zeroDefaultConstructor;
        const float16_t zeroP = float16_t::make_zero(/*positive=*/true);
        const float16_t zeroPStringConstructorDecimal("0.0", RoundingMode::ToNearestTiesToEven);
        const float16_t zeroPStringConstructorHex("0x0p0", RoundingMode::ToNearestTiesToEven);
        const float16_t zeroPFromFloat(0.0f, RoundingMode::ToNearestTiesToEven);
        const float16_t zeroPFromDouble(0.0,RoundingMode::ToNearestTiesToEven);
        const float16_t zeroPFromInt = float16_t::make_from_signed_int(0, RoundingMode::ToNearestTiesToEven);
        h_assert(zeroDefaultConstructor.to_bits() == zeroP.to_bits(), "Mismatch between constructors");
        h_assert(zeroPStringConstructorDecimal.to_bits() == zeroP.to_bits(), "Mismatch between constructors");
        h_assert(zeroPStringConstructorHex.to_bits() == zeroP.to_bits(), "Mismatch between constructors");
        h_assert(zeroPFromFloat.to_bits() == zeroP.to_bits(), "Mistmatch between constructors");
        h_assert(zeroPFromDouble.to_bits() == zeroP.to_bits(), "Mistmatch between constructors");
        h_assert(zeroPFromInt.to_bits() == zeroP.to_bits(), "make_from_signed_int gave wrong value");

        // Check the representation
        h_assert(zeroP.is_zero() && !zeroP.is_negative(), "+ve zero invalid");
        h_assert(zeroP.to_bits() == 0x0000, "+ve zero invalid bits");
        h_assert(zeroP.to_hex_string() == "0x0p0", "+ve zero hex string invalid");
        h_assert(zeroP.to_decimal_string(0) == "0.0E+0", "+ve zero decimal string invalid");

        // Try converting to native float types
        h_assert( ((float) zeroP) == 0.0f, "+ve zero conversion to float invalid");
        h_assert( ((double) zeroP) == 0.0, "+ve zero conversion to double invalid");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&zeroP) == 0x0000,
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // -ve Zero
    {
        // Try constructing -ve zero in different ways and check they all represent
        // the same float16_t
        const float16_t zeroN = float16_t::make_zero(/*positive=*/false);
        const float16_t zeroNStringConstructorDecimal("-0.0", RoundingMode::ToNearestTiesToEven);
        const float16_t zeroNStringConstructorHex("-0x0p0", RoundingMode::ToNearestTiesToEven);
        const float16_t zeroNFromFloat(-0.0f, RoundingMode::ToNearestTiesToEven);
        const float16_t zeroNFromDouble(-0.0, RoundingMode::ToNearestTiesToEven);
        h_assert(zeroNStringConstructorDecimal.to_bits() == zeroN.to_bits(), "Mismatch between constructors");
        h_assert(zeroNStringConstructorHex.to_bits() == zeroN.to_bits(), "Mismatch between constructors");
        h_assert(zeroNFromFloat.to_bits() == zeroN.to_bits(), "Mismatch between constructors");
        h_assert(zeroNFromDouble.to_bits() == zeroN.to_bits(), "Mismatch between constructors");

        // Check the representation
        h_assert(zeroN.is_zero() && zeroN.is_negative(), "-ve zero invalid");
        h_assert(zeroN.to_bits() == 0x8000, "-ve zero invalid bits");
        h_assert(zeroN.to_hex_string() == "-0x0p0", "-ve zero hex string invalid");
        h_assert(zeroN.to_decimal_string(0) == "-0.0E+0", "-ve zero decimal string invalid");

        // Try converting to native float types
        h_assert( ((float) zeroN) == -0.0f, "-ve zero conversion to float invalid");
        h_assert( ((double) zeroN) == -0.0, "-ve zero conversion to double invalid");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&zeroN) == 0x8000,
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // +ve infinity
    {
        // Try constructing +ve infinity in different ways and check they all
        // represent the same float16_t
        const float16_t infinityP = float16_t::make_infinity(/*positive=*/true);
        const float16_t infinityPFromFloat( (float) INFINITY, RoundingMode::ToNearestTiesToEven);
        const float16_t infinityPFromDouble( (double) INFINITY, RoundingMode::ToNearestTiesToEven);
        h_assert(infinityPFromFloat.to_bits() == infinityP.to_bits(), "Mismatch between constructors");
        h_assert(infinityPFromDouble.to_bits() == infinityP.to_bits(), "Mismatch between constructors");

        // Check the representation
        h_assert(infinityP.is_infinity() && !infinityP.is_negative(), "+ve infinity invalid");
        h_assert(infinityP.to_bits() == 0x7c00, "+ve infinity invalid bits");
        h_assert(infinityP.to_hex_string() == "infinity", "+ve infinity hex string invalid");
        h_assert(infinityP.to_decimal_string() == "+Inf", "+ve infinity decimal string invalid");

        // Try converting to native float types
        float infinityPf = (float) infinityP;
        double infinityPd = (double) infinityP;
        h_assert(std::isinf(infinityPf) & !std::signbit(infinityPf),
                 "+ve infinity conversion to float invalid");
        h_assert(std::isinf(infinityPd) & !std::signbit(infinityPd),
                 "+ve infinity conversion to double invalid");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&infinityP) == 0x7c00,
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // -ve infinity
    {
        // Try constructing -ve infinity in different ways and check they all
        // represent the same float16_t
        const float16_t infinityN = float16_t::make_infinity(/*positive=*/false);
        const float16_t infinityNFromFloat( (float) -INFINITY, RoundingMode::ToNearestTiesToEven);
        const float16_t infinityNFromDouble( (double) -INFINITY, RoundingMode::ToNearestTiesToEven);
        h_assert(infinityNFromFloat.to_bits() == infinityN.to_bits(), "Mismatch between constructors");
        h_assert(infinityNFromDouble.to_bits() == infinityN.to_bits(), "Mismatch between constructors");

        // Check the representation
        h_assert(infinityN.is_infinity() && infinityN.is_negative(), "-ve infinity invalid");
        h_assert(infinityN.to_bits() == 0xfc00, "-ve infinity invalid bits");
        h_assert(infinityN.to_hex_string() == "-infinity", "-ve infinity hex string invalid");
        h_assert(infinityN.to_decimal_string() == "-Inf", "-ve infinity decimal string invalid");

        // Try converting to native float types
        float infinityNf = (float) infinityN;
        double infinityNd = (double) infinityN;
        h_assert(std::isinf(infinityNf) & std::signbit(infinityNf),
                 "-ve infinity conversion to float invalid");
        h_assert(std::isinf(infinityNd) & std::signbit(infinityNd),
                 "-ve infinity conversion to double invalid");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&infinityN) == 0xfc00,
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // NaN
    {
        // Try constructing NaN in different ways and check they all
        // represent the same float16_t
        const float16_t nanValue = float16_t::make_nan();
        const float16_t nanValueFromFloat(std::numeric_limits<float>::quiet_NaN(), RoundingMode::ToNearestTiesToEven);
        const float16_t nanValueFromDouble(std::numeric_limits<double>::quiet_NaN(), RoundingMode::ToNearestTiesToEven);
        h_assert(nanValueFromFloat.to_bits() == nanValue.to_bits(), "Mismatch between constructors");
        h_assert(nanValueFromDouble.to_bits() == nanValue.to_bits(), "Mismatch between constructors");

        // Check the representation
        h_assert(nanValue.is_nan(), "NaN invalid");
        // Check exponent is all ones
        h_assert((nanValue.to_bits() & 0x7c00) == 0x7c00, "NaN exponent invalid");
        // Check significand is non zero
        h_assert((nanValue.to_bits() & 0x03ff) > 0, "NaN significant invalid");
        h_assert(nanValue.to_hex_string() == "nan", "NaN hex string invalid");
        h_assert(nanValue.to_decimal_string() == "NaN", "NaN decimal string invalid");

        // Try converting to native float types
        float nanValuef = (float) nanValue;
        double nanValued = (double) nanValue;
        h_assert(std::isnan(nanValuef), "NaN conversion to float invalid");
        h_assert(std::isnan(nanValued), "NaN conversion to float invalid");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&nanValue) == nanValue.to_bits(),
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // Largest +ve
    {
        const float16_t largestNorm("65504");
        const float16_t largestNormFromInt = float16_t::make_from_signed_int(65504);
        h_assert(largestNormFromInt.to_bits() == largestNorm.to_bits(), "make_from_signed_int gave wrong value");
        h_assert(largestNorm.to_bits() == 0x7bff, "65504 as float_16t has wrong bits");
        h_assert(largestNorm.to_hex_string() == "0x1.ffcp15", "65504 as float_16t has wrong hex repr");
        h_assert(largestNorm.to_decimal_string() == "6.5504E+4", "65504 as float_16t has wrong decimal repr");

        // Try converting to native float types
        float largestNormf = (float) largestNorm;
        double largestNormd = (double) largestNorm;
        h_assert(largestNormf == 65504.0f, "Conversion to float failed");
        h_assert(largestNormd == 65504.0, "Conversion to double failed");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&largestNorm) == largestNorm.to_bits(),
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // Largest -ve
    {
        const float16_t largestNorm("-65504");
        const float16_t largestNormFromInt = float16_t::make_from_signed_int(-65504);
        h_assert(largestNormFromInt.to_bits() == largestNorm.to_bits(), "make_from_signed_int gave wrong value");
        h_assert(largestNorm.to_bits() == 0xfbff, "65504 as float_16t has wrong bits");
        h_assert(largestNorm.to_hex_string() == "-0x1.ffcp15", "65504 as float_16t has wrong hex repr");
        h_assert(largestNorm.to_decimal_string() == "-6.5504E+4", "65504 as float_16t has wrong decimal repr");

        // Try converting to native float types
        float largestNormf = (float) largestNorm;
        double largestNormd = (double) largestNorm;
        h_assert(largestNormf == -65504.0f, "Conversion to float failed");
        h_assert(largestNormd == -65504.0, "Conversion to double failed");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&largestNorm) == largestNorm.to_bits(),
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // Smallest +ve
    {
        const float16_t smallestSubNorm("0x0.004p-14");
        h_assert(smallestSubNorm.to_bits() == 0x0001, "smallest number has wrong bits");
        h_assert(smallestSubNorm.to_hex_string() == "0x0.004p-14", "smallest number has wrong hex string");
        h_assert(smallestSubNorm.to_decimal_string() == "5.9605E-8", "smallest number has wrong decimal string");

        // Try converting to native float types
        float smallestSubNormf = (float) smallestSubNorm;
        double smallestSubNormd = (double) smallestSubNorm;
        h_assert(smallestSubNormf == (1.0f)/(1<<24), "conversion to float failed");
        h_assert(smallestSubNormd == (1.0)/(1<<24), "conversion to double failed");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&smallestSubNorm) == smallestSubNorm.to_bits(),
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // Smallest -ve
    {
        const float16_t smallestSubNorm("-0x0.004p-14");
        h_assert(smallestSubNorm.to_bits() == 0x8001, "smallest number has wrong bits");
        h_assert(smallestSubNorm.to_hex_string() == "-0x0.004p-14", "smallest number has wrong hex string");
        h_assert(smallestSubNorm.to_decimal_string() == "-5.9605E-8", "smallest number has wrong decimal string");

        // Try converting to native float types
        float smallestSubNormf = (float) smallestSubNorm;
        double smallestSubNormd = (double) smallestSubNorm;
        h_assert(smallestSubNormf == (-1.0f)/(1<<24), "conversion to float failed");
        h_assert(smallestSubNormd == (-1.0)/(1<<24), "conversion to double failed");

        // Check that directly accessing the bits of the type is correct
        h_assert(*reinterpret_cast<const uint16_t*>(&smallestSubNorm) == smallestSubNorm.to_bits(),
                 "Casting float16_t to uint16_t failed to give right bits");
    }

    // Test the rounding of a few constants

    // 0.1 Cannot be represented exactly in binary
    // Try rounding the decimal representation in different ways
    {
        const float16_t noughtPointOneRZ("0.1", RoundingMode::TowardZero);
        h_assert(noughtPointOneRZ.to_bits() == 0x2e66, "0.1 incorrectly rounded to zero");
        h_assert(noughtPointOneRZ.to_hex_string() == "0x1.998p-4", "0.1 incorrectly rounded to zero");
        h_assert(noughtPointOneRZ.to_decimal_string(0) == "9.9976E-2", "0.1 converted to half then decimal failed");

        // Check can roundtrip convert float16_t to a string and back again
        const float16_t reconstruct(noughtPointOneRZ.to_decimal_string(0).c_str(), RoundingMode::TowardZero);
        h_assert(reconstruct.to_bits() == noughtPointOneRZ.to_bits(), "roundtrip conversion failed");

        // Try round to nearest and round down. For 0.1 this turns out to be the same as rounding
        // to zero
        const float16_t noughtPointOneRNE("0.1", RoundingMode::ToNearestTiesToEven);
        const float16_t noughtPointOneRNA("0.1", RoundingMode::ToNearestTiesToAway);
        const float16_t noughtPointOneRD("0.1", RoundingMode::TowardNegativeInfinity);
        h_assert(noughtPointOneRNE.to_bits() == noughtPointOneRZ.to_bits(), "incorrect rounding");
        h_assert(noughtPointOneRNA.to_bits() == noughtPointOneRZ.to_bits(), "incorrect rounding");
        h_assert(noughtPointOneRD.to_bits() == noughtPointOneRZ.to_bits(), "incorrect rounding");
        // Technically 0.1 is rounded twice (once when compiled and once by
        // float16_t) when using a float or double literal but for this particular
        // example rounding twice does not cause any issues.
        const float16_t noughtPointOneFCast = (float16_t) 0.1f; // Implicitly RNE
        const float16_t noughtPointOneDCast = (float16_t) 0.1; // Implicitly RNE
        const float16_t noughtPointOneExplicitConstructor(0.1f); // Implicitly RNE
        const float16_t noughtPointOneExplicitConstructorStr("0.1"); // Implicitly RNE
        const float16_t noughtPointOneMakeFromBits = float16_t::make_from_bits((uint16_t)0x2e66); // Implicitly RNE
        h_assert(noughtPointOneFCast.to_bits() == noughtPointOneRNE.to_bits(), "cast from float failed");
        h_assert(noughtPointOneDCast.to_bits() == noughtPointOneRNE.to_bits(), "cast from double failed");
        h_assert(noughtPointOneExplicitConstructor.to_bits() == noughtPointOneRNE.to_bits(), "Use of explicit constructor produced bad value");
        h_assert(noughtPointOneExplicitConstructorStr.to_bits() == noughtPointOneRNE.to_bits(), "Use of explicit constructor produced bad value");
        h_assert(noughtPointOneMakeFromBits.to_bits() == noughtPointOneRNE.to_bits(), "Use of explicit constructor produced bad value");


        // Try rounding up
        const float16_t noughtPointOneRU("0.1", RoundingMode::TowardPositiveInfinity);
        h_assert(noughtPointOneRU.to_bits() == 0x2e67, "0.1 incorrectly rounded up");
        h_assert(noughtPointOneRU.to_hex_string() == "0x1.99cp-4", "0.1 incorrectly rounded up");
    }

    // 4091 is an integer that can't be exactly represented in half
    {
        const float16_t fourZeroNineOneRD("4091", RoundingMode::TowardNegativeInfinity);
        const float16_t fourZeroNineOneRDFromInt = float16_t::make_from_signed_int(4091, RoundingMode::TowardNegativeInfinity);
        h_assert(fourZeroNineOneRD.to_bits() == 0x6bfd, "4091 incorreclty rounded down");
        h_assert(fourZeroNineOneRD.to_hex_string() == "0x1.ff4p11", "4091 incorreclty rounded down");
        h_assert(fourZeroNineOneRD.to_decimal_string(0) == "4.09E+3", "4091 converted to half then decimal failed");
        h_assert(fourZeroNineOneRDFromInt.to_bits() == fourZeroNineOneRD.to_bits(), "make_from_signed_int gave wrong value");

        // Check can roundtrip convert float16_t to a string and back again
        const float16_t reconstruct(fourZeroNineOneRD.to_decimal_string(0).c_str(), RoundingMode::TowardNegativeInfinity);
        h_assert(reconstruct.to_bits() == fourZeroNineOneRD.to_bits(), "roundtrip conversion failed");

        const float16_t fourZeroNineOneRU("4091", RoundingMode::TowardPositiveInfinity);
        const float16_t fourZeroNineOneRUFromInt = float16_t::make_from_signed_int(4091, RoundingMode::TowardPositiveInfinity);
        h_assert(fourZeroNineOneRU.to_bits() == 0x6bfe, "4091 incorreclty rounded up");
        h_assert(fourZeroNineOneRU.to_hex_string() == "0x1.ff8p11", "4091 incorreclty rounded up");
        h_assert(fourZeroNineOneRU.to_decimal_string(0) == "4.092E+3", "4091 converted to half then decimal failed");
        h_assert(fourZeroNineOneRUFromInt.to_bits() == fourZeroNineOneRU.to_bits(), "make_from_signed_int gave wrong value");

        const float16_t fourZeroNineOneRZ("4091", RoundingMode::TowardZero);
        const float16_t fourZeroNineOneRZFromInt = float16_t::make_from_signed_int(4091, RoundingMode::TowardZero);
        h_assert(fourZeroNineOneRZ.to_bits() == fourZeroNineOneRD.to_bits(), "4091 incorrectly rounded toward zero");
        h_assert(fourZeroNineOneRZFromInt.to_bits() == fourZeroNineOneRD.to_bits(), "make_from_signed_int gave wrong value");

        const float16_t fourZeroNineOneRNE("4091", RoundingMode::ToNearestTiesToEven);
        const float16_t fourZeroNineOneRNEFromInt = float16_t::make_from_signed_int(4091, RoundingMode::ToNearestTiesToEven);
        h_assert(fourZeroNineOneRNE.to_bits() == fourZeroNineOneRU.to_bits(), "4091 incorrectly rounded towards nearest even");
        h_assert(fourZeroNineOneRNEFromInt.to_bits() == fourZeroNineOneRU.to_bits(), "make_from_signed_int gave wrong value");

        const float16_t fourZeroNineOneRNA("4091", RoundingMode::ToNearestTiesToAway);
        const float16_t fourZeroNineOneRNAFromInt = float16_t::make_from_signed_int(4091, RoundingMode::ToNearestTiesToAway);
        h_assert(fourZeroNineOneRNA.to_bits() == fourZeroNineOneRU.to_bits(), "4091 incorrectly rounded towards nearest, away from zero");
        h_assert(fourZeroNineOneRNAFromInt.to_bits() == fourZeroNineOneRU.to_bits(), "make_from_signed_int gave wrong value");
    }

    // -4091 is an integer that can't be exactly represented in half
    {
        const float16_t fourZeroNineOneRD("-4091", RoundingMode::TowardNegativeInfinity);
        const float16_t fourZeroNineOneRDFromInt = float16_t::make_from_signed_int(-4091, RoundingMode::TowardNegativeInfinity);
        h_assert(fourZeroNineOneRD.to_bits() == 0xebfe, "-4091 incorreclty rounded down");
        h_assert(fourZeroNineOneRD.to_hex_string() == "-0x1.ff8p11", "-4091 incorreclty rounded down");
        h_assert(fourZeroNineOneRD.to_decimal_string(0) == "-4.092E+3", "-4091 converted to half then decimal failed");
        h_assert(fourZeroNineOneRDFromInt.to_bits() == fourZeroNineOneRD.to_bits(), "make_from_signed_int gave wrong value");

        // Check can roundtrip convert float16_t to a string and back again
        const float16_t reconstruct(fourZeroNineOneRD.to_decimal_string(0).c_str(), RoundingMode::TowardNegativeInfinity);
        h_assert(reconstruct.to_bits() == fourZeroNineOneRD.to_bits(), "roundtrip conversion failed");

        const float16_t fourZeroNineOneRU("-4091", RoundingMode::TowardPositiveInfinity);
        const float16_t fourZeroNineOneRUFromInt = float16_t::make_from_signed_int(-4091, RoundingMode::TowardPositiveInfinity);
        h_assert(fourZeroNineOneRU.to_bits() == 0xebfd, "4091 incorreclty rounded up");
        h_assert(fourZeroNineOneRU.to_hex_string() == "-0x1.ff4p11", "-4091 incorreclty rounded up");
        h_assert(fourZeroNineOneRU.to_decimal_string(0) == "-4.09E+3", "-4091 converted to half then decimal failed");
        h_assert(fourZeroNineOneRUFromInt.to_bits() == fourZeroNineOneRU.to_bits(), "make_from_signed_int gave wrong value");

        const float16_t fourZeroNineOneRZ("-4091", RoundingMode::TowardZero);
        const float16_t fourZeroNineOneRZFromInt = float16_t::make_from_signed_int(-4091, RoundingMode::TowardZero);
        h_assert(fourZeroNineOneRZ.to_bits() == fourZeroNineOneRU.to_bits(), "-4091 incorrectly rounded toward zero");
        h_assert(fourZeroNineOneRZFromInt.to_bits() == fourZeroNineOneRU.to_bits(), "make_from_signed_int gave wrong value");

        const float16_t fourZeroNineOneRNE("-4091", RoundingMode::ToNearestTiesToEven);
        const float16_t fourZeroNineOneRNEFromInt = float16_t::make_from_signed_int(-4091, RoundingMode::ToNearestTiesToEven);
        h_assert(fourZeroNineOneRNE.to_bits() == fourZeroNineOneRD.to_bits(), "-4091 incorrectly rounded towards nearest even");
        h_assert(fourZeroNineOneRNEFromInt.to_bits() == fourZeroNineOneRD.to_bits(), "make_from_signed_int gave wrong value");

        const float16_t fourZeroNineOneRNA("-4091", RoundingMode::ToNearestTiesToAway);
        const float16_t fourZeroNineOneRNAFromInt = float16_t::make_from_signed_int(-4091, RoundingMode::ToNearestTiesToAway);
        h_assert(fourZeroNineOneRNA.to_bits() == fourZeroNineOneRD.to_bits(), "-4091 incorrectly rounded towards nearest, away from zero");
        h_assert(fourZeroNineOneRNAFromInt.to_bits() == fourZeroNineOneRD.to_bits(), "make_from_signed_int gave wrong value");
    }

    // 0.3 can't be exactly represented in half
    // This case is slightly different from the 0.1 case because both the "round
    // bit" and "sticky bit" are both 1 (see the Handbook of floating point
    // arithmetic 2.2.1 Rounding modes)
    {
        const float16_t noughtPointThreeRD("0.3", RoundingMode::TowardNegativeInfinity);
        h_assert(noughtPointThreeRD.to_bits() == 0x34cc, "0.3 incorrectly rounded downward");
        h_assert(noughtPointThreeRD.to_hex_string() == "0x1.33p-2", "0.3 incorrectly rounded downward");
        h_assert(noughtPointThreeRD.to_decimal_string(0) == "2.998E-1", "0.3 incorrectly rounded downward");

        // Possible BUG:?? This doesn't work if we use RoundingMode::TowardNegativeInfinity when
        // creating "reconstruct".
        // Check can roundtrip convert float16_t to a string and back again
        const float16_t reconstruct(noughtPointThreeRD.to_decimal_string(0).c_str(), RoundingMode::ToNearestTiesToEven);
        h_assert(reconstruct.to_bits() == noughtPointThreeRD.to_bits(), "roundtrip conversion failed");

        const float16_t noughtPointThreeRU("0.3", RoundingMode::TowardPositiveInfinity);
        h_assert(noughtPointThreeRU.to_bits() == 0x34cd, "0.3 incorrectly rounded upward");
        h_assert(noughtPointThreeRU.to_hex_string() == "0x1.334p-2", "0.3 incorrectly rounded upward");
        h_assert(noughtPointThreeRU.to_decimal_string(0) == "3.0005E-1", "0.3 incorrectly rounded upward");

        const float16_t noughtPointThreeRZ("0.3", RoundingMode::TowardZero);
        h_assert(noughtPointThreeRZ.to_bits() == noughtPointThreeRD.to_bits(), "0.3 incorrectly rounded toward zeron");

        const float16_t noughtPointThreeRNE("0.3", RoundingMode::ToNearestTiesToEven);
        h_assert(noughtPointThreeRNE.to_bits() == noughtPointThreeRU.to_bits(), "0.3 incorrectly rounded toward nearest even");

        const float16_t noughtPointThreeRNA("0.3", RoundingMode::ToNearestTiesToAway);
        h_assert(noughtPointThreeRNA.to_bits() == noughtPointThreeRU.to_bits(), "0.3 incorrectly rounded toward nearest, away from zero");
    }

    printf("Success!\n");
    return 0;
}
