#include <cmath>

#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

template<typename T>
inline void check(int line_number, T x, T target, T threshold = T(1e-6)) {
    _halide_user_assert(std::fabs((x) - (target)) < threshold)
        << "Line " << line_number << ": Expected " << (target) << " instead of " << (x) << "\n";
}

inline void check(int line_number, float16_t x, float16_t target) {
    return check(line_number, (double)x, (double)target, 5e-3);
}

/**
 *  Check all dependencies of func, return true if any of the dependent func
 *  uses non pure variables on left hand side
 */
bool has_non_pure_update(const Func &func) {
    std::map<std::string, Function> env = find_transitive_calls(func.function());
    std::vector<std::string> order =
        realization_order({ func.function() }, env).first;
    for (const auto &func_name : order) {
        Func func(env[func_name]);
        // For each update
        for (int id = 0; id < func.num_update_definitions(); id++) {
            // For each argument on left hand side
            const std::vector<Expr> &args = func.update_args(id);
            for (Expr arg : args) {
                if (arg.as<Variable>() == nullptr) {
                    return true;
                }
            }
        }
    }
    return false;
}

template<typename T>
void test_scalar() {
    {  // Test + - * / const
        Func x("x");
        x() = Expr(T(5));
        Func y("y");
        y() = x() * x() - Expr(T(2)) * x() + Expr(T(5)) + Expr(T(3)) / x();
        Derivative d = propagate_adjoints(y);
        Func dx = d(x);
        Buffer<T> dydx = dx.realize();
        // y = x^2 - 2x + 5 + 3 / x
        // dydx = 2x - 2 - 3 / x^2 = 12 - 3 / 25
        check(__LINE__, dydx(0), T(8.0 - 3.0 / 25.0));
    }
    {  // Test special functions
        Func x("x");
        x() = Expr(T(0.5));
        Func y("y");
        y() = sin(x()) +
              cos(x()) +
              tan(x()) +
              exp(x()) +
              log(x()) +
              sqrt(x()) +
              pow(x(), Expr(T(1.5))) +
              pow(Expr(T(1.5)), x()) +
              asin(x()) +
              Expr(T(1.2)) * acos(x()) +
              atan(x()) +
              atan2(x(), Expr(T(2))) +
              Expr(T(1.3)) * atan2(Expr(T(2)), x()) +
              sinh(x()) +
              Expr(T(1.2)) * cosh(x()) +
              tanh(x()) +
              asinh(x()) +
              acosh(x() + Expr(T(1))) +
              atanh(x());
        Derivative d = propagate_adjoints(y);
        Buffer<T> dydx = d(x).realize();
        // dydx = cos(x) -
        //        sin(x) +
        //        1 / cos(x)^2 +
        //        exp(x) +
        //        1/x +
        //        1 / (2 sqrt(x)) +
        //        1.5 * x^0.5 +
        //        (1.5^x) * log(1.5) +
        //        1 / sqrt(1 - x^2) -
        //        1.2 / sqrt(1 - x^2) +
        //        1 / (x^2 + 1) +
        //        2.f / (4.f + x^2) -
        //        x / (4.f + x^2) +
        //        cosh(x) +
        //        1.2 * sinh(x) +
        //        tanh(x) +
        //        1 / sqrt(x^2 + 1) +
        //        1 / (sqrt(x - 1) * sqrt(x + 1)) +
        //        1 / (1 - x^2)
        check(__LINE__, dydx(0),
              T(std::cos(0.5f) -
                std::sin(0.5f) +
                (1.f / (std::cos(0.5f) * std::cos(0.5f))) +
                std::exp(0.5f) +
                1.f / 0.5f +
                1.f / (2.f * std::sqrt(0.5f)) +
                1.5f * std::pow(0.5f, 0.5f) +
                std::log(1.5f) * std::pow(1.5f, 0.5f) +
                1.f / std::sqrt(1.f - 0.5f * 0.5f) -
                1.2f / std::sqrt(1.f - 0.5f * 0.5f) +
                (1.f / (0.5f * 0.5f + 1.f)) +
                2.f / (4.f + 0.5f * 0.5f) -
                1.3f * 2.f / (4.f + 0.5f * 0.5f) +
                std::cosh(0.5f) +
                1.2f * std::sinh(0.5f) +
                1.f / (std::cosh(0.5f) * std::cosh(0.5f)) +
                1.f / std::sqrt(0.5f * 0.5f + 1.f) +
                1.f / (std::sqrt(0.5f) * std::sqrt(2.5f)) +
                1.f / (1.f - 0.5f * 0.5f)));
    }
    {  // Test fast inv
        Func x("x");
        x() = 2.5f;
        Func y("y");
        y() = fast_inverse(x()) + fast_inverse_sqrt(x());
        Derivative d = propagate_adjoints(y);
        Buffer<float> dydx = d(x).realize();
        // dy/dx = -1/x^2 - 1/(2*x^(3/2))
        check(__LINE__, dydx(0), -1.f / (2.5f * 2.5f) - 1.f / (2.f * std::pow(2.5f, 3.f / 2.f)), 1e-3f);
    }
    {  // Test floor ceil round trunc
        Func x("x");
        x() = Expr(T(2.5));
        Func y("y");
        y() = ceil(x()) + floor(x()) + round(x()) + trunc(x());
        Derivative d = propagate_adjoints(y);
        Buffer<T> dydx = d(x).realize();
        check(__LINE__, dydx(0), T(0));
    }
    {  // Test max min
        Func x("x");
        x() = Expr(T(2.5));
        Func y("y");
        y() = Expr(T(2)) * max(x(), Expr(T(5))) +
              Expr(T(3)) * max(x(), Expr(T(1))) +
              Expr(T(5)) * min(x(), Expr(T(3))) +
              Expr(T(7)) * min(x(), Expr(T(2)));
        Derivative d = propagate_adjoints(y);
        Buffer<T> dydx = d(x).realize();
        check(__LINE__, dydx(0), T(8));
    }
    {  // Test abs
        Func x("x");
        x() = Expr(T(-2.5));
        Func y("y");
        y() = Expr(T(2)) * abs(x()) + Expr(T(3)) * abs(-x());
        Derivative d = propagate_adjoints(y);
        Buffer<T> dydx = d(x).realize();
        // y = -2x - 3x = -5x, dy/dx = -5
        check(__LINE__, dydx(0), T(-5));
    }
    {  // Test select
        Func x("x");
        x() = Expr(T(5));
        Func y("y");
        y() = select(x() > Expr(T(0)), Expr(T(2)) * x(), Expr(T(3)) * x()) +
              select(x() < Expr(T(0)), Expr(T(5)) * x(), Expr(T(7)) * x());
        Derivative d = propagate_adjoints(y);
        Buffer<T> dydx = d(x).realize();
        check(__LINE__, dydx(0), T(9));
    }
    {  // Test lerp
        Func x("x");
        x() = Expr(T(2));
        Func y("y");
        y() = Expr(T(6));
        Func w("w");
        w() = Expr(T(0.1));
        Func z("z");
        // z = x * (1 - w) + y * w
        z() = lerp(x(), y(), w());
        Derivative d = propagate_adjoints(z);
        // dzdx = 1 - w
        Buffer<T> dzdx = d(x).realize();
        check(__LINE__, dzdx(0), T(0.9));
        // dzdy = w
        Buffer<T> dzdy = d(y).realize();
        check(__LINE__, dzdy(0), T(0.1));
        // dzdw = y - x
        Buffer<T> dzdw = d(w).realize();
        check(__LINE__, dzdw(0), T(4.0));
    }
}

