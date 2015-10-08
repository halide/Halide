#include "Halide.h"
#include <cmath>
#include <cinttypes>
#include <stdio.h>
#include <stdlib.h>
#include <limits>
#include <vector>
#include "float_helpers.h"

// Get test cases
#include "float16_t_downcast_test_cases.h"

using namespace Halide;

// FIXME: We should use a proper framework for this. See issue #898
void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}


// Not a method of DownCastedValue because the tests cases
// need to be independent of libHalide.
uint16_t rm_get(DownCastedValue v, RoundingMode rm) {
    switch (rm) {
        case RoundingMode::TowardZero:
            return v.RZ;
        case RoundingMode::ToNearestTiesToEven:
            return v.RNE;
        case RoundingMode::ToNearestTiesToAway:
            return v.RNA;
        case RoundingMode::TowardPositiveInfinity:
            return v.RU;
        case RoundingMode::TowardNegativeInfinity:
            return v.RD;
        default:
            h_assert(false, "Unsupported rounding mode");
    }
    h_assert(false, "Unreachable");
    // Silence GCC warning (-Wreturn-type)
    return 0;
}

std::pair<Image<float>,Image<float16_t>> getInputAndExpectedResultImagesF(unsigned width, unsigned height, RoundingMode rm) {
    Image<float> input(width, height);
    Image<float16_t> expected(width, height);

    Float16ToFloatMap testCases = get_float16_t_downcast_test_cases().first;
    int modValue = testCases.size();

    for (unsigned x=0; x < width; ++x) {
        for (unsigned y=0; y < height; ++y) {
            auto resultPair = testCases[( x + y*width) % modValue ];
            input(x,y) = resultPair.first;
            expected(x,y) = float16_t::make_from_bits(rm_get(resultPair.second, rm));
        }
    }
    return std::make_pair(input, expected);
}

std::pair<Image<double>,Image<float16_t>> getInputAndExpectedResultImagesD(unsigned width, unsigned height, RoundingMode rm) {
    Image<double> input(width, height);
    Image<float16_t> expected(width, height);

    Float16ToDoubleMap testCases = get_float16_t_downcast_test_cases().second;
    int modValue = testCases.size();

    for (unsigned x=0; x < width; ++x) {
        for (unsigned y=0; y < height; ++y) {
            auto resultPair =testCases[( x + y*width) % modValue ];
            input(x,y) = resultPair.first;
            expected(x,y) = float16_t::make_from_bits(rm_get(resultPair.second, rm));
        }
    }
    return std::make_pair(input, expected);
}

template <typename T>
void checkResults(Image<float16_t>& expected, Image<float16_t>& result) {
    h_assert(expected.width() == result.width(), "width mismatch");
    h_assert(expected.height() == result.height(), "height mismatch");

    std::pair<Float16ToFloatMap, Float16ToDoubleMap> expectedResults = get_float16_t_downcast_test_cases();

    // Check results
    for (unsigned x=0; x < static_cast<unsigned>(result.width()); ++x) {
        for (unsigned y=0; y < static_cast<unsigned>(result.height()); ++y) {
            float16_t expectedValue = expected(x, y);
            float16_t resultValue = result(x, y);
            // Need to convert to bits as NaN is not comparable
            uint16_t expectedValueAsBits = expectedValue.to_bits();
            uint16_t resultValueAsBits = resultValue.to_bits();
            if (resultValueAsBits != expectedValueAsBits) {
                printf("Failed to cast correctly: x:%u y:%u\n", x, y);
                printf("resultValueAsBits  : 0x%.4" PRIx16 "\n", resultValueAsBits);
                printf("expectedValueAsBits: 0x%.4" PRIx16 "\n", expectedValueAsBits);

                // Show input and possible outputs
                // FIXME: Getting size this way
                int index = (x + y* result.width()) % expectedResults.first.size();
                printf("data index: %u\n", index);
                DownCastedValue r;
                if (sizeof(T) == sizeof(float)) {
                    std::pair<float,DownCastedValue> resultPair = expectedResults.first[index];
                    r = resultPair.second;
                    printf("Input: 0x%.8" PRIx32 "(~%f)\n", bits_from_float(resultPair.first), resultPair.first);
                } else {
                    h_assert(sizeof(T) == sizeof(double), "wrong type?");
                    std::pair<double,DownCastedValue> resultPair = expectedResults.second[index];
                    r = resultPair.second;
                    printf("Input: 0x%.16" PRIx64 "(~%f)\n", bits_from_double(resultPair.first), resultPair.first);
                }
                printf("Expected result as RZ: 0x%.4" PRIx16 "\n", r.RZ);
                printf("Expected result as RU: 0x%.4" PRIx16 "\n", r.RU);
                printf("Expected result as RD: 0x%.4" PRIx16 "\n", r.RD);
                printf("Expected result as RNE: 0x%.4" PRIx16 "\n", r.RNE);
                printf("Expected result as RNA: 0x%.4" PRIx16 "\n", r.RNA);
                h_assert(false, "Failed conversion");
            }
        }
    }
}

void testFloatSingleRoundingMode(Target host,
                                 unsigned width,
                                 unsigned height,
                                 int vectorizeWidth,
                                 RoundingMode rm) {
    std::pair<Image<float>,Image<float16_t>> img = getInputAndExpectedResultImagesF(
        width,
        height,
        rm);

    Var x, y;
    Func downCast;
    downCast(x, y) = cast<float16_t>(img.first(x, y), rm);
    if (vectorizeWidth) {
        downCast.vectorize(x, vectorizeWidth);
    }
    Image<float16_t> result = downCast.realize( {img.first.width(), img.first.height()},
                                                host);
    // Check results
    checkResults<float>(img.second, result);
}


