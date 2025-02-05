#include "FastMathFunctions.h"

#include "IRMutator.h"
#include "IROperator.h"
#include "ApproximationTables.h"
#include "CSE.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

// Implemented in IROperator.cpp
void range_reduce_log(const Expr &input, Expr *reduced, Expr *exponent);

namespace ApproxImpl {

constexpr double PI = 3.14159265358979323846;
constexpr double ONE_OVER_PI = 1.0 / PI;
constexpr double TWO_OVER_PI = 2.0 / PI;
constexpr double PI_OVER_TWO = PI / 2;

Expr constant(Type t, double value) {
    if (t == Float(64)) {
        return Expr(value);
    }
    if (t == Float(32)) {
        return Expr(float(value));
    }
    internal_error << "Constants only for double or float.";
    return 0;
}

Expr fast_sincos_helper(const Expr &x_full, bool is_sin, ApproximationPrecision precision) {
    Type type = x_full.type();
    // Range reduction to interval [0, pi/2] which corresponds to a quadrant of the circle.
    Expr scaled = x_full * constant(type, TWO_OVER_PI);
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);
    Expr k_mod4 = k % 4;
    Expr sin_usecos = is_sin ? ((k_mod4 == 1) || (k_mod4 == 3)) : ((k_mod4 == 0) || (k_mod4 == 2));
    // sin_usecos = !sin_usecos;
    Expr flip_sign = is_sin ? (k_mod4 > 1) : ((k_mod4 == 1) || (k_mod4 == 2));

    // Reduce the angle modulo pi/2: i.e., to the angle within the quadrant.
    Expr x = x_full - k_real * constant(type, PI_OVER_TWO);
    x = select(sin_usecos, constant(type, PI_OVER_TWO) - x, x);

    const Internal::Approximation *approx = Internal::best_sin_approximation(precision, type);
    // const Internal::Approximation *approx = Internal::best_cos_approximation(precision);
    const std::vector<double> &c = approx->coefficients;
    Expr x2 = x * x;
    Expr result = constant(type, c.back());
    for (size_t i = 1; i < c.size(); ++i) {
        result = x2 * result + constant(type, c[c.size() - i - 1]);
    }
    result *= x;
    result = select(flip_sign, -result, result);
    return common_subexpression_elimination(result, true);
}

Expr fast_sin(const Expr &x, ApproximationPrecision precision) {
    return fast_sincos_helper(x, true, precision);
}

Expr fast_cos(const Expr &x, ApproximationPrecision precision) {
    return fast_sincos_helper(x, false, precision);
}

#define TAN_PADE_APPROXIMANT 0
Expr fast_tan_helper(const Expr &x, ApproximationPrecision precision) {
  Type type = x.type();
  // x is assumed to be reduced to [-pi/2, pi/2]!
#if !TAN_PADE_APPROXIMANT
    const Internal::Approximation *approx = Internal::best_tan_approximation(precision, type);
    const std::vector<double> &c = approx->coefficients;
    Expr x2 = x * x;
    Expr result = constant(type, c.back());
    for (size_t i = 1; i < c.size(); ++i) {
        result = result * x2 + constant(type, c[c.size() - i - 1]);
    }
    result = result * x2 + constant(type, 1); // omitted term from table.
    result *= x;
    return result;
#else // PADE APPROXIMANT
  Expr x2 = x * x;
  Expr num, denom;
  //if (precision.constraint_max_absolute_error >= 2e-2 && false) {
  //  // (105 x - 10 x^3)/(x^4 - 45 x^2 + 105)
  //  num = constant(type, -10);
  //  num = num * x2 + constant(type, 105);
  //  num = num * x;
  //  denom = constant(type, +1);
  //  denom = denom * x2 + constant(type, -45);
  //  denom = denom * x2 + constant(type, +105);
  //} else if (precision.constraint_max_absolute_error >= 2e-3 || true) {
  //  // (x^5 - 105 x^3 + 945 x)/(15 x^4 - 420 x^2 + 945)
  //  num = constant(type, +1);
  //  num = num * x2 + constant(type, -105);
  //  num = num * x2 + constant(type, +945);
  //  num = num * x;
  //  denom = constant(type, +15);
  //  denom = denom * x2 + constant(type, -420);
  //  denom = denom * x2 + constant(type, +945);
  //} else if (precision.constraint_max_absolute_error >= 5e-5) {
  //  // (-21 x^5 + 1260 x^3 - 10395 x)/(x^6 - 210 x^4 + 4725 x^2 - 10395)
  //  num = constant(type, -21);
  //  num = num * x2 + constant(type, +1260);
  //  num = num * x2 + constant(type, -10395);
  //  num = num * x;
  //  denom = constant(type, +1);
  //  denom = denom * x2 + constant(type, -210);
  //  denom = denom * x2 + constant(type, +4725);
  //  denom = denom * x2 + constant(type, -10395);
  //} else if (precision.constraint_max_absolute_error >= 4e-5) {
  //  // (x^7 - 378 x^5 + 17325 x^3 - 135135 x)/(28 x^6 - 3150 x^4 + 62370 x^2 - 135135)
    num = constant(type, +1);
    num = num * x2 + constant(type, -378);
    num = num * x2 + constant(type, +17325);
    num = num * x2 + constant(type, -135135);
    num = num * x;
    denom = constant(type, +28);
    denom = denom * x2 + constant(type, -3150);
    denom = denom * x2 + constant(type, +62370);
    denom = denom * x2 + constant(type, -135135);
  //} else {
  //  // (-36 x^7 + 6930 x^5 - 270270 x^3 + 2027025 x)/(x^8 - 630 x^6 + 51975 x^4 - 945945 x^2 + 2027025)
  //  num = constant(type, -36);
  //  num = num * x2 + constant(type, +6930);
  //  num = num * x2 + constant(type, -270270);
  //  num = num * x2 + constant(type, +2027025);
  //  num = num * x;
  //  denom = constant(type, +1);
  //  denom = denom * x2 + constant(type, -630);
  //  denom = denom * x2 + constant(type, +51975);
  //  denom = denom * x2 + constant(type, -945945);
  //  denom = denom * x2 + constant(type, +2027025);
  //}
  return num / denom;
#endif
}

