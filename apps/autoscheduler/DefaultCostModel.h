#ifndef DEFAULT_COST_MODEL_H
#define DEFAULT_COST_MODEL_H

#include <string>

#include "CostModel.h"

namespace Halide {

std::unique_ptr<CostModel> make_default_cost_model(const std::string &weights_in_dir = "",
                                                   const std::string &weights_out_dir = "",
                                                   bool randomize_weights = false);
}  // namespace Halide

#endif  // DEFAULT_COST_MODEL_H