void test_1d_box_no_clamp() {
    Var x("x");
    Buffer<float> input(3);
    input(0) = 1.f;
    input(1) = 2.f;
    input(2) = 3.f;
    Func blur("blur");
    blur(x) = input(x) + input(x + 1);
    RDom r(0, 2);
    Func f_loss("f_loss");
    f_loss() += blur(r.x) * blur(r.x);
    Derivative d = propagate_adjoints(f_loss);

    Buffer<float> blur_buf = blur.realize(2);
    // d loss / d blur = 2 * blur(x)
    Buffer<float> d_blur_buf = d(blur).realize(2);
    check(__LINE__, d_blur_buf(0), 2 * blur_buf(0));
    check(__LINE__, d_blur_buf(1), 2 * blur_buf(1));
    // d input(x) = d blur(x) + d blur(x - 1)
    Func d_input = d(input);
    // Every dependency of d_input should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_input)) << "Function has non pure update\n";
    Buffer<float> d_input_buf = d_input.realize(3);
    check(__LINE__, d_input_buf(0), d_blur_buf(0));
    check(__LINE__, d_input_buf(1), d_blur_buf(0) + d_blur_buf(1));
    check(__LINE__, d_input_buf(2), d_blur_buf(1));
}

void test_1d_box() {
    Var x("x");
    Buffer<float> input(2);
    input(0) = 1.f;
    input(1) = 2.f;
    Func clamped("clamped");
    Expr clamped_x = Halide::clamp(x, 0, input.width() - 1);
    clamped(x) = input(clamped_x);
    Func blur("blur");
    blur(x) = clamped(x) + clamped(x + 1);
    RDom r(0, 2);
    Func f_loss("f_loss");
    f_loss() += blur(r.x) * blur(r.x);
    Derivative d = propagate_adjoints(f_loss);

    Buffer<float> blur_buf = blur.realize(2);
    // d loss / d blur = 2 * blur(x)
    Buffer<float> d_blur_buf = d(blur).realize(2);
    check(__LINE__, d_blur_buf(0), 2 * blur_buf(0));
    check(__LINE__, d_blur_buf(1), 2 * blur_buf(1));
    // d clamped(x) = d blur(x) + d blur(x - 1)
    Func d_clamped = d(clamped);
    // Every dependency of d_clamped should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_clamped)) << "Function has non pure update\n";
    Buffer<float> d_clamped_buf = d_clamped.realize(3);
    check(__LINE__, d_clamped_buf(0), d_blur_buf(0));
    check(__LINE__, d_clamped_buf(1), d_blur_buf(0) + d_blur_buf(1));
    check(__LINE__, d_clamped_buf(2), d_blur_buf(1));
    // d input(clamp(x, 0, 1)) = d clamped (x)
    Buffer<float> d_input_buf = d(input).realize(2);
    check(__LINE__, d_input_buf(0), d_clamped_buf(0));
    check(__LINE__, d_input_buf(1), d_clamped_buf(1) + d_clamped_buf(2));
}

void test_2d_box() {
    Var x("x"), y("y");
    Buffer<float> input(5, 5, "input");
    for (int i = 0; i < input.width(); i++) {
        for (int j = 0; j < input.height(); j++) {
            input(i, j) = (i + 1) * (j + 2);
        }
    }
    Func clamped("clamped");
    Expr clamped_x = Halide::clamp(x, 0, input.width() - 1);
    Expr clamped_y = Halide::clamp(y, 0, input.height() - 1);
    clamped(x, y) = input(clamped_x, clamped_y);
    Func blur_x("blur_x");
    blur_x(x, y) = clamped(x, y) + clamped(x + 1, y) + clamped(x + 2, y);
    Func blur_y("blur_y");
    blur_y(x, y) = blur_x(x, y - 1) + blur_x(x, y) + blur_x(x, y + 1);

    RDom r(0, 5, 0, 5);
    Func loss("loss");
    loss() += blur_y(r.x, r.y) * blur_y(r.x, r.y);
    Derivative d = propagate_adjoints(loss);

    Buffer<float> blur_y_buf = blur_y.realize(5, 5);
    // d loss / d blur_y = 2 * blur_y(x, y)
    Buffer<float> d_blur_y_buf = d(blur_y).realize(5, 5);
    const float eps = 1e-6;
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            float target = 2 * blur_y_buf(x, y);
            float diff = fabs(d_blur_y_buf(x, y) - target);
            _halide_user_assert(diff < eps)
                << "Expected d_blur_y(" << x << ", " << y << ") to be " << target << " instead of " << d_blur_y_buf(x, y) << "\n";
        }
    }
    // d loss / d blur_x = d blur_y(x, y) + d blur_y(x, y - 1) + d blur_y(x, y + 1)
    Buffer<float> d_blur_x_buf = d(blur_x).realize(5, 5);
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            float target = d_blur_y_buf(x, y);
            if (y >= 1) {
                target += d_blur_y_buf(x, y - 1);
            }
            if (y < 4) {
                target += d_blur_y_buf(x, y + 1);
            }
            float diff = fabs(d_blur_x_buf(x, y) - target);
            _halide_user_assert(diff < eps)
                << "Expected d_blur_x(" << x << ", " << y << ") to be " << target << " instead of " << d_blur_x_buf(x, y) << "\n";
        }
    }
    Func d_clamped = d(clamped);
    // Every dependency of d_clamped should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_clamped)) << "Function has non pure update\n";
    Buffer<float> d_clamped_buf = d_clamped.realize(5, 5);
    // d loss / d clamped = d blur_x(x, y) + d blur_x(x - 1, y) + d blur_x(x - 2, y)
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            float target = d_blur_x_buf(x, y);
            if (x >= 1) {
                target += d_blur_x_buf(x - 1, y);
            }
            if (x >= 2) {
                target += d_blur_x_buf(x - 2, y);
            }
            float diff = fabs(d_clamped_buf(x, y) - target);
            _halide_user_assert(diff < eps)
                << "Expected d_clamped(" << x << ", " << y << ") to be " << target << " instead of " << d_clamped_buf(x, y) << "\n";
        }
    }
}

