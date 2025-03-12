#ifndef HALIDE_APPROXIMATION_TABLES_H
#define HALIDE_APPROXIMATION_TABLES_H

#include <vector>

#include "IROperator.h"

namespace Halide {
namespace Internal {

struct Approximation {
    struct Metrics {
        double mse;
        double mae;
        double mulpe;
    } metrics_f16, metrics_f32, metrics_f64;

    std::vector<double> p;       // Polynomial in the numerator
    std::vector<double> q = {};  // Polynomial in the denominator (empty if not a Padé approximant)

    const Metrics &metrics_for(Type type) const {
        if (type == Float(16)) {
            return metrics_f16;
        } else if (type == Float(32)) {
            return metrics_f32;
        } else if (type == Float(64)) {
            return metrics_f64;
        }
        internal_error << "No correct type found.";
        return metrics_f32;
    }
};

const Approximation *best_atan_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_sin_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_cos_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_tan_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_log_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_exp_approximation(Halide::ApproximationPrecision precision, Type type);

}  // namespace Internal
}  // namespace Halide

#endif