Expr fast_tan(const Expr &x_full, ApproximationPrecision precision) {
  Type type = x_full.type();

  // Reduce range to [-pi/2, pi/2]
  Expr scaled = x_full * constant(type, ONE_OVER_PI);
  Expr k_real = round(scaled);

  Expr x = x_full - k_real * constant(type, PI);
#if TAN_PADE_APPROXIMANT
  return fast_tan_helper(x, precision);
#endif

  Expr abs_x = abs(x);
  Expr flip = x < constant(type, 0.0);
  Expr use_cotan = abs_x > constant(type, PI / 4.0);
  Expr arg = select(use_cotan, constant(type, PI_OVER_TWO) - abs_x, x);
  // Change the precision, because we need slighly higher accuracy
  // for the inverted branch (tan(x) = 1/tan(pi/2-x)).
  ApproximationPrecision adj_prec = precision;
  adj_prec.constraint_max_absolute_error *= 0.1f;
  adj_prec.constraint_max_ulp_error /= 4;
  Expr tan_of_arg = fast_tan_helper(arg, adj_prec);
  return select(use_cotan, constant(type, 1) / select(flip, -tan_of_arg, tan_of_arg), tan_of_arg);
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
        x = select(x_gt_1, constant(type, 1.0) / x_full, x_full);
    }
    const Internal::Approximation *approx = Internal::best_atan_approximation(precision, type);
    const std::vector<double> &c = approx->coefficients;
    Expr x2 = x * x;
    Expr result = constant(type, c.back());
    for (size_t i = 1; i < c.size(); ++i) {
        result = x2 * result + constant(type, c[c.size() - i - 1]);
    }
    result *= x;

    if (!between_m1_and_p1) {
        result = select(x_gt_1, select(x_full < 0, constant(type, -PI_OVER_TWO), constant(type, PI_OVER_TWO)) - result, result);
    }
    return common_subexpression_elimination(result, true);
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
    Expr ati = fast_atan_helper(atan_input, precision, true);
    Expr pi_over_two = constant(type, PI_OVER_TWO);
    Expr pi = constant(type, PI);
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
    return common_subexpression_elimination(result, true);
}