void test_update() {
    Var x("x");
    Buffer<float> input(3);
    input(0) = 1.f;
    input(1) = 2.f;
    input(2) = 3.f;
    Func clamped("clamped");
    Expr clamped_x = Halide::clamp(x, 0, input.width() - 1);
    clamped(x) = input(clamped_x);
    Func blur("blur");
    blur(x) = clamped(x);
    blur(x) += clamped(x + 1);
    RDom r(0, 3);
    Func f_loss("f_loss");
    f_loss() += blur(r.x) * blur(r.x);
    Derivative d = propagate_adjoints(f_loss);

    Buffer<float> blur_buf = blur.realize(3);
    // d loss / d blur = 2 * blur(x)
    Buffer<float> d_blur_buf = d(blur).realize(3);

    check(__LINE__, d_blur_buf(0), 2 * blur_buf(0));
    check(__LINE__, d_blur_buf(1), 2 * blur_buf(1));
    check(__LINE__, d_blur_buf(2), 2 * blur_buf(2));
    Func d_clamped = d(clamped);
    // Every dependency of d_clamped should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_clamped)) << "Function has non pure update\n";
    Buffer<float> d_clamped_buf = d_clamped.realize(3);
    check(__LINE__, d_clamped_buf(0), d_blur_buf(0));
    check(__LINE__, d_clamped_buf(1), d_blur_buf(0) + d_blur_buf(1));
    check(__LINE__, d_clamped_buf(2), d_blur_buf(1) + d_blur_buf(2));
}

void test_nonlinear_update() {
    Var x("x"), y("y");
    Buffer<float> input(3);
    input(0) = 1.f;
    input(1) = 2.f;
    input(2) = 3.f;
    Func clamped("clamped");
    Expr clamped_x = Halide::clamp(x, 0, input.width() - 1);
    clamped(x) = input(clamped_x);
    Func update("update");
    update(x, y) = 0.f;
    update(x, 0) = clamped(x);
    update(x, 1) = update(x, 0) * update(x, 0) + clamped(x + 1);
    // update(x) = clamp(x)^2 + clamp(x + 1)
    RDom r(0, 3);
    Func loss("loss");
    loss() += update(r.x, 1);
    Derivative d = propagate_adjoints(loss);
    Buffer<float> loss_buf = loss.realize();
    // loss = update(0) + update(1) + update(2)
    //      = 1^2 + 2 + 2^2 + 3 + 3^2 + 3 = 1 + 2 + 4 + 3 + 9 + 3 = 22
    check(__LINE__, loss_buf(0), 22.f);

    Func d_clamped = d(clamped);
    // d_clamp(x) = 2 * clamp(x) * d_update(x) + d_update(x - 1)
    Buffer<float> d_clamped_buf = d_clamped.realize(3);
    check(__LINE__, d_clamped_buf(0), 2.f * input(0));
    check(__LINE__, d_clamped_buf(1), 2.f * input(1) + 1.f);
    check(__LINE__, d_clamped_buf(2), 2.f * input(2) + 1.f);
}

void test_rdom_conv() {
    Var x("x");
    Buffer<float> input(4);
    input(0) = 1.f;
    input(1) = 2.f;
    input(2) = 3.f;
    input(3) = 4.f;
    Func clamped("clamped");
    clamped(x) = input(Halide::clamp(x, 0, input.width() - 1));
    Buffer<float> kernel(2);
    kernel(0) = 2.f;
    kernel(1) = 1.f;
    Func convolved("convolved");
    RDom support(0, 2);
    convolved(x) += clamped(x + support) * kernel(support);
    RDom r(0, 4);
    Func f_loss("f_loss");
    f_loss() += convolved(r.x) * convolved(r.x);
    Derivative d = propagate_adjoints(f_loss);
    Buffer<float> convolved_buf = convolved.realize(4);
    // d loss / d blur = 2 * blur(x)
    Buffer<float> d_convolved_buf = d(convolved).realize(4);
    for (int i = 0; i < 4; i++) {
        check(__LINE__, d_convolved_buf(i), 2 * convolved_buf(i));
    }
    // d loss / d clamped = d_convolved convolve with flipped kernel
    Func d_clamped = d(clamped);
    // Every dependency of d_clamped should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_clamped)) << "Function has non pure update\n";
    Buffer<float> d_clamped_buf = d_clamped.realize(4);
    for (int i = 0; i < 4; i++) {
        float target = d_convolved_buf(i) * kernel(0);
        if (i >= 1) {
            target += d_convolved_buf(i - 1) * kernel(1);
        }
        check(__LINE__, d_clamped_buf(i), target);
    }
    // loss = (k0 + 2k1)^2 + (2k0 + 3k1)^2 + (3k0 + 4k1)^2 + (4k0 + 4k1)^2
    //      = k0^2 + 4k0k1 + 4k1^2 + 4k0^2 + 12 k0k1 + 9k1^2 + 9k0^2 + 24 k0k1 + 16 k1^2 + 16k0^2 + 32k0k1 + 16k1^2
    //      = 30 k0^2 + 72 k0k1 + 45 k1^2
    // d loss / d kernel(0) = 2 * k0 * 30 + 72 * k1
    // d loss / d kernel(1) = 72 * k0 + 90 * k1
    Buffer<float> d_kernel = d(kernel).realize(2);
    check(__LINE__, d_kernel(0), 60.f * kernel(0) + 72.f * kernel(1));
    check(__LINE__, d_kernel(1), 72.f * kernel(0) + 90.f * kernel(1));
}

