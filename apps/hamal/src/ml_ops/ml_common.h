#ifndef HALIDE_APPS_HAMAL_ML_COMMON_H_
#define HALIDE_APPS_HAMAL_ML_COMMON_H_

#include "Halide.h"

namespace hamal {

// Allow easy choice between halide_exp and fast_exp.
Halide::Expr default_exp(Halide::Expr x) {
    return Halide::exp(x);
}

// Allow easy choice between halide_log and fast_log.
Halide::Expr default_log(Halide::Expr x) {
    return Halide::log(x);
}

enum class Activation {
    None,
    GELU,
    SILU,
    RELU,
};

inline const std::map<std::string, Activation> activation_names = {
    {"none", Activation::None},
    {"gelu", Activation::GELU},
    {"silu", Activation::SILU},
    {"relu", Activation::RELU},
};

}  // namespace hamal
#endif  // HALIDE_APPS_HAMAL_ML_COMMON_H_
