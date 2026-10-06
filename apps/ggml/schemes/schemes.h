#pragma once

// GGML type name -> its scheme (undefined for plain float types).
#include "legacy.h"

namespace ggml {

inline Approximation scheme(const std::string &type) {
    if (type == "q4_0") return q4_0();
    if (type == "q8_0") return q8_0();
    return {};
}

}  // namespace ggml