void test_horner_polynomial() {
    Var x("x"), y("y");
    Buffer<float> coeffs(8);
    for (int i = 0; i < 8; i++) {
        coeffs(i) = (i + 1.f);
    }
    RDom r(coeffs);
    Func polynomial("polynomial");
    Expr fx = x / cast<float>(1023);
    // Horner's method
    polynomial(x, y) = 0.f;
    polynomial(x, coeffs.dim(0).max() - r) =
        polynomial(x, coeffs.dim(0).max() - r + 1) * fx + coeffs(r);

    RDom rd(0, 1024);
    Func loss("loss");
    loss() += polynomial(rd, 0) / 1024.f;
    Derivative d = propagate_adjoints(loss);

    // poly(7) = coeff(0)
    // poly(6) = poly(7) * x + coeff(1)
    // poly(5) = poly(6) * x + coeff(2)
    // ...
    // poly(0) = poly(1) * x + coeff(0)
    // The adjoint is
    // d_coeffs(0) = \sum_x d_poly(x, 0)
    // d_coeffs(1) = \sum_x d_poly(x, 1)
    // ..
    // and
    // d_poly(x, 7) = \sum_x 1
    // d_poly(x, 6) = \sum_x x
    // d_poly(x, 5) = \sum_x x^2
    // ...
    Buffer<float> d_coeffs = d(coeffs).realize(8);
    for (int i = 0; i < 8; i++) {
        float d = 0.f;
        for (int j = 0; j < 1024; j++) {
            d += std::pow(j / 1023.f, (float) (7 - i));
        }
        d /= 1024.f;
        check(__LINE__, d_coeffs(i), d);
    }
}

void test_nonlinear_order_dependent_rdom() {
    Var x("x"), y("y");
    Buffer<float> in(2);
    for (int i = 0; i < 2; i++) {
        in(i) = (i + 2.f);
    }
    RDom r(in);
    Func f;
    f(x, y) = 0.f;
    f(x, 0) = in(x);
    f(x, r.x + 1) = f(x, r.x) * f(x, r.x) + in(r.x);

    Func loss("loss");
    loss() += f(r, 2);
    Derivative d = propagate_adjoints(loss);

    // Manual backprop
    float f0 = in(0);
    float f1 = in(1);
    float f0_a = f0 * f0 + in(0);
    float f0_b = f0_a * f0_a + in(1);
    float f1_a = f1 * f1 + in(0);
    float f1_b = f1_a * f1_a + in(1);
    float loss_ = f0_b + f1_b;
    float df0_b = 1.f;
    float df1_b = 1.f;
    float df1_a = df1_b * 2.f * f1_a;
    float din1 = df1_b;
    float df1 = df1_a * 2.f * f1;
    float din0 = df1_a;
    float df0_a = df0_b * 2.f * f0_a;
    din1 += df0_b;
    float df0 = df0_a * 2.f * f0;
    din0 += df0_a;
    din1 += df1;
    din0 += df0;
    Buffer<float> loss_buf = loss.realize();
    check(__LINE__, loss_, loss_buf());
    Buffer<float> d_in = d(in).realize(2);
    check(__LINE__, d_in(0), din0);
    check(__LINE__, d_in(1), din1);
}

void test_1d_to_2d() {
    Var x("x"), y("y");
    Buffer<float> input(2);
    input(0) = 1.f;
    input(1) = 2.f;
    Func output("output");
    output(x, y) = (x + 1.f) * input(y);

    RDom r(0, 2, 0, 2);
    Func loss("loss");
    loss() += output(r.x, r.y) * output(r.x, r.y);
    Derivative d = propagate_adjoints(loss);

    // loss = 5i0^2 + 5i1^2
    // d loss / d i0 = 10i0 = 10
    // d loss / d i1 = 10i1 = 20
    Buffer<float> d_output = d(output).realize(2, 2);
    check(__LINE__, d_output(0, 0), 2.f);
    check(__LINE__, d_output(1, 0), 4.f);
    check(__LINE__, d_output(0, 1), 4.f);
    check(__LINE__, d_output(1, 1), 8.f);

    Func d_input = d(input);
    // Every dependency of d_input should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_input)) << "Function has non pure update\n";
    Buffer<float> d_input_buf = d_input.realize(2);
    check(__LINE__, d_input_buf(0), 10.f);
    check(__LINE__, d_input_buf(1), 20.f);
}

void test_linear_resampling_1d() {
    // f(x) = i1(i0(x)) with linear resampling
    Var x("x");
    Buffer<float> input0(2);
    input0(0) = 0.3f;
    input0(1) = 1.8f;
    Buffer<float> input1(3);
    input1(0) = 1.0f;
    input1(1) = 2.0f;
    input1(2) = 4.0f;
    Func clamped0("clamped0");
    clamped0(x) = input0(Halide::clamp(x, 0, input0.width() - 1));
    Func clamped1("clamped1");
    clamped1(x) = input1(Halide::clamp(x, 0, input1.width() - 1));
    Expr gx = clamped0(x);
    Expr fx = cast<int>(clamp(floor(clamped0(x)), 0.f, 1.f));
    Expr cx = fx + 1;
    Expr wx = gx - fx;
    Func interpolate("interpolate");
    interpolate(x) = clamped1(fx) * (1.f - wx) + clamped1(cx) * wx;

    RDom r(0, 2);
    Func loss("loss");
    loss() += interpolate(r.x);
    Derivative d = propagate_adjoints(loss);

    // f_interpolate = {i1[0] * (1 - (i0[0] - floor(i0[0]))) +
    //                  i1[1] * (i0[0] - floor(i0[0])),
    //                  i1[1] * (1 - (i0[1] - floor(i0[1]))) +
    //                  i1[2] * (i0[1] - floor(i0[1]))}
    // loss = f_interpolate[0] + f_interpolate[1]
    // d loss / d i0[0] = -i1[0] + i1[1] = 1
    // d loss / d i0[1] = -i1[1] + i1[2] = 2
    // d loss / d i1[0] = 1 - (i0[0] - floor(i0[0]))
    // d loss / d i1[1] = (i0[0] - floor(i0[0])) +
    //                    (1 - (i0[1] - floor(i0[1])))
    // d loss / d i1[2] = i0[1] - floor(i0[1])

    Buffer<float> interpolate_buf = interpolate.realize(2);
    check(__LINE__, interpolate_buf(0), 1.3f);
    check(__LINE__, interpolate_buf(1), 3.6f);

    Buffer<float> d_clamped0 = d(clamped0).realize(2);
    check(__LINE__, d_clamped0(0), 1.f);
    check(__LINE__, d_clamped0(1), 2.f);

    Buffer<float> d_clamped1 = d(clamped1).realize(3);
    check(__LINE__, d_clamped1(0), 0.7f);
    check(__LINE__, d_clamped1(1), 0.5f);
    check(__LINE__, d_clamped1(2), 0.8f);
}

