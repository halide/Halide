// TODO: license.
#ifndef HALIDE_APPS_HAMAL_SOFTMAX_H_
#define HALIDE_APPS_HAMAL_SOFTMAX_H_

#include <limits>
#include <string>

#include "Halide.h"

namespace hamal {
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

// Copied from halide_ext, plan is to add this to Halide.
Halide::Tuple halide_ext_exp(const Expr &x_full) {
    // Type type = x_full.type();
    // CHECK_EQ(type.element_of(), Float(32));

    const float ln2_part1 = 0.6931457519f;
    const float ln2_part2 = 1.4286067653e-6f;
    const float one_over_ln2 = 1.0f / logf(2.0f);

    Expr scaled = x_full * one_over_ln2;
    Expr k_real = floor(scaled);

    Expr x = x_full - k_real * ln2_part1;
    x -= k_real * ln2_part2;

    float coeff[] = {0.00031965933071842413f,
                     0.00119156835564003744f,
                     0.00848988645943932717f,
                     0.04160188091348320655f,
                     0.16667983794100929562f,
                     0.49999899033463041098f,
                     1.0f,
                     1.0f};
    Expr result = evaluate_polynomial(x, coeff, sizeof(coeff) / sizeof(coeff[0]));

    result = Halide::Internal::common_subexpression_elimination(result);

    return {result, k_real};
}

}  // anonymous namespace

struct Softmax : public Halide::NamesInterface {
    Softmax(const std::string &base_name)
        : base_name(base_name),
          result(base_name + "_softmax"),
          ext_exp(base_name + "_softmax_ext_exp"),
          exponentials(base_name + "_softmax_exponentials"),
          softmax_sums(base_name + "_softmax_sum") {
    }
    std::string base_name;
    Func result;
    Func ext_exp;
    Func exponentials;
    Func softmax_sums;
    Var result_inner;
    RVar softmax_sum_inner;  // TODO: Remove this.
    Var softmax_sum_inner_var;
    LoopLevel softmax_sum_compute_at;

    // Keeping this to either use for testing or turn into a comment.
#if 0
  void naive_algorithm(Func input, const Type &generating_type) {
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

    result(args) = cast(generating_type,
                        input(args) / select(softmax_sum(args_sum) < Expr(1e-5),
                                             1, softmax_sum(args_sum)));
    result_inner = args[0];
  }
#endif

    // Implementation based on the algorithm in
    // https://arxiv.org/pdf/2001.04438.pdf
    void apply(Func input, Expr size, const Type &generating_type) {
        auto args = input.args();
        RDom r(0, size);

        // TODO: avoid needing double here
        ext_exp(args) = halide_ext_exp(cast<double>(input(args)));

        std::vector<Var> args_inner(args.begin() + 1, args.end());
        std::vector<Expr> args_reduction;
        args_reduction.emplace_back(r.x);
        args_reduction.insert(args_reduction.end(), args_inner.begin(),
                              args_inner.end());

        // This reduction maintains a Tuple of with the sum and the maximum exponent
        // so far, both as floating point numbers.
        softmax_sums(args_inner) =
            Tuple(cast<double>(0), Expr(std::numeric_limits<double>::lowest()));
        Expr running_max_exp =
            max(softmax_sums(args_inner)[1], ext_exp(args_reduction)[1]);
        Expr m_sub_i_term = ext_exp(args_reduction)[0] *
                            pow(2.0f, ext_exp(args_reduction)[1] - running_max_exp);
        Expr m_sum_term = softmax_sums(args_inner)[0] *
                          pow(2.0f, softmax_sums(args_inner)[1] - running_max_exp);
        Expr running_sum = m_sub_i_term + m_sum_term;
        softmax_sums(args_inner) = Tuple(running_sum, running_max_exp);
        Expr lambda = 1 / softmax_sums(args_inner)[0];
        Expr t =
            cast(generating_type,
                 ext_exp(args)[0] * lambda *
                     pow(2.0f, ext_exp(args)[1] - softmax_sums(args_inner)[1]));
        result(args) = t;
        result_inner = args[0];
        softmax_sum_inner = r;
        softmax_sum_inner_var = args_inner[0];
        softmax_sum_compute_at = LoopLevel(result, args[1]);
    }

    void default_schedule(LoopLevel result_loop_level, const Target &t,
                          bool vectorize) {
        ext_exp.compute_inline();
        softmax_sums.compute_at(softmax_sum_compute_at)
            .store_in(MemoryType::Register)
            .vectorize(softmax_sum_inner_var, t.natural_vector_size<float>())
            .update(0)
            .unscheduled();
        result.compute_at(result_loop_level);
        if (vectorize) {
            // In some modes, this dimension is narrow and we don't want to vectorize
            // it
            result.vectorize(result_inner, t.natural_vector_size<double>());
        }
    }
};

}  // namespace hamal

#endif  // HALIDE_APPS_HAMAL_SOFTMAX_H_