Expr fast_exp(const Expr &x_full, ApproximationPrecision prec) {
    Type type = x_full.type();
    user_assert(x_full.type() == Float(32)) << "fast_exp only works for Float(32)";

    Expr log2 = constant(type, std::log(2.0));

    Expr scaled = x_full / log2;
    Expr k_real = floor(scaled);
    Expr k = cast<int>(k_real);
    Expr x = x_full - k_real * log2;

#if 0
    float coeff[] = {
        0.01314350012789660196f,
        0.03668965196652099192f,
        0.16873890085469545053f,
        0.49970514590562437052f,
        1.0f,
        1.0f};
    Expr result = evaluate_polynomial(x, coeff, sizeof(coeff) / sizeof(coeff[0]));
#else
    const Internal::Approximation *approx = Internal::best_exp_approximation(prec, type);
    const std::vector<double> &c = approx->coefficients;

    Expr result = constant(type, c.back());
    for (size_t i = 1; i < c.size(); ++i) {
        result = x * result + constant(type, c[c.size() - i - 1]);
    }
    result = result * x + constant(type, 1.0); // Term omitted from table.
    result = result * x + constant(type, 1.0); // Term omitted from table.
#endif

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

    Expr log2 = constant(type, std::log(2.0));
    Expr reduced, exponent;
    range_reduce_log(x, &reduced, &exponent);

    Expr x1 = reduced - 1.0f;
#if 0
    float coeff[] = {
        0.07640318789187280912f,
        -0.16252961013874300811f,
        0.20625219040645212387f,
        -0.25110261010892864775f,
        0.33320464908377461777f,
        -0.49997513376789826101f,
        1.0f,
        0.0f};

    Expr result = evaluate_polynomial(x1, coeff, sizeof(coeff) / sizeof(coeff[0]));
#else
    const Internal::Approximation *approx = Internal::best_log_approximation(prec, type);
    const std::vector<double> &c = approx->coefficients;

    Expr result = constant(type, c.back());
    for (size_t i = 1; i < c.size(); ++i) {
        result = x1 * result + constant(type, c[c.size() - i - 1]);
    }
    result = result * x1;
#endif
    result = result + cast<float>(exponent) * log2;
    result = common_subexpression_elimination(result);
    return result;
}

}  // namespace


class LowerFastMathFunctions : public IRMutator {
  using IRMutator::visit;

  const Target &target;
  DeviceAPI for_device_api = DeviceAPI::None;

  bool is_cuda_cc20() {
    return for_device_api == DeviceAPI::CUDA;
  }
  bool is_cuda_cc70() {
    return for_device_api == DeviceAPI::CUDA && target.has_feature(Target::CUDACapability50);
  }

  bool is_vulkan() { return for_device_api == DeviceAPI::Vulkan; }
  bool is_metal() { return for_device_api == DeviceAPI::Metal; }
  bool is_opencl() { return for_device_api == DeviceAPI::Metal; }
  bool is_webgpu() { return for_device_api == DeviceAPI::WebGPU; }
  bool native_sincos_is_fast(Type type) {
    if (type == Float(32)) {
      return is_vulkan() || is_metal() || is_webgpu();
    } else {
      return false;
    }
  }
  bool native_atan_is_fast(Type type) {
    if (type == Float(32)) {
      return is_vulkan() || is_metal() || is_webgpu();
    } else {
      return false;
    }
  }
  bool native_exp_is_fast(Type type) {
    if (type == Float(32)) {
      // exp() on metal is fast (unlike log)!
      return is_opencl() || is_vulkan() || is_metal() || is_webgpu();
    } else {
      return false;
    }
  }
  bool native_log_is_fast(Type type) {
    if (type == Float(32)) {
      // log() on metal is slow (unlike exp)!
      return is_opencl() || is_vulkan() || is_webgpu();
    } else {
      return false;
    }
  }
  bool native_pow_is_fast(Type type) {
    if (type == Float(32)) {
      return false; // TODO figure out which ones!
    } else {
      return false;
    }
  }