void test_linear_resampling_2d() {
    // f(x, y) = i1(i0(x), y) with linear resampling
    Var x("x"), y("y");
    Buffer<float> input0(2, 1);
    input0(0, 0) = 0.3f;
    input0(1, 0) = 1.8f;
    Buffer<float> input1(3, 1);
    input1(0, 0) = 1.0f;
    input1(1, 0) = 2.0f;
    input1(2, 0) = 4.0f;
    Func clamped0("clamped0");
    Expr clamped_x0 = Halide::clamp(x, 0, input0.width() - 1);
    Expr clamped_y0 = Halide::clamp(y, 0, input0.height() - 1);
    clamped0(x, y) = input0(clamped_x0, clamped_y0);
    Func clamped1("clamped1");
    Expr clamped_x1 = Halide::clamp(x, 0, input1.width() - 1);
    Expr clamped_y1 = Halide::clamp(y, 0, input1.height() - 1);
    clamped1(x, y) = input1(clamped_x1, clamped_y1);
    Expr gx = clamped0(x, y);
    Expr fx = cast<int>(clamp(floor(clamped0(x, y)), 0.f, 1.f));
    Expr cx = fx + 1;
    Expr wx = gx - fx;
    Func interpolate("interpolate");
    interpolate(x, y) = clamped1(fx, y) * (1.f - wx) + clamped1(cx, y) * wx;

    RDom r(0, 2, 0, 1);
    Func loss("loss");
    loss() += interpolate(r.x, r.y);
    Derivative d = propagate_adjoints(loss);

    // Same as test_linear_resampling_1d()
    Buffer<float> interpolate_buf = interpolate.realize(2, 1);
    check(__LINE__, interpolate_buf(0, 0), 1.3f);
    check(__LINE__, interpolate_buf(1, 0), 3.6f);

    Buffer<float> d_clamped0 = d(clamped0).realize(2, 1);
    check(__LINE__, d_clamped0(0, 0), 1.f);
    check(__LINE__, d_clamped0(1, 0), 2.f);

    Buffer<float> d_clamped1 = d(clamped1).realize(3, 1);
    check(__LINE__, d_clamped1(0, 0), 0.7f);
    check(__LINE__, d_clamped1(1, 0), 0.5f);
    check(__LINE__, d_clamped1(2, 0), 0.8f);
}

void test_sparse_update() {
    Var x("x");
    Buffer<float> input(3);
    input(0) = 1.f;
    input(1) = 2.f;
    input(2) = 3.f;
    Func f_input("f_input");
    f_input(x) = input(x);
    Func output("output");
    output(x) = f_input(x);
    output(1) = 0.f;
    // XXX: if we write input(1) Halide returns a float
    // which means it is impossible to propagate to input,
    // so we need a surrogate f_input such that f_input(1) is symbolic
    output(2) = 2.f * f_input(1);

    Func loss("loss");
    RDom r(0, 3);
    loss() += output(r.x);
    Derivative d = propagate_adjoints(loss);

    Buffer<float> d_input = d(input).realize(3);
    check(__LINE__, d_input(0), 1.0f);
    check(__LINE__, d_input(1), 2.0f);
    check(__LINE__, d_input(2), 0.0f);
}

void test_histogram() {
    Var x("x");
    Buffer<int> input(4, "input");
    input(0) = 2;
    input(1) = 2;
    input(2) = 1;
    input(3) = 3;
    Buffer<float> k(5, "k");
    k(0) = 0.5f;
    k(1) = 1.f;
    k(2) = 1.5f;
    k(3) = 2.f;
    k(4) = 2.5f;
    Func output("output");
    output(x) = 0.f;
    RDom r(input);
    output(clamp(input(r), 0, 3)) += k(r);

    Func loss("loss");
    RDom rd(input);
    loss() += output(rd) * cast<float>(rd + 1);
    Derivative d = propagate_adjoints(loss);

    // d_output(2) -> d_k(0)
    // d_output(2) -> d_k(1)
    // d_output(1) -> d_k(2)
    // d_output(3) -> d_k(3)
    Buffer<float> d_k = d(k).realize(5);
    check(__LINE__, d_k(0), 3.0f);
    check(__LINE__, d_k(1), 3.0f);
    check(__LINE__, d_k(2), 2.0f);
    check(__LINE__, d_k(3), 4.0f);
    check(__LINE__, d_k(4), 0.0f);
}

void test_multiple_updates_histogram() {
    Var x("x");
    Buffer<int> input(4, "input");
    input(0) = 2;
    input(1) = 2;
    input(2) = 1;
    input(3) = 3;
    Buffer<float> k(4, "k");
    k(0) = 0.5f;
    k(1) = 1.f;
    k(2) = 1.5f;
    k(3) = 2.f;
    k(4) = 2.5f;
    Func output("output");
    output(x) = 0.f;
    RDom r(input);
    for (int i = 0; i < 10; i++) {
        output(clamp(input(r), 0, 3)) += k(r);
    }

    Func loss("loss");
    RDom rd(input);
    loss() += output(rd) * cast<float>(rd + 1);
    Derivative d = propagate_adjoints(loss);

    // Schedule this so it doesn't run forever
    output.compute_root();
    auto funcs = d.adjoints;
    for (auto it : funcs) {
        it.second.compute_root();
    }

    // d_output(2) -> d_k(0) * 2
    // d_output(2) -> d_k(1) * 2
    // d_output(1) -> d_k(2) * 2
    // d_output(3) -> d_k(3) * 2
    Buffer<float> d_k = d(k).realize(5);
    check(__LINE__, d_k(0), 30.0f);
    check(__LINE__, d_k(1), 30.0f);
    check(__LINE__, d_k(2), 20.0f);
    check(__LINE__, d_k(3), 40.0f);
    check(__LINE__, d_k(4), 0.0f);
}

