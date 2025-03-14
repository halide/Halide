#include "FastMathFunctions.h"

#include "ApproximationTables.h"
#include "CSE.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {
namespace ApproxImpl {

constexpr double PI = 3.14159265358979323846;
constexpr double ONE_OVER_PI = 1.0 / PI;
constexpr double TWO_OVER_PI = 2.0 / PI;
constexpr double PI_OVER_TWO = PI / 2;

std::pair<float, float> split_float(double value) {
    float high = float(value);                // Convert to single precision
    float low = float(value - double(high));  // Compute the residual part
    return {high, low};
}

Expr eval_poly_fast(Expr x, const std::vector<double> &coeff) {
    int n = coeff.size();
    internal_assert(n >= 2);

    Expr x2 = x * x;

    Expr even_terms = make_const(x.type(), coeff[n - 1]);
    Expr odd_terms = make_const(x.type(), coeff[n - 2]);

    for (int i = 2; i < n; i++) {
        double c = coeff[n - 1 - i];
        if ((i & 1) == 0) {
            if (c == 0.0f) {
                even_terms *= x2;
            } else {
                even_terms = even_terms * x2 + make_const(x.type(), c);
            }
        } else {
            if (c == 0.0f) {
                odd_terms *= x2;
            } else {
                odd_terms = odd_terms * x2 + make_const(x.type(), c);
            }
        }
    }

    if ((n & 1) == 0) {
        return even_terms * std::move(x) + odd_terms;
    } else {
        return odd_terms * std::move(x) + even_terms;
    }
}

Expr eval_poly_horner(const std::vector<double> &coefs, const Expr &x) {
    /*
     * The general scheme looks like this:
     *
     * R = a0 + x * a1 + x^2 * a2 + x^3 * a3
     *   = a0 + x * (a1 + x * a2 + x^2 * a3)
     *   = a0 + x * (a1 + x * (a2 + x * a3))
     */
    Type type = x.type();
    if (coefs.empty()) {
        return make_const(x.type(), 0.0);
    }

    Expr result = make_const(type, coefs.back());
    for (size_t i = 1; i < coefs.size(); ++i) {
        result = x * result + make_const(type, coefs[coefs.size() - i - 1]);
    }
    debug(3) << "Polynomial (normal): " << common_subexpression_elimination(result) << "\n";
    return result;
}

inline std::pair<Expr, Expr> two_sum(const Expr &a, const Expr &b) {
    // TODO(mcourteaux): replace with proper strict_float intrinsic ops.
    Expr x = strict_float(a + b);
    Expr z = strict_float(x - a);
    Expr y = strict_float(strict_float(a - strict_float(x - z)) + strict_float(b - z));
    return {x, y};
}

inline std::pair<Expr, Expr> two_prod(const Expr &a, const Expr &b) {
    // TODO(mcourteaux): replace with proper strict_float intrinsic ops.
    Expr x = strict_float(a * b);
    Expr y = (a * b - x);  // No strict float, so let's hope it gets compiled as FMA.
    return {x, y};
}

Expr eval_poly_compensated_horner(const std::vector<double> &coefs, const Expr &x) {
    // "Compensated Horner Scheme" by S. Graillat, Ph. Langlois, N. Louvet
    // https://www-pequan.lip6.fr/~jmc/polycopies/Compensation-horner.pdf
    // Currently I'm not seeing any notable precision improvement. I'm not sure if this
    // due to simplifications and optimizations happening, or the already good precision of fma ops.
    // TODO(mcourteaux): Revisit this once we have proper strict_float intrinsics.
    Type type = x.type();
    if (coefs.empty()) {
        return make_const(x.type(), 0.0);
    }

    Expr result = make_const(type, coefs.back());
    Expr error = make_const(type, 0.0);
    for (size_t i = 1; i < coefs.size(); ++i) {
        double c = coefs[coefs.size() - i - 1];
        if (c == 0.0) {
            auto [p, pi] = two_prod(result, x);
            result = p;
            error = error * x + pi;
        } else {
            auto [p, pi] = two_prod(result, x);
            auto [sn, sigma] = two_sum(p, make_const(type, c));
            result = sn;
            error = error * x + (pi + sigma);
        }
    }
    debug(3) << "Polynomial (preciser): " << common_subexpression_elimination(result) << "\n";
    return result;
}

Expr eval_poly(const std::vector<double> &coefs, const Expr &x) {
    // return eval_poly_compensated_horner(coefs, x);
    if (coefs.size() >= 2) {
        return eval_poly_fast(x, coefs);
    }
    return eval_poly_horner(coefs, x);
}

Expr eval_approx(const Approximation *approx, const Expr &x) {
    Expr eval_p = eval_poly(approx->p, x);
    if (approx->q.empty()) {
        return eval_p;
    }
    Expr eval_q = eval_poly(approx->q, x);
    return eval_p / eval_q;
}

Expr fast_sin(const Expr &x_full, ApproximationPrecision precision) {
    Type type = x_full.type();
    // To increase precision for negative arguments, we should not flip the argument of the polynomial,
    // but instead take absolute value of argument, and flip the result's sign in case of sine.
    Expr x_abs = abs(x_full);
    // Range reduction to interval [0, pi/2] which corresponds to a quadrant of the circle.
    Expr scaled = x_abs * make_const(type, TWO_OVER_PI);
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);
    Expr k_mod4 = k % 4;  // Halide mod is always positive!
    Expr mirror = (k_mod4 == 1) || (k_mod4 == 3);
    Expr flip_sign = (k_mod4 > 1) ^ (x_full < 0);

