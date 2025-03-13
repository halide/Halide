#ifndef HALIDE_APPROXIMATION_TABLES_H
#define HALIDE_APPROXIMATION_TABLES_H

#include <vector>

#include "IROperator.h"

namespace Halide {
namespace Internal {

struct Approximation {
    struct Metrics {
        double mse;
        double mae{std::numeric_limits<double>::quiet_NaN()};
        uint64_t mulpe{0};
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

namespace ApproximationTables {
extern const std::vector<Approximation> table_atan;
extern const std::vector<Approximation> table_sin;
extern const std::vector<Approximation> table_cos;
extern const std::vector<Approximation> table_tan;
extern const std::vector<Approximation> table_exp;
extern const std::vector<Approximation> table_log;

const Approximation *best_atan_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_sin_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_cos_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_tan_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_log_approximation(Halide::ApproximationPrecision precision, Type type);
const Approximation *best_exp_approximation(Halide::ApproximationPrecision precision, Type type);
}  // namespace ApproximationTables

}  // namespace Internal
}  // namespace Halide

#endif