  /** Strips the fast_ prefix, appends the type suffix, and
   * drops the precision argument from the end. */
  Expr to_native_func(const Call *op) {
    internal_assert(op->name.size() > 5);
    internal_assert(op->name.substr(0, 5) == "fast_");
    internal_assert(op->args.size() >= 2); // At least one arg, and a precision
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

  const FloatImm *get_float_imm(const Expr &e) {
    if (const Call *c = e.as<Call>()) {
      internal_assert(c->is_intrinsic(Call::strict_float));
      return get_float_imm(c->args[0]);
    } else {
      return e.as<FloatImm>();
    }
  }

  ApproximationPrecision extract_approximation_precision(const Call *op) {
    internal_assert(op);
    internal_assert(op->args.size() >= 2);
    const Call *make_ap = op->args.back().as<Call>(); // Precision is always last argument.
    internal_assert(make_ap);
    internal_assert(make_ap->is_intrinsic(Call::make_struct));
    internal_assert(make_ap->args.size() == 5);
    const IntImm *imm_optimized_for = make_ap->args[0].as<IntImm>();
    const IntImm *imm_min_poly_terms = make_ap->args[1].as<IntImm>();
    const IntImm *imm_max_ulp_error = make_ap->args[2].as<IntImm>();
    const FloatImm *imm_max_abs_error = get_float_imm(make_ap->args[3]);
    const IntImm *imm_allow_native = make_ap->args[4].as<IntImm>();
    internal_assert(imm_optimized_for);
    internal_assert(imm_min_poly_terms);
    internal_assert(imm_max_abs_error);
    internal_assert(imm_allow_native);
    return ApproximationPrecision{
        (ApproximationPrecision::OptimizationObjective) imm_optimized_for->value,
        (int) imm_min_poly_terms->value,
        (int) imm_max_ulp_error->value,
        (float) imm_max_abs_error->value,
        (bool) imm_allow_native->value,
    };
  }

  public:
  LowerFastMathFunctions(const Target &t) : target(t) { }

  Stmt visit(const For *op) override {
    if (op->device_api != DeviceAPI::None) {
      ScopedValue<DeviceAPI> bind(for_device_api, op->device_api);
      return IRMutator::visit(op);
    } else {
      return IRMutator::visit(op);
    }
  }

  Expr visit(const Call *op) override {
      if (op->is_intrinsic(Call::fast_sin) || op->is_intrinsic(Call::fast_cos)) {
        // Handle fast_sin and fast_cos together!
        ApproximationPrecision prec = extract_approximation_precision(op);
        if (op->type == Float(32) && is_cuda_cc20() && prec.allow_native_when_faster) {
          // We have an intrinsic in the ptx.ll module with the same name.
          return append_type_suffix(op);
        } else if (native_sincos_is_fast(op->type) && prec.allow_native_when_faster) {
          // The native sine and cosine are fast: fall back to native and continue lowering.
          return to_native_func(op);
        } else {
          // No known fast version available, we will expand our own approximation.
          if (op->is_intrinsic(Call::fast_sin)) {
            return ApproxImpl::fast_sin(mutate(op->args[0]), prec);
          } else {
            return ApproxImpl::fast_cos(mutate(op->args[0]), prec);
          }
        }
      } else if (op->is_intrinsic(Call::fast_atan) || op->is_intrinsic(Call::fast_atan2)) {
        // Handle fast_atan and fast_atan2 together!
        ApproximationPrecision prec = extract_approximation_precision(op);
        if (native_atan_is_fast(op->type) && prec.allow_native_when_faster) {
          // The native atan is fast: fall back to native and continue lowering.
          return to_native_func(op);
        } else {
          if (op->is_intrinsic(Call::fast_atan)) {
            return ApproxImpl::fast_atan(mutate(op->args[0]), prec);
          } else {
            return ApproxImpl::fast_atan2(mutate(op->args[0]), mutate(op->args[1]), prec);
          }
        }
      } else if (op->is_intrinsic(Call::fast_tan)) {
        ApproximationPrecision prec = extract_approximation_precision(op);
        return ApproxImpl::fast_tan(mutate(op->args[0]), prec);
      } else if (op->is_intrinsic(Call::fast_exp)) {
        // Handle fast_exp and fast_log together!
        ApproximationPrecision prec = extract_approximation_precision(op);
        if (native_exp_is_fast(op->type) && prec.allow_native_when_faster) {
          // The native atan is fast: fall back to native and continue lowering.
          return to_native_func(op);
        } else {
          return ApproxImpl::fast_exp(mutate(op->args[0]), prec);
        }
      } else if (op->is_intrinsic(Call::fast_log)) {
        // Handle fast_exp and fast_log together!
        ApproximationPrecision prec = extract_approximation_precision(op);
        if (native_log_is_fast(op->type) && prec.allow_native_when_faster) {
          // The native atan is fast: fall back to native and continue lowering.
          return to_native_func(op);
        } else {
          return ApproxImpl::fast_log(mutate(op->args[0]), prec);
        }
      } else if (op->is_intrinsic(Call::fast_tanh)) {
        // We have a fast version on PTX
        if (is_cuda_cc70()) {
          return append_type_suffix(op);
        } else {
          // Unfortunately, no fast_tanh approximation implemented yet!
          return to_native_func(op);
        }
      } else if (op->is_intrinsic(Call::fast_pow)) {
        ApproximationPrecision prec = extract_approximation_precision(op);
        if (native_pow_is_fast(op->type) && prec.allow_native_when_faster) {
          return to_native_func(op);
        } else {
          // Rewrite as exp(log(x) * y), and recurse.
          const Expr &x = op->args[0];
          const Expr &y = op->args[1];
          return select(x == 0.0f, 0.0f, mutate(Halide::fast_exp(Halide::fast_log(x, prec) * y, prec)));
        }
      }
      else {
        return IRMutator::visit(op);
      }
  }

};

Stmt lower_fast_math_functions(const Stmt &s, const Target &t) {
  return LowerFastMathFunctions(t).mutate(s);
}

}
}