    // Reduce the angle modulo pi/2: i.e., to the angle within the quadrant.
    Expr x = x_abs - k_real * make_const(type, PI_OVER_TWO);
    Expr pi_over_two_minus_x = make_const(type, PI_OVER_TWO) - x;
    if (type == Float(32) && precision.optimized_for == ApproximationPrecision::MULPE) {
        auto [hi, lo] = split_float(PI_OVER_TWO);
        // TODO(mcourteaux): replace with proper strict_float intrinsic ops.
        pi_over_two_minus_x = strict_float(make_const(type, hi) - x) + make_const(type, lo);
    }
    x = select(mirror, pi_over_two_minus_x, x);

    const Internal::Approximation *approx = Internal::ApproximationTables::best_sin_approximation(precision, type);
    Expr result = eval_approx(approx, x);
    result = select(flip_sign, -result, result);
    result = common_subexpression_elimination(result, true);
    return result;
}

Expr fast_cos(const Expr &x_full, ApproximationPrecision precision) {
    const bool use_sin = precision.optimized_for == ApproximationPrecision::MULPE;

    Type type = x_full.type();
    Expr x_abs = abs(x_full);
    // Range reduction to interval [0, pi/2] which corresponds to a quadrant of the circle.
    Expr scaled = x_abs * make_const(type, TWO_OVER_PI);
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);
    Expr k_mod4 = k % 4;  // Halide mod is always positive!
    Expr mirror = ((k_mod4 == 1) || (k_mod4 == 3));
    if (use_sin) {
        mirror = !mirror;
    }
    Expr flip_sign = ((k_mod4 == 1) || (k_mod4 == 2));

    // Reduce the angle modulo pi/2: i.e., to the angle within the quadrant.
    Expr x = x_abs - k_real * make_const(type, PI_OVER_TWO);
    Expr pi_over_two_minus_x = make_const(type, PI_OVER_TWO) - x;
    if (type == Float(32) && precision.optimized_for == ApproximationPrecision::MULPE) {
        auto [hi, lo] = split_float(PI_OVER_TWO);
        // TODO(mcourteaux): replace with proper strict_float intrinsic ops.
        pi_over_two_minus_x = strict_float(strict_float(make_const(type, hi) - x) + make_const(type, lo));
    }
    x = select(mirror, pi_over_two_minus_x, x);

    Expr result;
    if (use_sin) {
        // Approximating cos(x) as sin(pi/2 - x).
        const Internal::Approximation *approx = Internal::ApproximationTables::best_sin_approximation(precision, type);
        result = eval_approx(approx, x);
    } else {
        const Internal::Approximation *approx = Internal::ApproximationTables::best_cos_approximation(precision, type);
        result = eval_approx(approx, x);
    }
    result = select(flip_sign, -result, result);
    result = common_subexpression_elimination(result, true);
    return result;
}

Expr fast_tan(const Expr &x_full, ApproximationPrecision precision) {
    Type type = x_full.type();

    // Reduce range to [-pi/2, pi/2]
    Expr scaled = x_full * make_const(type, ONE_OVER_PI);
    Expr k_real = round(scaled);

    Expr x = x_full - k_real * make_const(type, PI);
    if (type == Float(32) && precision.optimized_for == ApproximationPrecision::MULPE) {
        auto [pi_hi, pi_lo] = split_float(PI);
        // TODO(mcourteaux): replace with proper strict_float intrinsic ops.
        x = strict_float(strict_float(x_full - k_real * make_const(type, pi_hi)) - (k_real * make_const(type, pi_lo)));
    }

    // When polynomial: x is assumed to be reduced to [-pi/2, pi/2]!
    const Internal::Approximation *approx = Internal::ApproximationTables::best_tan_approximation(precision, type);

    Expr abs_x = abs(x);
    Expr flip = x < make_const(type, 0.0);
    Expr use_cotan = abs_x > make_const(type, PI / 4.0);
    Expr pi_over_two_minus_abs_x;
    if (type == Float(64)) {
        pi_over_two_minus_abs_x = make_const(type, PI_OVER_TWO) - abs_x;
    } else if (type == Float(32)) {  // We want to do this trick always, because we invert later.
        auto [hi, lo] = split_float(PI_OVER_TWO);
        // TODO(mcourteaux): replace with proper strict_float intrinsic ops.
        pi_over_two_minus_abs_x = strict_float(make_const(type, hi) - abs_x) + make_const(type, lo);
    }
    Expr arg = select(use_cotan, pi_over_two_minus_abs_x, abs_x);

    Expr result;
    if (!approx->q.empty()) {
        // If we are dealing with Padé approximants, we can immediately swap the two
        // things we divide to handle the cotan-branch.
        Expr p = eval_poly(approx->p, arg);
        Expr q = eval_poly(approx->q, arg);
        result = select(use_cotan, q, p) / select(use_cotan, p, q);
    } else {
        Expr tan_of_arg = eval_approx(approx, arg);
        result = select(use_cotan, make_const(type, 1) / tan_of_arg, tan_of_arg);
    }
    result = select(flip, -result, result);
    result = common_subexpression_elimination(result, true);
    return result;
}

