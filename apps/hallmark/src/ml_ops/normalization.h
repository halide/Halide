#ifndef HALIDE_APPS_HALLMARK_NORMALIZATION_H_
#define HALIDE_APPS_HALLMARK_NORMALIZATION_H_

#include <string>

#include "Halide.h"

namespace hallmark {

// TODO: Rename to NormalizationMethod or NormalizationKind?
enum class NormalizationKind {
    None,
    RMS,
    Layer,
};

inline const std::map<std::string, NormalizationKind> normalization_kind_names = {
    {"none", NormalizationKind::None},
    {"rms", NormalizationKind::RMS},
    {"layer", NormalizationKind::Layer},
};

struct Normalization : public Halide::NamesInterface {
    Normalization(const std::string &base_name)
        : base_name(base_name),
          result(base_name + "_apply_norm"),
          norm_sum(base_name + "_apply_norm_sum"),
          clamped_rms(base_name + "_apply_norm_clamped_rms"),
          diff(base_name + "_apply_norm_diff"),
          var(base_name + "_apply_norm_var"),
          stddev(base_name + "_apply_norm_stddev") {
    }

    std::string base_name;
    Func result;
    Func norm_sum;
    Func clamped_rms;
    Func diff;
    Func var;
    Func stddev;
    RVar norm_sum_range;

    NormalizationKind norm_kind;
    Halide::GeneratorInput<Buffer<>> *rms_weight_input;
    Func weights;
    Halide::GeneratorInput<Buffer<>> *gamma_input;
    Halide::GeneratorInput<Buffer<>> *beta_input;
    Func gamma{"gamma"};
    Func beta{"beta"};
    Expr epsilon;
    Type processing_type;
    std::vector<Var> args_norm_sum;

    // TODO: Make into a constructor and use pointers in generator.
    void add_inputs(NormalizationKind norm_kind_arg,
                    const Halide::Type &processing_type_arg,
                    Halide::Internal::GeneratorBase *generator,
                    int arg_count = 1) {
        processing_type = processing_type_arg;
        norm_kind = norm_kind_arg;
        if (norm_kind == NormalizationKind::RMS) {
            rms_weight_input = generator->add_input<Buffer<>>(base_name + "_rms_weights", processing_type, arg_count);
        } else if (norm_kind == NormalizationKind::Layer) {
            // TODO: fill in.
        }
    }

    void apply(Func input, Expr size) {
        std::vector<Var> args = input.args();
        if (norm_kind == NormalizationKind::None) {
            // It's important that we always create a distinct function for scheduling purposes
            result = input;
        } else {
            Expr zero = cast(processing_type, 0);

            // Probably should make the splitting up of the operation part of the API
            // as it affects the result. There are two goals: avoiding overflow and
            // allowing efficient parallel computation.
            RDom r(0, size, "apply_norm_sum_range");

            args_norm_sum = std::vector<Var>{args.begin() + 1, args.end()};
            std::vector<Expr> args_reduction;
            args_reduction.emplace_back(r);
            args_reduction.insert(args_reduction.end(),
                                  args_norm_sum.begin(), args_norm_sum.end());
            norm_sum(args_norm_sum) = zero;
            norm_sum(args_norm_sum) += input(args_reduction) * input(args_reduction);
            norm_sum_range = r.x;
            if (norm_kind == NormalizationKind::RMS) {
                // Can't set this up in the configure call so has to happen here.
                if (!weights.defined()) {
                    weights = *rms_weight_input;
                }
                clamped_rms(args_norm_sum) = max(cast(processing_type, 1e-6f),
                                                 sqrt(norm_sum(args_norm_sum) / size));
                std::vector<Var> weights_args(args.begin(),
                                              args.begin() + weights.args().size());
                result(args) = (input(args) / clamped_rms(args_norm_sum)) *
                               (1 + weights(weights_args));
            } else if (norm_kind == NormalizationKind::Layer) {
                diff(args) = input(args) - sqrt(norm_sum(args_norm_sum));
                var(args_norm_sum) = 0;
                var(args_norm_sum) += diff(args_reduction) * diff(args_reduction);
                stddev(args_norm_sum) =
                    sqrt(var(args_norm_sum) / size +
                         (epsilon.defined() ? cast(processing_type, epsilon) : zero));
                Expr body = diff(args) / stddev(args_norm_sum);
                std::vector<Var> gamma_args(args.begin(),
                                            args.begin() + gamma.args().size());
                if (gamma.defined()) {
                    body = body * gamma(gamma_args);
                }
                if (beta.defined()) {
                    std::vector<Var> beta_args(args.begin(),
                                               args.begin() + beta.args().size());
                    body += beta(beta_args);
                }
                result(args) = body;
            }
        }
    }

    void default_schedule(LoopLevel result_loop_level, const Target &t) {
        if (norm_kind != NormalizationKind::None) {
            norm_sum.compute_at(result, Var::outermost())
                .vectorize(args_norm_sum[0], t.natural_vector_size<float>(), TailStrategy::RoundUp)
                .update(0)
                .atomic()
                .vectorize(norm_sum_range, t.natural_vector_size<float>());
            result.compute_at(result_loop_level)
                .vectorize(result.args()[0], t.natural_vector_size(result.type()));
        }
        if (norm_kind == NormalizationKind::Layer) {
            // TODO untested
            var.compute_at(result, Var::outermost())
                .vectorize(args_norm_sum[0], t.natural_vector_size<float>(), TailStrategy::RoundUp)
                .update(0)
                .atomic()
                .vectorize(norm_sum_range, t.natural_vector_size<float>());
            result.compute_at(result_loop_level)
                .vectorize(result.args()[0], t.natural_vector_size(result.type()));
        }
    }
};

}  // namespace hallmark
#endif  // HALIDE_APPS_HALLMARK_NORMALIZATION_H_
