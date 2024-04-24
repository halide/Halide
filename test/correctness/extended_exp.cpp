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
    input(r) = random_float();

    Func in_exp("in_exp");
    in_exp(x) = halide_exp(input(x));
    Func exp_sum("exp_sum");
    exp_sum() = sum(in_exp(r));

    Func naive_softmax("naive_softmax");
    naive_softmax(x) = in_exp(x) / exp_sum();

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
    relative_error(x) = abs(naive_softmax(x) - two_pass_softmax(x)) / naive_softmax(x);
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
    extended_exp_sum.compute_root();
    naive_softmax.compute_root();
    two_pass_softmax.compute_root();

    auto output = result.realize();

    float max_relative_error_result = ((Buffer<float> &)output[0])();
    float max_probability = ((Buffer<float> &)output[1])();
    float min_probability = ((Buffer<float> &)output[2])();
    float sum_probability = ((Buffer<float> &)output[3])();

    std::cout << "Two pass softmax with scale " << scale
              << "\nMax relative error: " << max_relative_error_result
              << "\nmax probability: " << max_probability
              << "\nmin probability: " << min_probability
              << "\nsum of probabilities: " << sum_probability << "\n";

    if (max_relative_error_result > .0001f) {
        std::cout << "Failed: Softmax results do not match.\n";
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
    // Implementation does not support these yet.
#if 0
    expect(std::numeric_limits<float>::lowest(), 0, 0);
    expect(std::numeric_limits<float>::max(), 0, 0);
#endif
    two_pass_softmax_test(1.0f);
    two_pass_softmax_test(10000.0f);
    two_pass_softmax_test(-10000.0f);
    two_pass_softmax_test(std::numeric_limits<float>::max());
    two_pass_softmax_test(std::numeric_limits<float>::lowest());
    std::cout << "Success\n";
}
