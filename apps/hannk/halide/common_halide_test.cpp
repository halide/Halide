#include "halide/common_halide.h"

using namespace Halide;
using namespace hannk;

Var x("x"), y("y");

double relative_error(double x, double y) {
    return std::abs(x - y) / std::max({std::abs(x), std::abs(y), 1e-6});
}

template<typename T>
double saturate(double x) {
    return std::min<double>(std::max<double>(x, std::numeric_limits<T>::min()), std::numeric_limits<T>::max());
}

template<typename T>
bool test_approx_log2() {
    const int extent = 50000;
    const int scale = std::numeric_limits<int>::max() / extent;
    const int log2_precisions[] = {0, 1, 2, 3, 8, 15};

    Func test("test_log2");
    std::vector<Expr> tests;
    for (int i : log2_precisions) {
        tests.push_back(approx_log2(i, (x + 1) * scale, 0, halide_type_of<T>()));
    }
    test(x) = Tuple(tests);

    const double relative_tolerance = 1e-3;
    const double absolute_tolerance = 2;

    auto results = test.realize({extent});
    const int log2_precisions_size = sizeof(log2_precisions) / sizeof(log2_precisions[0]);
    for (int z = 0; z < log2_precisions_size; z++) {
        Buffer<T, 1> result = results[z];
        const int log2_precision = log2_precisions[z];
        const double precision = 1 << log2_precision;
        for (int x = 0; x < result.width(); x++) {
            const double exact_x = (x + 1) * scale;
            const double exact = std::round(std::log2(exact_x) * precision);
            if (std::numeric_limits<T>::min() <= exact && exact <= std::numeric_limits<T>::max()) {
                const double result_x = result(x);
                if (relative_error(exact, result_x) > relative_tolerance &&
                    std::abs(exact - result_x) > absolute_tolerance) {
                    std::cout << "approx_log2(" << exact_x << ", " << log2_precision << "): "
                              << exact << " !~= " << result_x << "\n";
                    return false;
                }
            } else {
                // The result would have overflowed.
            }
        }
    }
    return true;
}

template<typename T>
bool test_approx_exp2() {
    const int extent = 50000;
    const int offset = extent / 2;
    const int scale = std::numeric_limits<T>::max() / offset;
    const int log2_precision_results[] = {0, 1, 2, 3, 8, 15};

    Func test("test_exp2");
    std::vector<Expr> tests;
    for (int i : log2_precision_results) {
        tests.push_back(approx_exp2(i, (x - offset) * scale, y, halide_type_of<T>()));
    }
    test(x, y) = Tuple(tests);

    const double relative_tolerance = 1e-3;
    const double absolute_tolerance = 1.0;

    auto results = test.realize({extent, 15});
    const int log2_precision_results_size =
        sizeof(log2_precision_results) / sizeof(log2_precision_results[0]);
    for (int z = 0; z < log2_precision_results_size; z++) {
        Buffer<T, 1> result = results[z];
        const int log2_precision_result = log2_precision_results[z];
        const double precision_result = 1 << log2_precision_result;
        for (int y = 0; y < result.height(); y++) {
            const double precision_x = 1 << y;
            for (int x = 0; x < result.width(); x++) {
                const double exact_x = (x - offset) * scale / precision_x;
                const double exact = std::round(std::exp2(exact_x) * precision_result);
                if (std::numeric_limits<T>::min() <= exact && exact <= std::numeric_limits<T>::max()) {
                    const double result_xy = result(x, y);
                    if (relative_error(exact, result_xy) > relative_tolerance &&
                        std::abs(exact - result_xy) > absolute_tolerance) {
                        std::cout << "approx_exp2(" << exact_x << ", " << y << ", " << log2_precision_result << "): "
                                  << exact << " !~= " << result_xy << "\n";
                        return false;
                    }
                } else {
                    // The result would have overflowed.
                }
            }
        }
    }
    return true;
}

template<typename T>
bool test_approx_log2p1_exp2() {
    const int extent = 5000;
    const int offset = extent / 2;
    const int scale = std::numeric_limits<T>::max() / offset;
    const int log2_precision_results[] = {8};

    Func test("test_log2p1_exp2");
    std::vector<Expr> tests;
    for (int i : log2_precision_results) {
        tests.push_back(approx_log2p1_exp2(i, (x - offset) * scale, y, halide_type_of<T>()));
    }
    test(x, y) = Tuple(tests);

    const double relative_tolerance = 1e-3;
    const double absolute_tolerance = 2.0;

    auto results = test.realize({extent, 8 * sizeof(T)});
    const int log2_precision_results_size =
        sizeof(log2_precision_results) / sizeof(log2_precision_results[0]);
    for (int z = 0; z < log2_precision_results_size; z++) {
        Buffer<T, 2> result = results[z];
        const int log2_precision_result = log2_precision_results[z];
        const double precision_result = 1 << log2_precision_result;
        for (int y = 0; y < result.height(); y++) {
            const double precision_x = 1 << y;
            for (int x = 0; x < result.width(); x++) {
                const double exact_x = (x - offset) * scale / precision_x;
                const double exact = saturate<T>(std::round(precision_result * std::log2(1.0 + std::exp2(exact_x))));
                const double result_xy = result(x, y);
                if (relative_error(exact, result_xy) > relative_tolerance &&
                    std::abs(exact - result_xy) > absolute_tolerance) {
                    std::cout << "approx_log2p1_exp2(" << exact_x << ", " << y << ", " << log2_precision_result << "): "
                              << exact << " !~= " << result_xy << "\n";
                    return false;
                }
            }
        }
    }
    return true;
}

