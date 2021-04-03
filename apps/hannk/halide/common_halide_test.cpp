#include "halide/common_halide.h"

using namespace Halide;
using namespace hannk;

Var x("x"), y("y");

double relative_error(double x, double y) {
    return std::abs(x - y) / std::max({x, y, 1e-6});
}

bool test_approx_exp2() {
    const int extent = 32768;
    const int offset = extent / 2;
    const int scale = std::numeric_limits<int>::max() / offset;
    const int log2_precision_results[] = {0, 1, 2, 3, 8, 16};

    Func test("test");
    std::vector<Expr> tests;
    for (int i : log2_precision_results) {
        tests.push_back(approx_exp2((x - offset) * scale, y, i));
    }
    test(x, y) = Tuple(tests);

    const double tolerance = 1e-1;

    auto results = test.realize({extent, 31});
    int log2_precision_results_size =
        sizeof(log2_precision_results) / sizeof(log2_precision_results[0]);
    for (int z = 0; z < log2_precision_results_size; z++) {
        Buffer<int> result = results[z];
        int log2_precision_result = log2_precision_results[z];
        double precision_result = 1 << log2_precision_result;
        for (int y = 0; y < result.height(); y++) {
            double precision_x = 1 << y;
            for (int x = 0; x < result.width(); x++) {
                double exact_x = (x - offset) * scale / precision_x;
                double exact = std::round(std::exp2(exact_x) * precision_result);
                if (std::numeric_limits<int>::min() <= exact && exact <= std::numeric_limits<int>::max()) {
                    double result_xy = result(x, y);
                    if (relative_error(exact, result_xy) > tolerance && std::abs(exact - result_xy) > 1) {
                        std::cout << "Failure(" << exact_x << " " << y << " " << log2_precision_result << "): " << exact << " !~= " << result_xy << "\n";
                    }
                } else {
                    // The result would have overflowed.
                }
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (!test_approx_exp2()) {
        return -1;
    }

    return 0;
}