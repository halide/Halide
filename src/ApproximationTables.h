#ifndef HALIDE_APPROXIMATION_TABLES_H
#define HALIDE_APPROXIMATION_TABLES_H

#include <vector>

#include "IROperator.h"

namespace Halide {
namespace Internal {

struct Approximation {
    ApproximationPrecision::OptimizationObjective objective;
    struct Metrics {
        double mse;
        double mae;
        double mulpe;
    } metrics_f32, metrics_f64;
    std::vector<double> coefficients;
};

const Approximation *best_atan_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_sin_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_cos_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_log_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_exp_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_expm1_approximation(Halide::ApproximationPrecision precision, Type type);

}  // namespace Internal
}  // namespace Halide

#endif
