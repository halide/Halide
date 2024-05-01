#include "Halide.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

using namespace Halide;
using Halide::Internal::halide_exp;
using Halide::Internal::halide_extended_exp;

// Compare naive two pass softmax, which will overflow easily, to two
// pass algorithm from "The Two-Pass Softmax Algorithm" by Marat
// Dukhan and Artsiom Ablavatski [https://arxiv.org/abs/2001.04438],
// which is implemented using halide_extended_exp.
void two_pass_softmax_test(float scale) {
    Var x("x");
    RDom r(0, 1024);

    Func input("input");
    input(x) = 0.0f;
    input(r) = random_float() * scale;

    // Naive two pass algorithm. Doesn't work for large values or large size inputs.
    Func in_exp("in_exp");
    in_exp(x) = halide_exp(input(x));
    Func exp_sum("exp_sum");
    exp_sum() = sum(in_exp(r));

    Func naive_softmax("naive_softmax");
    naive_softmax(x) = in_exp(x) / exp_sum();

    // Three pass algorithm that works for all inputs.
    Func max_input("max_input");
    max_input() = maximum(input(r));
    Func biased_in_exp("biased_in_exp");
    biased_in_exp(x) = halide_exp(input(x) - max_input());
    Func biased_exp_sum("biased_exp_sum");
    biased_exp_sum() = sum(biased_in_exp(r));

    Func three_pass_softmax("three_pass_softmax");
    three_pass_softmax(x) = biased_in_exp(x) / biased_exp_sum();

    // Two pass extended exp algorithm.
    Func in_extended_exp("in_extended_exp");
    in_extended_exp(x) = halide_extended_exp(input(x));
    Expr mantissa = in_extended_exp(x)[0];
    Expr exponent = in_extended_exp(x)[1];

    Func extended_exp_sum("extended_exp_sum");
    extended_exp_sum() = Tuple(0.0f, std::numeric_limits<float>::lowest());  // mantissa, exponent
    Expr max_exp = max(extended_exp_sum()[1], in_extended_exp(r)[1]);
    Expr mantissa_sum = in_extended_exp(r)[0] * pow(2, in_extended_exp(r)[1] - max_exp) +
                        extended_exp_sum()[0] * pow(2, extended_exp_sum()[1] - max_exp);
    extended_exp_sum() = Tuple(mantissa_sum, max_exp);

    Expr lambda = 1 / extended_exp_sum()[0];
    Func two_pass_softmax("two_pass_softmax");
    two_pass_softmax(x) = in_extended_exp(x)[0] * lambda * pow(2, in_extended_exp(x)[1] - extended_exp_sum()[1]);

    Func relative_error("relative_error");
    relative_error(x) = abs(three_pass_softmax(x) - two_pass_softmax(x)) / max(.000001f, three_pass_softmax(x));
    Func max_relative_error("max_relative_error");
    max_relative_error() = maximum(relative_error(r));
    Func max_prob("max_prob");
    max_prob() = maximum(two_pass_softmax(r));
    Func min_prob("min_prob");
    min_prob() = minimum(two_pass_softmax(r));
    Func sum_prob("sum_prob");
    sum_prob() = sum(two_pass_softmax(r));

    Func result("result");
    result() = Tuple(max_relative_error(), max_prob(), min_prob(), sum_prob());
    exp_sum.compute_root();
    biased_exp_sum.compute_root();
    extended_exp_sum.compute_root();
    naive_softmax.compute_root();
    three_pass_softmax.compute_root();
    two_pass_softmax.compute_root();

    auto output = result.realize();

    float max_relative_error_result = ((Buffer<float> &)output[0])();
    float max_probability = ((Buffer<float> &)output[1])();
    float min_probability = ((Buffer<float> &)output[2])();
    float sum_probability = ((Buffer<float> &)output[3])();

    if (max_relative_error_result > .0001f) {
        std::cout << "Failed: Softmax results do not match.\n";
        exit(1);
    }

    if (max_probability > 1.0f) {
        std::cout << "Failed: Softmax probability is greater than 1.0f.\n";
        exit(1);
    }

    if (min_probability < 0.0f) {
        std::cout << "Failed: Softmax probability is negative.\n";
        exit(1);
    }

    if (sum_probability > 1.0001f) {
        std::cout << "Failed: Softmax probability sum is too large.\n";
        exit(1);
    }
}

void expect(float x, float mantissa, float exponent) {
    float computed_mantissa;
    float computed_exponent;
    evaluate(halide_extended_exp(x), &computed_mantissa, &computed_exponent);
    if (fabs(computed_mantissa) > exp(1.0f)) {
        std::cout << "Mantissa large for x " << x << " mantissa " << computed_mantissa
                  << " exponent " << computed_exponent << "\n";
    }
    if (fabs(mantissa - computed_mantissa) > .00001 ||
        fabs(exponent - computed_exponent) > .00001) {
        std::cout << "Falied: halide_extended_exp(" << x << ") == {"
                  << computed_mantissa << ", " << computed_exponent
                  << "} expected {"
                  << mantissa << ", " << exponent << "}\n";
        exit(1);
    }
}

int main(int argc, char **argv) {
    std::cout << std::hexfloat;
    expect(0, 1, 0);
    expect(1, exp(1.0f) / 2, 1);
    expect(88, 1.94149, 126);
    expect(0x1.62e43p+23f, 0x1.085012p+0, 0x1p+24);
    expect(std::numeric_limits<float>::lowest(), 1.0f, -std::numeric_limits<float>::infinity());
    expect(std::numeric_limits<float>::max(), 1.0f, std::numeric_limits<float>::infinity());
    two_pass_softmax_test(1.0f);
    two_pass_softmax_test(10000.0f);
    two_pass_softmax_test(-10000.0f);
    two_pass_softmax_test(std::numeric_limits<float>::max());
    two_pass_softmax_test(std::numeric_limits<float>::lowest());
    std::cout << "Success!\n";
}
