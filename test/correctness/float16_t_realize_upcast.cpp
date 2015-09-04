#include "Halide.h"
#include <stdio.h>
#include <cmath>
#include <cinttypes>
#include <vector>

using namespace Halide;

// FIXME: We should use a proper framework for this. See issue #898
void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

uint32_t bits_from_float(float v) {
    union {
        float asFloat;
        uint32_t asUInt;
    } out;
    out.asFloat = v;
    return out.asUInt;
}

float float_from_bits(uint32_t bits) {
    union {
        float asFloat;
        uint32_t asUInt;
    } out;
    out.asUInt = bits;
    return out.asFloat;
}

double double_from_bits(uint64_t bits) {
    union {
        double asDouble;
        uint64_t asUInt;
    } out;
    out.asUInt = bits;
    return out.asDouble;
}

uint64_t bits_from_double(double v) {
    union {
        double asDouble;
        uint64_t asUInt;
    } out;
    out.asDouble = v;
    return out.asUInt;
}


uint16_t inputs[] = {
  0x0000, // +ve zero
  0x8000, // -ve zero
  0x7c00, // +ve infinity
  0xfc00, // -ve infinity
  0x7e00, // quiet NaN
  0x7bff, // Largest +ve normal number
  0xfbff, // Smallest -ve normal number
  0x0001, // Smallest +ve subnormal number
  0x8001, // Largest -ve subnormal number
  0x0002, // 2nd smallest +ve subnormal number
  0x8002, // 2nd largest -ve subnormal number
  0x0003, // 3rd smallest subnormal number
  0x03ff, // Largest subnormal
  0x03fe, // 2nd largest subnormal
  0x3c00, // 1.0
  0xbc00  // -1.0
};

// Unfortunately we can't use the C99 hex float format
// here because MSVC doesn't support them
float expectedF[] = {
    0.0f,
    -0.0f,
    INFINITY,
    -INFINITY,
    std::numeric_limits<float>::quiet_NaN(),
    65504.0f,
    -65504.0f,
    (1.0f)/(1<<24),
    (-1.0f)/(1<<24),
    (1.0f)/(1<<23),  // 0x1.000000p-23
    (-1.0f)/(1<<23), // -0x1.000000p-23
    (1.5f)/(1<<23), // 0x1.800000p-23
    float_from_bits(0x387fc000), //0x1.ff8000p-15,
    float_from_bits(0x387f8000), // 0x1.ff0000p-15,
    1.0f,
    -1.0f
};

double expectedD[] = {
    0.0,
    -0.0,
    (double) INFINITY,
    (double) -INFINITY,
    std::numeric_limits<double>::quiet_NaN(),
    65504.0,
    -65504.0,
    (1.0)/(1<<24),
    (-1.0)/(1<<24),
    (1.0)/(1<<23), // 0x1.000000000000p-23
    (-1.0)/(1<<23), // -0x1.000000000000p-23
    (1.5)/(1<<23), // 0x1.800000000000p-23
    double_from_bits(0x3f0ff80000000000), // 0x1.ff8000000000p-15,
    double_from_bits(0x3f0ff00000000000), // 0x1.ff0000000000p-15,
    1.0,
    -1.0
};

// Make input image and expected output image that is filled
// with our special values. We allow it to take any size so
// we can test vectorisation later.
// FIXME: Image class doesn't have Move constructor
// so this will probably be super inefficient!!
std::pair<Image<float16_t>,Image<float>> getInputAndExpectedResultImagesF(unsigned width, unsigned height) {
    static_assert(sizeof(inputs)/sizeof(uint16_t) == sizeof(expectedF)/sizeof(float),
                  "array size mismatch");

    Image<float16_t> input(width, height);
    Image<float> expected(width, height);

    int modValue = sizeof(inputs)/sizeof(uint16_t);

    for (unsigned x=0; x < width; ++x) {
        for (unsigned y=0; y < height; ++y) {
            input(x,y) = float16_t::make_from_bits(inputs[ (x +y*width) % modValue]);
            expected(x,y) = expectedF[ (x +y*width) % modValue];
        }
    }
    return std::make_pair(input, expected);
}

std::pair<Image<float16_t>,Image<double>> getInputAndExpectedResultImagesD(unsigned width, unsigned height) {
    static_assert((sizeof(inputs)/sizeof(uint16_t)) == (sizeof(expectedD)/sizeof(double)),
                  "array size mismatch");

    Image<float16_t> input(width, height);
    Image<double> expected(width, height);

    int modValue = sizeof(inputs)/sizeof(uint16_t);

    for (unsigned x=0; x < width; ++x) {
        for (unsigned y=0; y < height; ++y) {
            input(x,y) = float16_t::make_from_bits(inputs[ (x +y*width) % modValue]);
            expected(x,y) = expectedD[ (x +y*width) % modValue];
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
        Image<double> result = upCast.realize( { tenByTen.first.width(),
                                                 tenByTen.first.height()},
                                               host);
        checkResult(result, tenByTen.second);
        printf("Tested float16 -> float64\n");
    }
}

int main() {
    /*
     * Test software implementation of converting float16 to single
     * and double
     */
    Target host = get_jit_target_from_environment();

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
        testFloatAndDoubleConversion(host, /*vectorizeWidth=*/4);
    } else {
        printf("No target specific float16 support available on target \"%s\"\n",
               host.to_string().c_str());
    }

    printf("Success!\n");
    return 0;
}
