#include "Halide.h"
#include <stdio.h>
#include <cmath>
#include <cinttypes>
#include <vector>
#include "float_helpers.h"

// Get test cases
#include "float16_t_upcast_test_cases.h"

using namespace Halide;

// FIXME: We should use a proper framework for this. See issue #898
void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

// Make input image and expected output image that is filled
// with our special values. We allow it to take any size so
// we can test vectorisation later.
// FIXME: Image class doesn't have Move constructor
// so this will probably be super inefficient!!
std::pair<Image<float16_t>,Image<float>> getInputAndExpectedResultImagesF(unsigned width, unsigned height) {

    Image<float16_t> input(width, height);
    Image<float> expected(width, height);

    auto testCases = get_float16_t_upcast_test_cases();
    int modValue = testCases.size();

    for (unsigned x=0; x < width; ++x) {
        for (unsigned y=0; y < height; ++y) {
            input(x,y) = float16_t::make_from_bits(testCases[ (x +y*width) % modValue].first);
            expected(x,y) = testCases[ (x +y*width) % modValue].second.asFloat;
        }
    }
    return std::make_pair(input, expected);
}

std::pair<Image<float16_t>,Image<double>> getInputAndExpectedResultImagesD(unsigned width, unsigned height) {
    Image<float16_t> input(width, height);
    Image<double> expected(width, height);

    auto testCases = get_float16_t_upcast_test_cases();
    int modValue = testCases.size();

    for (unsigned x=0; x < width; ++x) {
        for (unsigned y=0; y < height; ++y) {
            input(x,y) = float16_t::make_from_bits(testCases[ (x +y*width) % modValue].first);
            expected(x,y) = testCases[ (x +y*width) % modValue].second.asDouble;
        }
    }
    return std::make_pair(input, expected);
}

void checkResult(Image<float>& result, Image<float>& expected) {
    h_assert(result.extent(0) == expected.extent(0), "extend(0) mismatch");
    h_assert(result.extent(1) == expected.extent(1), "extend(1) mismatch");
    // Check result
    for (int x=0; x < result.extent(0); ++x) {
        for (int y=0; y < result.extent(1); ++y) {
            float resultValue = result(x, y);
            float expectedValue = expected(x, y);
            // Convert to bits so we can check NaN
            uint32_t resultValueAsBits = bits_from_float(resultValue);
            uint32_t expectedValueAsBits = bits_from_float(expectedValue);
            if (resultValueAsBits != expectedValueAsBits) {
               printf("Failed to cast correctly: x:%u y:%u\n", x, y);
               printf("resultValueAsBits  : 0x%.8" PRIx32 "\n", resultValueAsBits);
               printf("expectedValueAsBits: 0x%.8" PRIx32 "\n", expectedValueAsBits);
               h_assert(false, "Failed conversion");
            }
        }
    }
}

void checkResult(Image<double>& result, Image<double>& expected) {
    h_assert(result.extent(0) == expected.extent(0), "extend(0) mismatch");
    h_assert(result.extent(1) == expected.extent(1), "extend(1) mismatch");
    // Check result
    for (int x=0; x < result.extent(0); ++x) {
        for (int y=0; y < result.extent(1); ++y) {
            double resultValue = result(x, y);
            double expectedValue = expected(x, y);
            // Convert to bits so we can check NaN
            uint64_t resultValueAsBits = bits_from_double(resultValue);
            uint64_t expectedValueAsBits = bits_from_double(expectedValue);
            if (resultValueAsBits != expectedValueAsBits) {
               printf("Failed to cast correctly: x:%u y:%u\n", x, y);
               printf("resultValueAsBits  : 0x%.16" PRIx64 "\n", resultValueAsBits);
               printf("expectedValueAsBits: 0x%.16" PRIx64 "\n", expectedValueAsBits);
               h_assert(false, "Failed conversion");
            }
        }
    }
}

void testFloatAndDoubleConversion(Target host, int vectorizeWidth=0) {
    int width = 10;
    int height = 10;
    if (vectorizeWidth > 0) {
        // use multiple of vectorization width
        width = 3*vectorizeWidth;
        height = 3*vectorizeWidth;
    }
    // Test conversion to float
    {
        std::pair<Image<float16_t>,Image<float>> tenByTen = getInputAndExpectedResultImagesF(width, height);
        Var x, y;
        Func upCast;
        upCast(x, y) = cast<float>(tenByTen.first(x, y));
        if (vectorizeWidth) {
            upCast.vectorize(x, vectorizeWidth);
        }
        Image<float> result = upCast.realize( { tenByTen.first.width(),
                                                tenByTen.first.height()},
                                              host);
        checkResult(result, tenByTen.second);
        printf("Tested float16 -> float32\n");
    }
    // Test conversion to double
    {
        std::pair<Image<float16_t>,Image<double>> tenByTen = getInputAndExpectedResultImagesD(width, height);
        Var x, y;
        Func upCast;
        upCast(x, y) = cast<double>(tenByTen.first(x, y));
        if (vectorizeWidth) {
            upCast.vectorize(x, vectorizeWidth);
        }
        Image<double> result = upCast.realize( { tenByTen.first.width(),
                                                 tenByTen.first.height()},
                                               host);
        checkResult(result, tenByTen.second);
        printf("Tested float16 -> float64\n");
    }
}

int main() {
    // FIXME: This test only works with x86 right now.
    Target host = get_jit_target_from_environment();
    if (host.arch != Target::X86) {
        printf("FIXME: Running test on other architectures not supported.\n");
        return 0;
    }

    /*
     * Test software implementation of converting float16 to single
     * and double
     */

    // FIXME: This seems a bit cumbersome and fragile, perhaps we should have
    // a softf16c target feature that forces our software implementation to be used?

    // We want to test the software implementation of floating
    // point so remove support from target
    if (host.arch == Target::X86) {
        host.set_feature(Target::F16C, false);
    }
    // TODO: Add code for other architectures to disable their native float16
    // conversion support if they have it

    // Test software implementation of float16 to single/double conversion.
    testFloatAndDoubleConversion(host);

    /*
     * Test hardware implementations of converting float16 to single
     * and double
     */
    host = get_jit_target_from_environment();

    // TODO: Add support for other architectures
    if (host.arch == Target::X86 && host.has_feature(Target::F16C)) {
        // x86-64 f16c intrinsics have 4 and 8 wide versions just try 4
        // for now.
        // FIXME: Is there a way to test that we vectorized correctly?
        printf("Trying vectorization width 4\n");
        testFloatAndDoubleConversion(host, /*vectorizeWidth=*/4);

        // Pick a width that isn't the native size
        // FIXME: This produces wrong results!
        //testFloatAndDoubleConversion(host, /*vectorizeWidth=*/3);

        printf("Trying vectorization width 8\n");
        testFloatAndDoubleConversion(host, /*vectorizeWidth=*/8);

        // Pick a width that isn't the native size
        printf("Trying vectorization width 10\n");
        testFloatAndDoubleConversion(host, /*vectorizeWidth=*/10);

        // Make sure when on F16C we generate correct code even when we don't
        // ask to vectorize
        printf("Trying non vectorized\n");
        testFloatAndDoubleConversion(host, /*vectorizeWidth=*/0);
    } else {
        printf("No target specific float16 support available on target \"%s\"\n",
               host.to_string().c_str());
    }

    printf("Success!\n");
    return 0;
}
