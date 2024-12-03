#ifndef HALIDE_APPROXIMATION_TABLES_H
#define HALIDE_APPROXIMATION_TABLES_H

#include <vector>

#include "IROperator.h"

namespace Halide {
namespace Internal {

struct Approximation {
    ApproximationPrecision::OptimizationObjective objective;
    double mse;
    double mae;
    double mulpe;
    std::vector<double> coefficients;
};

const Approximation *best_atan_approximation(Halide::ApproximationPrecision precision);

}  // namespace Internal
}  // namespace Halide

#endif