// A vectorizable atan and atan2 implementation.
// Based on the ideas presented in https://mazzo.li/posts/vectorized-atan2.html.
Expr fast_atan_helper(const Expr &x_full, ApproximationPrecision precision, bool between_m1_and_p1) {
    Type type = x_full.type();
    Expr x;
    // if x > 1 -> atan(x) = Pi/2 - atan(1/x)
    Expr x_gt_1 = abs(x_full) > 1.0f;
    if (between_m1_and_p1) {
        x = x_full;
    } else {
        x = select(x_gt_1, make_const(type, 1.0) / x_full, x_full);
    }
    const Internal::Approximation *approx = Internal::ApproximationTables::best_atan_approximation(precision, type);
    Expr result = eval_approx(approx, x);

    if (!between_m1_and_p1) {
        result = select(x_gt_1, select(x_full < 0, make_const(type, -PI_OVER_TWO), make_const(type, PI_OVER_TWO)) - result, result);
    }
    result = common_subexpression_elimination(result, true);
    return result;
}

Expr fast_atan(const Expr &x_full, ApproximationPrecision precision) {
    return fast_atan_helper(x_full, precision, false);
}

Expr fast_atan2(const Expr &y, const Expr &x, ApproximationPrecision precision) {
    user_assert(y.type() == x.type()) << "fast_atan2 should take two arguments of the same type.";
    Type type = y.type();
    // Making sure we take the ratio of the biggest number by the smallest number (in absolute value)
    // will always give us a number between -1 and +1, which is the range over which the approximation
    // works well. We can therefore also skip the inversion logic in the fast_atan_helper function
    // by passing true for "between_m1_and_p1". This increases both speed (1 division instead of 2) and
    // numerical precision.
    Expr swap = abs(y) > abs(x);
    Expr atan_input = select(swap, x, y) / select(swap, y, x);
    // Increase precision somewhat, as we will compound some additional errors.
    precision.constraint_max_ulp_error /= 2;
    precision.constraint_max_absolute_error *= 0.5f;
    Expr ati = fast_atan_helper(atan_input, precision, true);
    Expr pi_over_two = make_const(type, PI_OVER_TWO);
    Expr pi = make_const(type, PI);
    Expr at = select(swap, select(atan_input >= 0.0f, pi_over_two, -pi_over_two) - ati, ati);
    // This select statement is literally taken over from the definition on Wikipedia.
    // There might be optimizations to be done here, but I haven't tried that yet. -- Martijn
    Expr result = select(
        x > 0.0f, at,
        x < 0.0f && y >= 0.0f, at + pi,
        x < 0.0f && y < 0.0f, at - pi,
        x == 0.0f && y > 0.0f, pi_over_two,
        x == 0.0f && y < 0.0f, -pi_over_two,
        0.0f);
    result = common_subexpression_elimination(result, true);
    return result;
}

Expr fast_exp(const Expr &x_full, ApproximationPrecision prec) {
    Type type = x_full.type();
    user_assert(x_full.type() == Float(32)) << "fast_exp only works for Float(32)";

    Expr log2 = make_const(type, std::log(2.0));

    Expr scaled = x_full / log2;
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);
    Expr x = x_full - k_real * log2;

    // exp(x) = 2^k * exp(x - k * log(2)), where k = floor(x / log(2))
    //                ^^^^^^^^^^^^^^^^^^^
    //                We approximate this
    //
    // Proof of identity:
    //   exp(x) = 2^(floor(x/log(2))) * exp(x - floor(x/log(2)) * log(2))
    //   exp(x) = 2^(floor(x/log(2))) * exp(x) / exp(floor(x/log(2)) * log(2))
    //   exp(x) = 2^(floor(x/log(2))) / exp(floor(x/log(2)) * log(2)) * exp(x)
    //   exp(x) = 2^(K) / exp(K * log(2))     * exp(x)
    //   log(exp(x)) = log(2^(K) / exp(K * log(2))     * exp(x))
    //   x = log(2^K) - K*log(2) + x
    //   x = K*log(2) - K*log(2) + x
    //   x = x

    const Internal::Approximation *approx = Internal::ApproximationTables::best_exp_approximation(prec, type);
    Expr result = eval_approx(approx, x);

    // Compute 2^k.
    int fpbias = 127;
    Expr biased = clamp(k + fpbias, 0, 255);

    // Shift the bits up into the exponent field and reinterpret this
    // thing as float.
    Expr two_to_the_n = reinterpret<float>(biased << 23);
    result *= two_to_the_n;
    result = common_subexpression_elimination(result, true);
    return result;
}

