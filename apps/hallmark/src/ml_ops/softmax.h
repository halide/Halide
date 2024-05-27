// TODO: license.
#ifndef HALIDE_APPS_HALLMARK_SOFTMAX_H_
#define HALIDE_APPS_HALLMARK_SOFTMAX_H_

#include <limits>
#include <string>

#include "Halide.h"

namespace hallmark {
namespace {

using Halide::Expr;
using Halide::Float;
using Halide::Type;

Expr evaluate_polynomial(Expr x, float *coeff, int n) {
    Expr x2 = x * x;

    Expr even_terms = coeff[0];
    Expr odd_terms = coeff[1];

    for (int i = 2; i < n; i++) {
        if ((i & 1) == 0) {
            if (coeff[i] == 0.0f) {
                even_terms *= x2;
            } else {
                even_terms = even_terms * x2 + coeff[i];
            }
        } else {
            if (coeff[i] == 0.0f) {
                odd_terms *= x2;
            } else {
                odd_terms = odd_terms * x2 + coeff[i];
            }
        }
    }

    if ((n & 1) == 0) {
        return even_terms * std::move(x) + odd_terms;
    } else {
        return odd_terms * std::move(x) + even_terms;
    }
}

/* Extended exponential which produces two output values,
 * each of the same precision as the input, as described in
 * "The Two-Pass Softmax Algorithm" by Marat Dukhan and
 * Artsiom Ablavatski [https://arxiv.org/abs/2001.04438].
 *
 * The first element of the returned Tuple is a psuedo-mantissa while
 * the second is an exponent which is an integer. The product of the
 * pseudo-mantissa and 2 raised to the returned exponent is the
 * desired result e^a.  For arguments up to slightly greater than
 * 11629079, the pseudo-mantissa is guaranteed to be within the
 * interval (-e, e). For larger arguments, the exponent result of the
 * tuple may not be able to represent the exact integer necessary to
 * keep the pseudo-mantissa within bounds. Thus it can become
 * progressively larger in magnitude as the argument increases.
 *
 * Ideally this routine will maintain a degree of accuracy through the
 * entire range and be able to produce results out to the end of the
 * numeric range. At present neither of these properties are true due to
 * the following issues:
 *  - Range reduction may overflow when scaling the argument.
 *  - Range reduction is increasingly inaccurate in reducing the value
 *    due to the implementation. This results in overflow in the polynomial
 *    evaluation.
 *  - Even if the above to issues were resolved, the approximation polynomial
 *    would have to run on values outside its intended approximation range.
 */
Halide::Tuple extended_exp(const Expr &x_full) {
    float ln2_part1 = 0.6931457519f;
    float ln2_part2 = 1.4286067653e-6f;
    float one_over_ln2 = 1.0f / logf(2.0f);

    Expr scaled = x_full * one_over_ln2;
    Expr k_real = floor(scaled);

    Expr x = x_full - k_real * ln2_part1;
    x = x - k_real * ln2_part2;

    float coeff[] = {
        0.00031965933071842413f,
        0.00119156835564003744f,
        0.00848988645943932717f,
        0.04160188091348320655f,
        0.16667983794100929562f,
        0.49999899033463041098f,
        1.0f,
        1.0f};
    Expr result = evaluate_polynomial(x, coeff, sizeof(coeff) / sizeof(coeff[0]));

    // Ensure that the mantissa part is not a NaN or itself an infinity.
    result = strict_float(select(!is_finite(k_real), 1, result));
    result = common_subexpression_elimination(result);

    return {result, k_real};
}

}  // anonymous namespace

struct Softmax : public Halide::NamesInterface {
    enum class Algorithm {
      Naive,
      TwoPass,
      ThreePass,
    };

    Softmax(const std::string &base_name,
            Algorithm algorithm = Algorithm::TwoPass)
        : base_name(base_name),
          algorithm(algorithm),
          result(base_name + "_softmax"),
          ext_exp(base_name + "_softmax_ext_exp"),
          exponentials(base_name + "_softmax_exponentials"),
          softmax_sum(base_name + "_softmax_sum") {
    }
    std::string base_name;
    Algorithm algorithm;
    Func result;

    // Naive algorithm
    Func exponentials;

    // Two pass algorithm
    Func ext_exp;

    // Three pass algorithm
    Func max_bias;
    Func biased_exp;
  
    // Common to different algorithms
    Func softmax_sum;
    Var result_inner;
    RVar softmax_sum_inner;  // TODO: Remove this.
    Var softmax_sum_inner_var;
    LoopLevel softmax_sum_compute_at;

    void apply(Func input, Expr size, const Type &generating_type) {
        switch (algorithm) {
          case Algorithm::Naive:
            naive_algorithm(input, size, generating_type);
            break;
          case Algorithm::TwoPass:
            two_pass_algorithm(input, size, generating_type);
            break;
          case Algorithm::ThreePass:
            three_pass_algorithm(input, size, generating_type);
            break;
        };
    }