void test_rdom_update() {
    Var x("x");
    Buffer<float> input(3);
    input(0) = 1.f;
    input(1) = 2.f;
    input(2) = 3.f;
    Func output("output");
    RDom r0(1, 2), r1(3, 4);
    output(x) = input(x);
    output(r0) = input(r0 - 1);
    output(r1) = 0.f;

    Func loss("f_loss");
    RDom r_target(0, 5);
    loss() += output(r_target);
    Derivative d = propagate_adjoints(loss);

    Buffer<float> d_input = d(input).realize(3);
    check(__LINE__, d_input(0), 2.0f);
    check(__LINE__, d_input(1), 1.0f);
    check(__LINE__, d_input(2), 0.0f);
}

void test_repeat_edge() {
    Var x("x");
    Buffer<float> input(2);
    input(0) = 1.f;
    input(1) = 2.f;
    Func clamped = BoundaryConditions::repeat_edge(input);
    Func blur("blur");
    blur(x) = clamped(x) + clamped(x + 1);
    RDom r(0, 3);
    Func loss("loss");
    loss() += blur(r.x);
    Derivative d = propagate_adjoints(loss);
    // loss = (i0 + i1) + (i1 + i1) + (i1 + i1) = i0 + 5 * i1

    Buffer<float> d_blur_buf = blur.realize(3);
    Buffer<float> d_input_buf = d(input).realize(2);
    // d loss / d i0 = 1
    // d loss / d i1 = 5
    check(__LINE__, d_input_buf(0), 1.f);
    check(__LINE__, d_input_buf(1), 5.f);
}

void test_constant_exterior() {
    Var x("x");
    Buffer<float> input(2);
    input(0) = 1.f;
    input(1) = 2.f;
    Func clamped = BoundaryConditions::constant_exterior(input, 0.f);
    Func blur("blur");
    blur(x) = clamped(x) + clamped(x + 1);
    RDom r(0, 3);
    Func loss("loss");
    loss() += blur(r.x);
    Derivative d = propagate_adjoints(loss);
    // loss = (i0 + i1) + i1 = i0 + 2 * i1

    Buffer<float> d_blur_buf = blur.realize(3);
    Buffer<float> d_input_buf = d(input).realize(2);
    // d loss / d i0 = 1
    // d loss / d i1 = 2
    check(__LINE__, d_input_buf(0), 1.f);
    check(__LINE__, d_input_buf(1), 2.f);
}

void test_repeat_image() {
    Var x("x");
    Buffer<float> input(2);
    input(0) = 1.f;
    input(1) = 2.f;
    Func clamped = BoundaryConditions::repeat_image(input);
    Func blur("blur");
    blur(x) = clamped(x) + clamped(x + 1);
    RDom r(0, 3);
    Func loss("loss");
    loss() += blur(r.x);
    Derivative d = propagate_adjoints(loss);
    // loss = (i0 + i1) + (i1 + i0) + (i0 + i1) = 3 * i0 + 3 * i1

    Buffer<float> d_blur_buf = blur.realize(3);
    Buffer<float> d_input_buf = d(input).realize(2);
    // d loss / d i0 = 3
    // d loss / d i1 = 3
    check(__LINE__, d_input_buf(0), 3.f);
    check(__LINE__, d_input_buf(1), 3.f);
}

void test_mirror_image() {
    Var x("x");
    Buffer<float> input(2);
    input(0) = 1.f;
    input(1) = 2.f;
    Func clamped = BoundaryConditions::mirror_image(input);
    Func blur("blur");
    blur(x) = clamped(x) + clamped(x + 1);
    RDom r(0, 3);
    Func loss("loss");
    loss() += blur(r.x);
    Derivative d = propagate_adjoints(loss);
    // loss = (i0 + i1) + (i1 + i1) + (i1 + i0) = 2 * i0 + 4 * i1

    Buffer<float> d_blur_buf = blur.realize(3);
    Buffer<float> d_input_buf = d(input).realize(2);
    // d loss / d i0 = 2
    // d loss / d i1 = 4
    check(__LINE__, d_input_buf(0), 2.f);
    check(__LINE__, d_input_buf(1), 4.f);
}

void test_mirror_interior() {
    Var x("x");
    Buffer<float> input(2);
    input(0) = 1.f;
    input(1) = 2.f;
    Func clamped = BoundaryConditions::mirror_interior(input);
    Func blur("blur");
    blur(x) = clamped(x) + clamped(x + 1);
    RDom r(0, 3);
    Func loss("loss");
    loss() += blur(r.x);
    Derivative d = propagate_adjoints(loss);
    // loss = (i0 + i1) + (i1 + i0) + (i0 + i1) = 3 * i0 + 3 * i1

    Buffer<float> d_blur_buf = blur.realize(3);
    Buffer<float> d_input_buf = d(input).realize(2);
    // d loss / d i0 = 3
    // d loss / d i1 = 3
    check(__LINE__, d_input_buf(0), 3.f);
    check(__LINE__, d_input_buf(1), 3.f);
}

void test_second_order() {
    Var x("x");
    Func input("input");
    input() = 1.f;
    Func polynomial("polynomial");
    // x^2 + 3x + 4.f
    polynomial() = input() * input() + 3.f * input() + 4.f;
    Derivative d = propagate_adjoints(polynomial);
    Func d_input = d(input);
    Derivative d2 = propagate_adjoints(d_input);
    Func d2_input = d2(input);

    Buffer<float> buf = d_input.realize();
    Buffer<float> buf2 = d2_input.realize();
    // d/dx = 2x + 3
    check(__LINE__, buf(0), 5.f);

    // d^2/dx^2 = 2
    check(__LINE__, buf2(0), 2.f);
}