Expr fast_log(const Expr &x, ApproximationPrecision prec) {
    Type type = x.type();
    user_assert(x.type() == Float(32)) << "fast_log only works for Float(32)";

    Expr log2 = make_const(type, std::log(2.0));
    Expr reduced, exponent;
    Internal::range_reduce_log(x, &reduced, &exponent);

    Expr x1 = reduced - 1.0f;
    const Internal::Approximation *approx = Internal::ApproximationTables::best_log_approximation(prec, type);
    Expr result = eval_approx(approx, x1);

    result = result + cast<float>(exponent) * log2;
    result = common_subexpression_elimination(result);
    return result;
}

Expr fast_tanh(const Expr &x, ApproximationPrecision prec) {
    // Rewrite with definition:
    // tanh(x) = (exp(2x) - 1) / (exp(2x) + 1)
    //         = (1 - exp(-2x)) / (1 + exp(-2x))
    // But abs(x) the argument, and flip when negative.
    Type type = x.type();
    Expr abs_x = abs(x);
    Expr flip_sign = x < 0;
    if (prec.optimized_for == ApproximationPrecision::MULPE) {
        // Positive arguments to exp() have preciser ULP.
        // So, we will rewrite the expression to always use exp(2*x)
        // instead of exp(-2*x) when we are close to zero.
        // Rewriting it like this is slighlty more expensive, hence the branch
        // to only pay this extra cost in case we need MULPE-optimized approximations.
        Expr flip_exp = abs_x > make_const(type, 4);
        Expr arg_exp = select(flip_exp, -abs_x, abs_x);
        Expr exp2x = Halide::fast_exp(2 * arg_exp, prec);
        Expr tanh = (exp2x - make_const(type, 1.0)) / (exp2x + make_const(type, 1));
        tanh = select(flip_exp ^ flip_sign, -tanh, tanh);
        return common_subexpression_elimination(tanh, true);
    } else {
        // Even if we are optimizing for MAE, the nested call to exp()
        // should be MULPE optimized for accuracy, as we are taking ratios.
        prec.optimized_for = ApproximationPrecision::MULPE;
        Expr exp2x = Halide::fast_exp(-2 * abs_x, prec);
        Expr tanh = (make_const(type, 1) - exp2x) / (make_const(type, 1) + exp2x);
        tanh = select(flip_sign, -tanh, tanh);
        return common_subexpression_elimination(tanh, true);
    }
}

}  // namespace ApproxImpl

using OO = ApproximationPrecision::OptimizationObjective;
struct IntrinsicsInfo {
    DeviceAPI device_api{DeviceAPI::None};

    struct NativeFunc {
        bool is_fast{false};
        OO behavior{OO::AUTO};
        float max_abs_error{0.0f};
        uint64_t max_ulp_error{0};
        bool defined() const {
            return behavior != OO::AUTO;
        }
    } native_func;  //< Default-initialized means it works and is exact.

    struct IntrinsicImpl {
        OO behavior{OO::AUTO};
        float max_abs_error{0.0f};
        uint64_t max_ulp_error{0};
        bool defined() const {
            return behavior != OO::AUTO;
        }
    } intrinsic;
};

struct IntrinsicsInfoPerDeviceAPI {
    OO reasonable_behavior;  // A reasonable optimization objective for a given function.
    float default_mae;       // A reasonable desirable MAE (if specified)
    int default_mulpe;       // A reasonable desirable MULPE (if specified)
    std::vector<IntrinsicsInfo> device_apis;
};

