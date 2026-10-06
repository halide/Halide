#pragma once

// The format registry: GGML type name -> its scheme and the number of values
// per encoded record (f32: no scheme, one value per element).
#include "legacy.h"

namespace ggml {

struct Format {
    Approximation scheme;
    int block = 1;
};

inline Format format(const std::string &type) {
    if (type == "q4_0") return {q4_0(), QK};
    if (type == "q8_0") return {q8_0(), QK};
    if (type == "f16") return {fp16()};
    return {};
}

}  // namespace ggml