void test_second_order_conv() {
    Var x("x");
    Buffer<float> input(10, "input");
    for (int i = 0; i < 10; i++) {
        input(i) = float(i) / 10.f;
    }
    Buffer<float> target(10, "target");
    for (int i = 0; i < 10; i++) {
        target(i) = float(i + 1) / 10.f;
    }
    Buffer<float> kernel(3, "kernel");
    kernel(0) = kernel(1) = kernel(2) = 1.f;
    Func input_re = BoundaryConditions::repeat_edge(input);
    RDom rc(0, 3);
    Func conv("conv");
    conv(x) += input_re(x + rc - 1) * kernel(rc);
    RDom rl(0, 9);
    Func loss0("loss0");
    loss0() += pow(conv(rl) - target(rl), 2.f);
    Derivative d = propagate_adjoints(loss0);
    Func d_input = d(input);
    Func loss1("loss1");
    loss1() += d_input(rl);
    Derivative d2 = propagate_adjoints(loss1);

    Buffer<float> conv_buf = conv.realize(9);
    Buffer<float> d_conv_buf = d(conv).realize(9);
    // d_conv(x) = 2 * (conv(x) - target(x))
    for (int i = 0; i < 9; i++) {
        check(__LINE__, d_conv_buf(i), 2.f * (conv_buf(i) - target(i)));
    }
    // d_input(x) = d_conv(x + 1) + d_conv(x) + d_conv(x - 1)
    Buffer<float> d_input_buf = d_input.realize(10);
    check(__LINE__, d_input_buf(0), d_conv_buf(0) + d_conv_buf(1));
    for (int i = 1; i < 8; i++) {
        check(__LINE__, d_input_buf(i), d_conv_buf(i + 1) + d_conv_buf(i) + d_conv_buf(i - 1));
    }
    check(__LINE__, d_input_buf(8), d_conv_buf(7) + d_conv_buf(8));
    check(__LINE__, d_input_buf(9), d_conv_buf(8));
    Buffer<float> d2_conv_buf = d2(conv).realize(9);
    // d2_conv(x) = 6
    for (int i = 0; i < 8; i++) {
        check(__LINE__, d2_conv_buf(i), 6.f);
    }
    check(__LINE__, d2_conv_buf(8), 4.f);
    // d2_input(x) = d2_conv(x + 1) + d2_conv(x) + d2_conv(x - 1)
    Buffer<float> d2_input_buf = d2(input).realize(10);
    check(__LINE__, d2_input_buf(0), 2.f * d2_conv_buf(0) + d2_conv_buf(1));
    for (int i = 1; i <= 7; i++) {
        check(__LINE__, d2_input_buf(i), d2_conv_buf(i) + d2_conv_buf(i + 1) + d2_conv_buf(i - 1));
    }
    check(__LINE__, d2_input_buf(8), d2_conv_buf(8) + d2_conv_buf(7));
    check(__LINE__, d2_input_buf(9), d2_conv_buf(8));
}

void test_implicit_vars() {
    Var x("x");
    Buffer<float> input(2);
    input(0) = 1.f;
    input(1) = 2.f;
    Func copy("copy");
    copy(_) = input(_);
    RDom r(0, 2);
    Func loss("loss");
    loss() += copy(r.x);
    Derivative d = propagate_adjoints(loss);

    Func d_input = d(input);
    // Every dependency of d_input should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_input)) << "Function has non pure update\n";
    Buffer<float> d_input_buf = d_input.realize(2);
    check(__LINE__, d_input_buf(0), 1.f);
    check(__LINE__, d_input_buf(1), 1.f);
    Func d_copy = d(copy);
    // Every dependency of d_copy should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_copy)) << "Function has non pure update\n";
    Buffer<float> d_copy_buf = d_copy.realize(2);
    check(__LINE__, d_copy_buf(0), 1.f);
    check(__LINE__, d_copy_buf(1), 1.f);
}

void test_tuple() {
    Var x("x");
    Buffer<float> input(3);
    input(0) = 1.f;
    input(1) = 2.f;
    input(2) = 3.f;
    Func tuple("tuple");
    tuple(x) = Tuple(input(x), input(x + 1));
    tuple(x) += Tuple(1.f, 1.f);
    Func reduce("reduce");
    reduce(x) = tuple(x)[0] + tuple(x)[1];
    RDom r(0, 2);
    Func loss("loss");
    loss() += reduce(r.x);
    Derivative d = propagate_adjoints(loss);
    // tuple(0) = {1, 2}
    // tuple(1) = {2, 3}
    // reduce(0) = 3
    // reduce(1) = 5
    // loss = reduce(0) + reduce(1)
    //      = tuple(0)[0] + tuple(0)[1] + tuple(1)[0] + tuple(1)[1]
    //      = input(0) + input(1) * 2 + input(2)

    Func d_tuple = d(tuple);
    // Every dependency of d_tuple should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_tuple)) << "Function has non pure update\n";
    Realization d_tuple_buf = d_tuple.realize(2);
    Buffer<float> d_tuple_buf_0 = d_tuple_buf[0];
    Buffer<float> d_tuple_buf_1 = d_tuple_buf[1];
    check(__LINE__, d_tuple_buf_0(0), 1.f);
    check(__LINE__, d_tuple_buf_0(1), 1.f);
    check(__LINE__, d_tuple_buf_1(0), 1.f);
    check(__LINE__, d_tuple_buf_1(1), 1.f);

    Func d_input = d(input);
    // Every dependency of d_input should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_input)) << "Function has non pure update\n";
    Buffer<float> d_input_buf = d_input.realize(3);
    check(__LINE__, d_input_buf(0), 1.f);
    check(__LINE__, d_input_buf(1), 2.f);
    check(__LINE__, d_input_buf(2), 1.f);
}

void test_floor_ceil() {
    Var x("x");
    Buffer<float> input(3);
    input(0) = 1.f;
    input(1) = 2.f;
    input(2) = 3.f;
    Func floor_output("floor_output");
    floor_output(x) = input(cast<int>(floor(x / 4.f)));
    Func ceil_output("ceil_output");
    ceil_output(x) = input(cast<int>(ceil(x / 4.f)));
    Func output("output");
    output(x) = ceil_output(x) + floor_output(x);
    RDom r(0, 8);
    Func loss("loss");
    loss() += output(r.x);
    Derivative d = propagate_adjoints(loss);
    // floor_output(0~3) == input[0]
    // floor_output(4~7) == input[1]
    // ceil_output(0) == input[0]
    // ceil_output(1~4) == input[1]
    // ceil_output(5~7) = input[2]
    Buffer<float> d_input_buf = d(input).realize(3);

    check(__LINE__, d_input_buf(0), 5.f);
    check(__LINE__, d_input_buf(1), 8.f);
    check(__LINE__, d_input_buf(2), 3.f);
}