// clang-format off
IntrinsicsInfoPerDeviceAPI ii_sin{
    OO::MAE, 1e-5f, 0, {
      {DeviceAPI::Vulkan, {true}, {}},
      {DeviceAPI::CUDA, {false}, {OO::MAE, 5e-7f, 1'000'000}},
      {DeviceAPI::Metal, {true}, {OO::MAE, 6e-5f,   400'000}},
      {DeviceAPI::WebGPU, {true}, {}},
      {DeviceAPI::OpenCL, {false}, {OO::MAE, 5e-7f, 1'000'000}},
}};

IntrinsicsInfoPerDeviceAPI ii_cos{
    OO::MAE, 1e-5f, 0, {
      {DeviceAPI::Vulkan, {true}, {}},
      {DeviceAPI::CUDA, {false}, {OO::MAE, 5e-7f, 1'000'000}},
      {DeviceAPI::Metal, {true}, {OO::MAE, 7e-7f,     5'000}},
      {DeviceAPI::WebGPU, {true}, {}},
      {DeviceAPI::OpenCL, {false}, {OO::MAE, 5e-7f, 1'000'000}},
}};

IntrinsicsInfoPerDeviceAPI ii_atan_atan2{
    OO::MAE, 1e-5f, 0, {
      // no intrinsics available
      {DeviceAPI::Vulkan, {false}, {}},
      {DeviceAPI::Metal, {true}, {OO::MAE, 5e-6f}},
      {DeviceAPI::WebGPU, {true}, {}},
}};

IntrinsicsInfoPerDeviceAPI ii_tan{
    OO::MULPE, 0.0f, 2000, {
      {DeviceAPI::Vulkan, {true, OO::MAE, 2e-6f, 1'000'000}, {}},  // Vulkan tan seems to mimic our CUDA implementation
      {DeviceAPI::CUDA, {false}, {OO::MAE, 2e-6f, 1'000'000}},
      {DeviceAPI::Metal, {true}, {OO::MULPE, 2e-6f, 1'000'000}},
      {DeviceAPI::WebGPU, {true}, {}},
      {DeviceAPI::OpenCL, {false}, {OO::MAE, 2e-6f, 1'000'000}},
}};

IntrinsicsInfoPerDeviceAPI ii_exp{
    OO::MULPE, 0.0f, 50, {
      {DeviceAPI::Vulkan, {true}, {}},
      {DeviceAPI::CUDA, {false}, {OO::MULPE, 0.0f, 5}},
      {DeviceAPI::Metal, {true}, {OO::MULPE, 0.0f, 5}},  // precise::exp() is fast on metal
      {DeviceAPI::WebGPU, {true}, {}},
      {DeviceAPI::OpenCL, {true}, {OO::MULPE, 0.0f, 5}}, // Both exp() and native_exp() are faster than polys.
}};

IntrinsicsInfoPerDeviceAPI ii_log{
    OO::MAE, 1e-5f, 1000, {
     {DeviceAPI::Vulkan, {true}, {}},
     {DeviceAPI::CUDA, {false}, {OO::MULPE, 0.0f, 3'800'000}},
     {DeviceAPI::Metal, {false}, {OO::MAE, 0.0f, 3'800'000}},  // slow log() on metal
     {DeviceAPI::WebGPU, {true}, {}},
     {DeviceAPI::OpenCL, {true}, {OO::MULPE, 0.0f, 3'800'000}},
}};

IntrinsicsInfoPerDeviceAPI ii_pow{
    OO::MULPE, 1e-5f, 1000, {
     {DeviceAPI::Vulkan, {false}, {}},
     {DeviceAPI::CUDA, {false}, {OO::MULPE, 0.0f, 3'800'000}},
     {DeviceAPI::Metal, {true}, {OO::MULPE, 0.0f, 3'800'000}},
     {DeviceAPI::WebGPU, {true}, {}},
     {DeviceAPI::OpenCL, {true}, {OO::MULPE, 0.0f, 3'800'000}},
}};

IntrinsicsInfoPerDeviceAPI ii_tanh{
    OO::MAE, 1e-5f, 1000, {
     {DeviceAPI::Vulkan, {true}, {}},
     {DeviceAPI::CUDA, {true}, {OO::MULPE, 1e-5f, 135}},  // Requires CC75
     {DeviceAPI::Metal, {true}, {OO::MULPE, 1e-5f, 135}},
     {DeviceAPI::WebGPU, {true}, {}},
}};

IntrinsicsInfoPerDeviceAPI ii_asin_acos{
   OO::MULPE, 1e-5f, 500, {
    {DeviceAPI::Vulkan, {true}, {}},
    {DeviceAPI::CUDA, {true}, {}},
    {DeviceAPI::OpenCL, {true}, {}},
}};
// clang-format on

bool fast_math_func_has_intrinsic_based_implementation(Call::IntrinsicOp op, DeviceAPI device, const Target &t) {
    const IntrinsicsInfoPerDeviceAPI *iipda = nullptr;
    switch (op) {
    case Call::fast_atan:
    case Call::fast_atan2:
        iipda = &ii_atan_atan2;
        break;
    case Call::fast_cos:
        iipda = &ii_cos;
        break;
    case Call::fast_exp:
        iipda = &ii_exp;
        break;
    case Call::fast_log:
        iipda = &ii_log;
        break;
    case Call::fast_pow:
        iipda = &ii_pow;
        break;
    case Call::fast_sin:
        iipda = &ii_sin;
        break;
    case Call::fast_tan:
        iipda = &ii_tan;
        break;
    case Call::fast_tanh:
        iipda = &ii_tanh;
        break;
    case Call::fast_asin:
    case Call::fast_acos:
        iipda = &ii_asin_acos;
        break;

    default:
        std::string name = Call::get_intrinsic_name(op);
        internal_assert(name.length() > 5 && name.substr(0, 5) != "fast_") << "Did not handle " << name << " in switch case";
        break;
    }

    internal_assert(iipda != nullptr) << "Function is only supported for fast_xxx math functions. Got: " << Call::get_intrinsic_name(op);

    for (const auto &cand : iipda->device_apis) {
        if (cand.device_api == device) {
            if (cand.intrinsic.defined()) {
                if (op == Call::fast_tanh && device == DeviceAPI::CUDA) {
                    return t.get_cuda_capability_lower_bound() >= 75;
                }
                return true;
            }
        }
    }
    return false;
}

IntrinsicsInfo resolve_precision(ApproximationPrecision &prec, const IntrinsicsInfoPerDeviceAPI &iida, DeviceAPI api) {
    IntrinsicsInfo ii{};
    for (const auto &cand : iida.device_apis) {
        if (cand.device_api == api) {
            ii = cand;
            break;
        }
    }

    if (prec.optimized_for == ApproximationPrecision::AUTO) {
        if (!ii.intrinsic.defined()) {
            // We don't know about the performance of the intrinsic on this backend.
            // Alternatively, this backend doesn't even have an intrinsic.
            if (ii.native_func.is_fast) {
                if (ii.native_func.behavior == ApproximationPrecision::AUTO) {
                    prec.optimized_for = iida.reasonable_behavior;
                } else {
                    prec.optimized_for = ii.native_func.behavior;
                }
            } else {
                // Function is slow, intrinsic doesn't exist, so let's use our own polynomials,
                // where we define what we think is a reasonable default for OO.
                prec.optimized_for = iida.reasonable_behavior;
            }
        } else {
            // User doesn't care about the optimization objective: let's prefer the
            // intrinsic, as that's fastest.
            prec.optimized_for = ii.intrinsic.behavior;
        }
    }

    if (!prec.force_halide_polynomial) {
        if (prec.constraint_max_absolute_error == 0.0f && prec.constraint_max_ulp_error == 0) {
            // User didn't specify a desired precision. We will prefer intrinsics (which are fast)
            // or else simply use a reasonable value.
            if (ii.intrinsic.defined() && prec.optimized_for == ii.intrinsic.behavior) {
                // The backend intrinsic behaves the way the user wants, let's pick that!
                prec.constraint_max_absolute_error = ii.intrinsic.max_abs_error;
                prec.constraint_max_ulp_error = ii.intrinsic.max_ulp_error;
            } else if (ii.native_func.is_fast && prec.optimized_for == ii.native_func.behavior) {
                // The backend native func is fast behaves the way the user wants, let's pick that!
                prec.constraint_max_absolute_error = ii.native_func.max_abs_error;
                prec.constraint_max_ulp_error = ii.native_func.max_ulp_error;
            } else {
                prec.constraint_max_ulp_error = iida.default_mulpe;
                prec.constraint_max_absolute_error = iida.default_mae;
            }
        }
    }
    return ii;
}

bool intrinsic_satisfies_precision(const IntrinsicsInfo &ii, const ApproximationPrecision &prec) {
    if (!ii.intrinsic.defined()) {
        return false;
    }
    if (prec.force_halide_polynomial) {
        return false;  // Don't use intrinsics if the user really wants a polynomial.
    }
    if (prec.optimized_for != ii.intrinsic.behavior) {
        return false;
    }
    if (prec.constraint_max_ulp_error != 0) {
        if (ii.intrinsic.max_ulp_error != 0) {
            if (ii.intrinsic.max_ulp_error > prec.constraint_max_ulp_error) {
                return false;
            }
        } else {
            // We don't know?
        }
    }
    if (prec.constraint_max_absolute_error != 0) {
        if (ii.intrinsic.max_abs_error != 0) {
            if (ii.intrinsic.max_abs_error > prec.constraint_max_absolute_error) {
                return false;
            }
        } else {
            // We don't know?
        }
    }
    return true;
}

bool native_func_satisfies_precision(const IntrinsicsInfo &ii, const ApproximationPrecision &prec) {
    if (prec.force_halide_polynomial) {
        return false;  // Don't use native functions if the user really wants a polynomial.
    }
    if (!ii.native_func.defined()) {
        return true;  // Unspecified means it's exact.
    }
    if (prec.optimized_for != ii.native_func.behavior) {
        return false;
    }
    if (prec.constraint_max_ulp_error != 0) {
        if (ii.native_func.max_ulp_error != 0) {
            if (ii.native_func.max_ulp_error > prec.constraint_max_ulp_error) {
                return false;
            }
        } else {
            // We don't know?
        }
    }
    if (prec.constraint_max_absolute_error != 0) {
        if (ii.native_func.max_abs_error != 0) {
            if (ii.native_func.max_abs_error > prec.constraint_max_absolute_error) {
                return false;
            }
        } else {
            // We don't know?
        }
    }
    return true;
}

class LowerFastMathFunctions : public IRMutator {
    using IRMutator::visit;

    const Target &target;
    DeviceAPI for_device_api = DeviceAPI::None;

    bool is_cuda_cc20() {
        return for_device_api == DeviceAPI::CUDA && target.get_cuda_capability_lower_bound() >= 20;
    }
    bool is_cuda_cc75() {
        return for_device_api == DeviceAPI::CUDA && target.get_cuda_capability_lower_bound() >= 75;
    }

    void adjust_precision_for_target(ApproximationPrecision &prec) {
        if (for_device_api == DeviceAPI::None) {
            if (target.arch == Target::Arch::X86) {
                // If we do not have fused-multiply-add, we lose some precision.
                if (target.bits == 32 || !target.has_feature(Target::Feature::FMA)) {
                    prec.constraint_max_absolute_error *= 0.5f;
                    prec.constraint_max_ulp_error /= 2;
                }
            }
        }
    }

    /** Strips the fast_ prefix, appends the type suffix, and
     * drops the precision argument from the end. */
    Expr to_native_func(const Call *op) {
        internal_assert(op->name.size() > 5);
        internal_assert(op->name.substr(0, 5) == "fast_");
        internal_assert(op->args.size() >= 2);  // At least one arg, and a precision
        std::string new_name = op->name.substr(5);
        if (op->type == Float(16)) {
            new_name += "_f16";
        } else if (op->type == Float(32)) {
            new_name += "_f32";
        } else if (op->type == Float(64)) {
            new_name += "_f64";
        }
        // Mutate args, and drop precision parameter.
        std::vector<Expr> args;
        for (size_t i = 0; i < op->args.size() - 1; ++i) {
            const Expr &arg = op->args[i];
            args.push_back(IRMutator::mutate(arg));
        }
        return Call::make(op->type, new_name, args, Call::PureExtern);
    }

    Expr append_type_suffix(const Call *op) {
        std::string new_name = op->name;
        if (op->type == Float(16)) {
            new_name += "_f16";
        } else if (op->type == Float(32)) {
            new_name += "_f32";
        } else if (op->type == Float(64)) {
            new_name += "_f64";
        }
        // Mutate args, and drop precision parameter.
        std::vector<Expr> args;
        for (size_t i = 0; i < op->args.size() - 1; ++i) {
            const Expr &arg = op->args[i];
            args.push_back(IRMutator::mutate(arg));
        }
        return Call::make(op->type, new_name, args, Call::PureExtern);
    }

    ApproximationPrecision extract_approximation_precision(const Call *op) {
        internal_assert(op);
        internal_assert(op->args.size() >= 2);
        const Call *make_ap = op->args.back().as<Call>();  // Precision is always last argument.
        internal_assert(make_ap);
        internal_assert(make_ap->is_intrinsic(Call::make_struct));
        internal_assert(make_ap->args.size() == 4);
        const IntImm *imm_optimized_for = make_ap->args[0].as<IntImm>();
        const UIntImm *imm_max_ulp_error = make_ap->args[1].as<UIntImm>();
        const FloatImm *imm_max_abs_error = make_ap->args[2].as<FloatImm>();
        const IntImm *imm_force_poly = make_ap->args[3].as<IntImm>();
        internal_assert(imm_optimized_for);
        internal_assert(imm_max_ulp_error);
        internal_assert(imm_max_abs_error);
        internal_assert(imm_force_poly);
        return ApproximationPrecision{
            (ApproximationPrecision::OptimizationObjective)imm_optimized_for->value,
            imm_max_ulp_error->value,
            imm_max_abs_error->value,
            (int)imm_force_poly->value,
        };
    }

public:
    LowerFastMathFunctions(const Target &t)
        : target(t) {
    }

    Stmt visit(const For *op) override {
        if (op->device_api != DeviceAPI::None) {
            ScopedValue<DeviceAPI> bind(for_device_api, op->device_api);
            return IRMutator::visit(op);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::fast_sin)) {
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_sin, for_device_api);
            if (op->type == Float(32) && intrinsic_satisfies_precision(ii, prec)) {
                return append_type_suffix(op);
            }
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                return to_native_func(op);
            }

            // No known fast version available, we will expand our own approximation.
            adjust_precision_for_target(prec);
            return ApproxImpl::fast_sin(mutate(op->args[0]), prec);
        } else if (op->is_intrinsic(Call::fast_cos)) {
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_cos, for_device_api);
            if (op->type == Float(32) && intrinsic_satisfies_precision(ii, prec)) {
                return append_type_suffix(op);
            }
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                return to_native_func(op);
            }

            // No known fast version available, we will expand our own approximation.
            adjust_precision_for_target(prec);
            return ApproxImpl::fast_cos(mutate(op->args[0]), prec);
        } else if (op->is_intrinsic(Call::fast_atan) || op->is_intrinsic(Call::fast_atan2)) {
            // Handle fast_atan and fast_atan2 together!
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_atan_atan2, for_device_api);
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                // The native atan is fast: fall back to native and continue lowering.
                return to_native_func(op);
            }

            adjust_precision_for_target(prec);
            if (op->is_intrinsic(Call::fast_atan)) {
                return ApproxImpl::fast_atan(mutate(op->args[0]), prec);
            } else {
                return ApproxImpl::fast_atan2(mutate(op->args[0]), mutate(op->args[1]), prec);
            }
        } else if (op->is_intrinsic(Call::fast_tan)) {
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_tan, for_device_api);
            if (op->type == Float(32) && intrinsic_satisfies_precision(ii, prec)) {
                if (is_cuda_cc20()) {
                    Expr arg = mutate(op->args[0]);
                    Expr sin = Call::make(arg.type(), "fast_sin_f32", {arg}, Call::PureExtern);
                    Expr cos = Call::make(arg.type(), "fast_cos_f32", {arg}, Call::PureExtern);
                    Expr tan = Call::make(arg.type(), "fast_div_f32", {sin, cos}, Call::PureExtern);
                    return tan;
                } else {
                    return append_type_suffix(op);
                }
            }
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                // The native atan is fast: fall back to native and continue lowering.
                return to_native_func(op);
            }

            adjust_precision_for_target(prec);
            return ApproxImpl::fast_tan(mutate(op->args[0]), prec);
        } else if (op->is_intrinsic(Call::fast_exp)) {
            // Handle fast_exp and fast_log together!
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_exp, for_device_api);
            if (op->type == Float(32) && is_cuda_cc20() && intrinsic_satisfies_precision(ii, prec)) {
                Type type = op->args[0].type();
                // exp(x) = 2^(a*x) = (2^a)^x
                // 2^a = e
                // => log(2^a) = log(e)
                // => a * log(2) = 1
                // => a = 1/log(2)
                Expr ool2 = make_const(type, 1.0 / std::log(2.0));
                return Call::make(type, "fast_ex2_f32", {mutate(op->args[0]) * ool2}, Call::PureExtern);
            }
            if (op->type == Float(32) && intrinsic_satisfies_precision(ii, prec)) {
                return append_type_suffix(op);
            }
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                // The native atan is fast: fall back to native and continue lowering.
                return to_native_func(op);
            }

            adjust_precision_for_target(prec);
            return ApproxImpl::fast_exp(mutate(op->args[0]), prec);
        } else if (op->is_intrinsic(Call::fast_log)) {
            // Handle fast_exp and fast_log together!
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_log, for_device_api);
            if (op->type == Float(32) && is_cuda_cc20() && intrinsic_satisfies_precision(ii, prec)) {
                Type type = op->args[0].type();
                Expr lg = Call::make(type, "fast_lg2_f32", {mutate(op->args[0])}, Call::PureExtern);
                // log(x) = lg2(x) / lg2(e)
                // lg2(e) = log(e)/log(2)
                // => log(x) = lg2(x) / (log(e)/log(2)) = lg2(x) * (log(2) / log(e)) = log(2) * log(2)
                return lg * make_const(type, std::log(2.0));
            }
            if (op->type == Float(32) && intrinsic_satisfies_precision(ii, prec)) {
                return append_type_suffix(op);
            }
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                // The native atan is fast: fall back to native and continue lowering.
                return to_native_func(op);
            }

            adjust_precision_for_target(prec);
            return ApproxImpl::fast_log(mutate(op->args[0]), prec);
        } else if (op->is_intrinsic(Call::fast_tanh)) {
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_tanh, for_device_api);
            // We have a fast version on PTX with CC7.5
            if (op->type == Float(32) && is_cuda_cc75() && intrinsic_satisfies_precision(ii, prec)) {
                return append_type_suffix(op);
            }

            // Expand using defintion in terms of exp(2x), and recurse.
            // Note: no adjustment of precision, as the recursed mutation will take care of that!
            return mutate(ApproxImpl::fast_tanh(op->args[0], prec));
        } else if (op->is_intrinsic(Call::fast_pow)) {
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_pow, for_device_api);
            if (op->type == Float(32) && is_cuda_cc20() && !prec.force_halide_polynomial) {
                Type type = op->args[0].type();
                // Lower to 2^(lg2(x) * y), thanks to specialized instructions.
                Expr arg_x = mutate(op->args[0]);
                Expr arg_y = mutate(op->args[1]);
                Expr lg = Call::make(type, "fast_lg2_f32", {arg_x}, Call::PureExtern);
                Expr pow = Call::make(type, "fast_ex2_f32", {lg * arg_y}, Call::PureExtern);
                pow = select(arg_x == 0.0f, 0.0f, pow);
                pow = select(arg_y == 0.0f, 1.0f, pow);
                return pow;
            }
            if (op->type == Float(32) && intrinsic_satisfies_precision(ii, prec)) {
                return append_type_suffix(op);
            }
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                return to_native_func(op);
            }

            // Improve precision somewhat, as we will compound errors.
            prec.constraint_max_absolute_error *= 0.5;
            prec.constraint_max_ulp_error *= 0.5;
            // Rewrite as exp(log(x) * y), and recurse.
            Expr arg_x = mutate(op->args[0]);
            Expr arg_y = mutate(op->args[1]);
            Expr pow = mutate(Halide::fast_exp(Halide::fast_log(arg_x, prec) * arg_y, prec));
            pow = select(arg_x == 0.0f, 0.0f, pow);
            pow = select(arg_y == 0.0f, 1.0f, pow);
            return pow;
        } else if (op->is_intrinsic(Call::fast_asin)) {
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_asin_acos, for_device_api);
            if (op->type == Float(32) && intrinsic_satisfies_precision(ii, prec)) {
                return append_type_suffix(op);
            }
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                return to_native_func(op);
            }
            Expr x = mutate(op->args[0]);
            return mutate(Halide::fast_atan2(x, sqrt((1 + x) * (1 - x)), prec));
        } else if (op->is_intrinsic(Call::fast_acos)) {
            ApproximationPrecision prec = extract_approximation_precision(op);
            IntrinsicsInfo ii = resolve_precision(prec, ii_asin_acos, for_device_api);
            if (op->type == Float(32) && intrinsic_satisfies_precision(ii, prec)) {
                return append_type_suffix(op);
            }
            if (ii.native_func.is_fast && native_func_satisfies_precision(ii, prec)) {
                return to_native_func(op);
            }
            Expr x = mutate(op->args[0]);
            return mutate(Halide::fast_atan2(sqrt((1 + x) * (1 - x)), x, prec));
        } else {
            return IRMutator::visit(op);
        }
    }
};

Stmt lower_fast_math_functions(const Stmt &s, const Target &t) {
    return LowerFastMathFunctions(t).mutate(s);
}

}  // namespace Internal
}  // namespace Halide