    void naive_algorithm(Func input, Expr size, const Type &generating_type) {
        auto args = input.args();
        RDom r(0, size);

        exponentials(args) =
            default_exp(cast<double>(clamp(input(args), -1e12f, 1e12f)));

        std::vector<Var> args_sum(args.begin() + 1, args.end());
        std::vector<Expr> args_reduction;
        args_reduction.emplace_back(r.x);
        args_reduction.insert(args_reduction.end(), args_sum.begin(),
                              args_sum.end());

        softmax_sum(args_sum) = Expr(0.0);
        softmax_sum(args_sum) += exponentials(args_reduction);
        softmax_sum_inner = r.x;
        softmax_sum_inner_var = args_sum[0];

        result(args) = cast(generating_type,
                            input(args) / select(softmax_sum(args_sum) < Expr(1e-5),
                                                 1, softmax_sum(args_sum)));
        result_inner = args[0];
        softmax_sum_compute_at = LoopLevel(result, args[1]);
    }

    // Implementation based on the algorithm in
    // https://arxiv.org/pdf/2001.04438.pdf
    void two_pass_algorithm(Func input, Expr size, const Type &generating_type) {
        auto args = input.args();
        RDom r(0, size);

        // TODO: It should not be necessary to use double for computation here.
#define USE_DOUBLE 1
#if USE_DOUBLE
        ext_exp(args) = extended_exp(cast<double>(input(args)));
#else
        ext_exp(args) = extended_exp(input(args));
#endif

        std::vector<Var> args_inner(args.begin() + 1, args.end());
        std::vector<Expr> args_reduction;
        args_reduction.emplace_back(r.x);
        args_reduction.insert(args_reduction.end(), args_inner.begin(),
                              args_inner.end());

        // This reduction maintains a Tuple of with the sum and the maximum exponent
        // so far, both as floating point numbers.
        softmax_sum(args_inner) =
#if USE_DOUBLE
            Halide::Tuple(Expr(0.0), Expr(std::numeric_limits<double>::lowest()));
#else
            Halide::Tuple(0.0f, Expr(std::numeric_limits<float>::lowest()));
#endif
        Expr running_max_exp =
            max(softmax_sum(args_inner)[1], ext_exp(args_reduction)[1]);
        Expr m_sub_i_term = ext_exp(args_reduction)[0] *
                            pow(2.0f, ext_exp(args_reduction)[1] - running_max_exp);
        Expr m_sum_term = softmax_sum(args_inner)[0] *
                          pow(2.0f, softmax_sum(args_inner)[1] - running_max_exp);
        Expr running_sum = m_sub_i_term + m_sum_term;
        softmax_sum(args_inner) = Tuple(running_sum, running_max_exp);
        Expr lambda = 1 / softmax_sum(args_inner)[0];
        Expr t =
            cast(generating_type,
                 ext_exp(args)[0] * lambda *
                     pow(2.0f, ext_exp(args)[1] - softmax_sum(args_inner)[1]));
        result(args) = t;
        result_inner = args[0];
        softmax_sum_inner = r;
        softmax_sum_inner_var = args_inner[0];
        softmax_sum_compute_at = LoopLevel(result, args[1]);
    }

    void three_pass_algorithm(Func input, Expr size, const Type &generating_type) {
        auto args = input.args();
        RDom r(0, size);

        std::vector<Var> args_inner(args.begin() + 1, args.end());
        std::vector<Expr> args_reduction;
        args_reduction.emplace_back(r.x);
        args_reduction.insert(args_reduction.end(), args_inner.begin(),
                              args_inner.end());

        max_bias(args_inner) = std::numeric_limits<float>::lowest();
        max_bias(args_inner) = max(max_bias(args_inner), input(args_reduction));

        biased_exp(args) = halide_exp(input(args) - max_bias(args_inner));
        softmax_sum(args_inner) = 0.0f;
        softmax_sum(args_inner) += biased_exp(args_reduction);

        Expr lambda = 1 / softmax_sum(args_inner);
        result(args) = halide_exp(input(args) - max_bias(args_inner)) * lambda;
        result_inner = args[0];
        softmax_sum_inner = r;
        softmax_sum_inner_var = args_inner[0];
        softmax_sum_compute_at = LoopLevel(result, args[1]);
    }

    // TODO: add support for resuse vs. recompute scheduling on exp operations.
  
    void default_schedule(LoopLevel result_loop_level, const Target &t,
                          bool vectorize) {
        if (algorithm == Algorithm::Naive) {
            exponentials.compute_at(softmax_sum_compute_at);
        } else if (algorithm == Algorithm::TwoPass) {
            ext_exp.compute_inline();
        } else if (algorithm == Algorithm::ThreePass) {
          max_bias.compute_at(softmax_sum_compute_at);
            // TODO: vectorize max loop, maybe parallelize
          biased_exp.compute_at(softmax_sum_compute_at);
        }
        softmax_sum.compute_at(softmax_sum_compute_at)
            .store_in(MemoryType::Register)
            .vectorize(softmax_sum_inner_var, t.natural_vector_size<float>())
            .update(0)
            .unscheduled();
        result.compute_at(result_loop_level);
        if (vectorize) {
            // In some modes, this dimension is narrow and we don't want to vectorize
            // it
#if USE_DOUBLE
            result.vectorize(result_inner, t.natural_vector_size<double>());
#else
            result.vectorize(result_inner, t.natural_vector_size<float>());
#endif
        }
    }
};

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_SOFTMAX_H_