template<typename T>
bool test_approx_log2m1_exp2() {
    const int extent = 5000;
    const int scale = std::numeric_limits<T>::max() / extent;
    const int log2_precision_results[] = {8};

    Func test("test_log2m1_exp2");
    std::vector<Expr> tests;
    for (int i : log2_precision_results) {
        tests.push_back(approx_log2p1_exp2(i, max(1, x * scale), y, halide_type_of<T>()));
    }
    test(x, y) = Tuple(tests);

    const double relative_tolerance = 1e-3;
    const double absolute_tolerance = 2.0;

    auto results = test.realize({extent, 8 * sizeof(T)});
    const int log2_precision_results_size =
        sizeof(log2_precision_results) / sizeof(log2_precision_results[0]);
    for (int z = 0; z < log2_precision_results_size; z++) {
        Buffer<T, 2> result = results[z];
        const int log2_precision_result = log2_precision_results[z];
        const double precision_result = 1 << log2_precision_result;
        for (int y = 0; y < result.height(); y++) {
            const double precision_x = 1 << y;
            for (int x = 0; x < result.width(); x++) {
                const double exact_x = std::max(1, x * scale) / precision_x;
                const double exact = saturate<T>(std::round(precision_result * std::log2(1.0 - std::exp2(exact_x))));
                const double result_xy = result(x, y);
                if (relative_error(exact, result_xy) > relative_tolerance &&
                    std::abs(exact - result_xy) > absolute_tolerance) {
                    std::cout << "approx_log2m1_exp2(" << exact_x << ", " << y << ", " << log2_precision_result << "): "
                              << exact << " !~= " << result_xy << "\n";
                    return false;
                }
            }
        }
    }
    return true;
}

template<typename T>
bool test_approx_logistic() {
    const int extent = 5000;
    const int offset = extent / 2;
    const int scale = std::numeric_limits<T>::max() / offset;
    const int log2_precision_results[] = {8, 15};

    Func test("test_logistic");
    std::vector<Expr> tests;
    for (int i : log2_precision_results) {
        tests.push_back(approx_logistic(i, (x - offset) * scale, y, halide_type_of<T>()));
    }
    test(x, y) = Tuple(tests);

    const double relative_tolerance = 1e-1;

    auto results = test.realize({extent, 8 * sizeof(T)});
    const int log2_precision_results_size =
        sizeof(log2_precision_results) / sizeof(log2_precision_results[0]);
    for (int z = 0; z < log2_precision_results_size; z++) {
        Buffer<T, 2> result = results[z];
        const int log2_precision_result = log2_precision_results[z];
        const double precision_result = 1 << log2_precision_result;
        const double absolute_tolerance = precision_result / 128;
        for (int y = 0; y < result.height(); y++) {
            const double precision_x = 1 << y;
            for (int x = 0; x < result.width(); x++) {
                const double exact_x = (x - offset) * scale / precision_x;
                if (exact_x > std::numeric_limits<T>::max() / 2) {
                    // We can't scale by log2(e) without losing a bit.
                    continue;
                }
                const double exact = std::round(precision_result / (1.0 + std::exp(-exact_x)));
                const double result_xy = result(x, y);
                if (relative_error(exact, result_xy) > relative_tolerance &&
                    std::abs(exact - result_xy) > absolute_tolerance) {
                    std::cout << "approx_logistic(" << exact_x << ", " << y << ", " << log2_precision_result << "): "
                              << exact << " !~= " << result_xy << "\n";
                    return false;
                }
            }
        }
    }
    return true;
}

template<typename T>
bool test_approx_tanh() {
    const int extent = 5000;
    const int offset = extent / 2;
    const int scale = std::numeric_limits<T>::max() / offset;
    const int log2_precision_results[] = {7, 15};

    Func test("test_tanh");
    std::vector<Expr> tests;
    for (int i : log2_precision_results) {
        tests.push_back(approx_tanh(i, (x - offset) * scale, y, halide_type_of<T>()));
    }
    test(x, y) = Tuple(tests);

    const double relative_tolerance = 1e-1;

    auto results = test.realize({extent, 8 * sizeof(T)});
    const int log2_precision_results_size =
        sizeof(log2_precision_results) / sizeof(log2_precision_results[0]);
    for (int z = 0; z < log2_precision_results_size; z++) {
        Buffer<T, 2> result = results[z];
        const int log2_precision_result = log2_precision_results[z];
        const double precision_result = 1 << log2_precision_result;
        const double absolute_tolerance = std::max(3.0, precision_result / 512);
        for (int y = 0; y < result.height(); y++) {
            const double precision_x = 1 << y;
            for (int x = 0; x < result.width(); x++) {
                const double exact_x = (x - offset) * scale / precision_x;
                const double exact = std::round(precision_result * std::tanh(exact_x));
                const double result_xy = result(x, y);
                if (relative_error(exact, result_xy) > relative_tolerance &&
                    std::abs(exact - result_xy) > absolute_tolerance) {
                    std::cout << "approx_tanh(" << exact_x << ", " << y << ", " << log2_precision_result << "): "
                              << exact << " !~= " << result_xy << "\n";
                    return false;
                }
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (!test_approx_log2<int16_t>()) {
        return -1;
    }
    if (!test_approx_log2<int32_t>()) {
        return -1;
    }

    if (!test_approx_exp2<int16_t>()) {
        return -1;
    }
    if (!test_approx_exp2<int32_t>()) {
        return -1;
    }

    if (!test_approx_log2p1_exp2<int16_t>()) {
        return -1;
    }
    if (!test_approx_log2m1_exp2<int16_t>()) {
        return -1;
    }

    if (!test_approx_logistic<int16_t>()) {
        return -1;
    }

    if (!test_approx_tanh<int16_t>()) {
        return -1;
    }

    std::cout << "Success!\n";
    return 0;
}