void test_downsampling() {
    Var x("x");
    Buffer<float> input(10);
    for (int i = 0; i < 10; i++) {
        input(i) = float(i);
    }
    Func output("output");
    RDom r(0, 4);
    output(x) += input(4 * x + r);
    RDom r_loss(0, 2);
    Func loss("loss");
    loss() += output(r_loss);
    Derivative d = propagate_adjoints(loss);
    // output(0) = \sum input(0~4)
    // output(1) = \sum input(5~8)
    Func d_input = d(input);
    // Every dependency of d_tuple should only use pure variables in lhs
    _halide_user_assert(!has_non_pure_update(d_input)) << "Function has non pure update\n";
    Buffer<float> d_input_buf = d_input.realize(10);

    for (int i = 0; i < 8; i++) {
        check(__LINE__, d_input_buf(i), 1.f);
    }
    check(__LINE__, d_input_buf(8), 0.f);
    check(__LINE__, d_input_buf(9), 0.f);
}

void test_upsampling() {
    Var x("x");
    Buffer<float> input(4);
    for (int i = 0; i < 4; i++) {
        input(i) = float(i);
    }
    Func output("output");
    output(x) = input(x / 4);
    RDom r_loss(0, 16);
    Func loss("loss");
    loss() += output(r_loss);
    Derivative d = propagate_adjoints(loss);
    Func d_input = d(input);
    // Every dependency of d_tuple should only use pure variables in lhs
    // _halide_user_assert(!has_non_pure_update(d_input)) << "Function has non pure update\n";
    Buffer<float> d_input_buf = d_input.realize(4);

    for (int i = 0; i < 4; i++) {
        check(__LINE__, d_input_buf(i), 4.f);
    }
}

void test_transpose() {
    Var x("x"), y("y");
    Buffer<float> input(5, 5);
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            input(i, j) = float(i + j);
        }
    }
    Buffer<float> target(5, 5);
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            target(i, j) = float(i * j);
        }
    }
    Func output("output");
    output(x, y) = input(y, x);
    RDom r(0, 5, 0, 5);
    Func loss("loss");
    loss() += pow(output(r.x, r.y) - target(r.x, r.y), 2);
    Derivative d = propagate_adjoints(loss);
    Func d_input = d(input);
    Buffer<float> d_input_buf = d_input.realize(5, 5);
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            check(__LINE__, d_input_buf(i, j), 2.f * (input(i, j) - target(j, i)));
        }
    }
}

void test_change_var() {
    Var x("x"), y("y"), a("a"), b("b");
    Buffer<float> input(5, 5);
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            input(i, j) = float(i + j);
        }
    }
    Buffer<float> target(5, 5);
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            target(i, j) = float(i * j);
        }
    }
    Func xy_func("xy");
    xy_func(x, y) = input(x, y);
    Func ab_func("ab");
    ab_func(a, b) = xy_func(a, b);
    RDom r(0, 5, 0, 5);
    Func loss("loss");
    loss() += pow(ab_func(r.x, r.y) - target(r.x, r.y), 2);
    Derivative d = propagate_adjoints(loss);
    Func d_input = d(input);
    Buffer<float> d_input_buf = d_input.realize(5, 5);
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            check(__LINE__, d_input_buf(i, j), 2.f * (input(i, j) - target(j, i)));
        }
    }
}

void test_rdom_predicate() {
    Var x("x"), y("y");
    Buffer<float> input(7, 7);
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            input(i, j) = float(i + j);
        }
    }
    RDom r(0, 7, 0, 7);
    r.where((r.x - 3) * (r.x - 3) + (r.y - 3) * (r.y - 3) <= 10);
    Func circle;
    circle(x, y) = input(x, y);
    circle(r.x, r.y) *= 2.f;

    RDom r_full(0, 7, 0, 7);
    Func loss("loss");
    loss() += circle(r_full.x, r_full.y);
    Derivative d = propagate_adjoints(loss);
    Func d_input = d(input);
    Buffer<float> d_input_buf = d_input.realize(7, 7);
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            bool in_circle =
                (i - 3) * (i - 3) + (j - 3) * (j - 3) <= 10;
            if (in_circle) {
                check(__LINE__, d_input_buf(i, j), 2.f);
            } else {
                check(__LINE__, d_input_buf(i, j), 1.f);
            }
        }
    }
}

void test_reverse_scan() {
    Var x("x");
    Buffer<float> input(5);
    for (int i = 0; i < 5; i++) {
        input(i) = float(i);
    }
    RDom r(input);
    Func reverse("reverse");
    reverse(x) = input(x);
    reverse(r.x) = reverse(4 - r.x);
    Func loss("loss");
    loss() += reverse(r.x) * (r.x + 1.f);
    Derivative d = propagate_adjoints(loss);
    Func d_input = d(input);
    Buffer<float> d_input_buf = d_input.realize(5);
    for (int i = 0; i < 5; i++) {
        check(__LINE__, d_input_buf(i), (5.f - (float)i));
    }
}

int main(int argc, char **argv) {
    test_scalar<float>();
    test_scalar<double>();
    test_1d_box_no_clamp();
    test_1d_box();
    test_2d_box();
    test_update();
    test_nonlinear_update();
    test_rdom_conv();
    test_horner_polynomial();
    test_nonlinear_order_dependent_rdom();
    test_1d_to_2d();
    test_linear_resampling_1d();
    test_linear_resampling_2d();
    test_sparse_update();
    test_histogram();
    test_multiple_updates_histogram();
    test_rdom_update();
    test_repeat_edge();
    test_constant_exterior();
    test_repeat_image();
    test_mirror_image();
    test_mirror_interior();
    test_second_order();
    test_second_order_conv();
    test_implicit_vars();
    test_tuple();
    test_floor_ceil();
    test_downsampling();
    test_upsampling();
    test_transpose();
    test_change_var();
    test_rdom_predicate();
    test_reverse_scan();
    printf("Success!\n");
}