void testDoubleSingleRoundingMode(Target host,
                                 unsigned width,
                                 unsigned height,
                                 int vectorizeWidth,
                                 RoundingMode rm) {
    std::pair<Image<double>,Image<float16_t>> img = getInputAndExpectedResultImagesD(
        width,
        height,
        rm);

    Var x, y;
    Func downCast;
    downCast(x, y) = cast<float16_t>(img.first(x, y), rm);
    if (vectorizeWidth) {
        downCast.vectorize(x, vectorizeWidth);
    }
    Image<float16_t> result = downCast.realize( {img.first.width(), img.first.height()},
                                                host);
    // Check results
    checkResults<double>(img.second, result);
}

void testFloatAndDoubleConversion(Target host, DownCastedValue modes, bool testDoubleConv, int vectorizeWidth=0) {
    int width = 10;
    int height = 10;
    if (vectorizeWidth > 0) {
        // use multiple of vectorization width
        width = 3*vectorizeWidth;
        height = 3*vectorizeWidth;
    }

    // Test float
    printf("Testing float -> float16\n");
    if (modes.RZ > 0) {
        printf("Testing RZ\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::TowardZero);
    } else {
        printf("Skipping RZ\n");
    }

    if (modes.RNE > 0) {
        printf("Testing RNE\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::ToNearestTiesToEven);
    } else {
        printf("Skipping RNE\n");
    }
    if (modes.RNA > 0) {
        printf("Testing RNA\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::ToNearestTiesToAway);
    } else {
        printf("Skipping RNA\n");
    }
    if (modes.RU > 0) {
        printf("Testing RU\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::TowardPositiveInfinity);
    } else {
        printf("Skipping RU\n");
    }
    if (modes.RD > 0) {
        printf("Testing RD\n");
        testFloatSingleRoundingMode(host,
                                    width,
                                    height,
                                    vectorizeWidth,
                                    RoundingMode::TowardNegativeInfinity);
    } else {
        printf("Skipping RD\n");
    }

    // Test double
    if (testDoubleConv) {
        printf("Testing double -> float16\n");
        if (modes.RZ > 0) {
            printf("Testing RZ\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::TowardZero);
        } else {
            printf("Skipping RZ\n");
        }

        if (modes.RNE > 0) {
            printf("Testing RNE\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::ToNearestTiesToEven);
        } else {
            printf("Skipping RNE\n");
        }
        if (modes.RNA > 0) {
            printf("Testing RNA\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::ToNearestTiesToAway);
        } else {
            printf("Skipping RNA\n");
        }
        if (modes.RU > 0) {
            printf("Testing RU\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::TowardPositiveInfinity);
        } else {
            printf("Skipping RU\n");
        }
        if (modes.RD > 0) {
            printf("Testing RD\n");
            testDoubleSingleRoundingMode(host,
                                        width,
                                        height,
                                        vectorizeWidth,
                                        RoundingMode::TowardNegativeInfinity);
        } else {
            printf("Skipping RD\n");
        }
    } else {
        printf("Skipping double -> float16\n");
    }
}

int main(){
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

    // Test all rounding modes, we abuse DownCastedValue here to indicate
    // the rounding modes we wish to test.
    DownCastedValue roundingModesToTest = DownCastedValue::all(0x0001);
    testFloatAndDoubleConversion(host, roundingModesToTest, /*testDoubleConv=*/true);

    /*
     * Try to test hardware implementations of converting single and double to
     * float16
     */
    host = get_jit_target_from_environment();
    if (host.arch == Target::X86 && host.has_feature(Target::F16C)) {
        roundingModesToTest = {
            .RZ = 1,
            .RU = 1,
            .RD = 1,
            .RNE = 1,
            .RNA = 0, // Not supported by vcvtps2ph
        };

        // RNA from float and * from double are not supported in hardware but
        // because no vectorization is used it should fall back to the software
        // implementation in those cases
        printf("Trying no vectorization\n");
        testFloatAndDoubleConversion(host,
                                     DownCastedValue::all(0x0001), // Test all rounding modes
                                     /*testDoubleConv=*/true,
                                     /*vectorizeWidth=*/0);

        printf("Trying vectorization width 4\n");
        // Note: No native support for "double -> float16" if vectorizing
        testFloatAndDoubleConversion(host,
                                     roundingModesToTest,
                                     /*testDoubleConv=*/ false,
                                     /*vectorizeWidth=*/4);

        // FIXME: Gives wrong results under LLVM3.6
        //printf("Trying vectorization width 3\n");
        // Note: No native support for "double -> float16" if vectorizing
        //testFloatAndDoubleConversion(host,
        //                             roundingModesToTest,
        //                             /*testDoubleConv=*/ false,
        //                             /*vectorizeWidth=*/3);

        printf("Trying vectorization width 8\n");
        // Note: No native support for "double -> float16" if vectorizing
        testFloatAndDoubleConversion(host,
                                     roundingModesToTest,
                                     /*testDoubleConv=*/ false,
                                     /*vectorizeWidth=*/8);

        printf("Trying vectorization width 10\n");
        // Note: No native support for "double -> float16" if vectorizing
        testFloatAndDoubleConversion(host,
                                     roundingModesToTest,
                                     /*testDoubleConv=*/ false,
                                     /*vectorizeWidth=*/10);
    }
    printf("Success!\n");
    return 0;
}
