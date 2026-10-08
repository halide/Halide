#pragma once

// The format registry. A format is "<type>[.<codes>][.<layout>]": a GGML type
// name and its scheme, optionally with another code encoding ("u4": GGML's
// offset-binary nibbles, the default; "i4": two's complement, as GGML's
// repacked q4_0) and layout ("aos": GGML's block structs, the default;
// "<rows>x<chunk>": Interleave), chosen independently. `block`: values per
// record along a row; `rows`: rows per record (f32: no scheme, one value).
#include "legacy.h"

namespace ggml {

struct Format {
    Approximation scheme;
    int block = 1, rows = 1;
};

inline Format format(const std::string &spec) {
    std::vector<std::string> t;
    for (size_t i = 0, j; i <= spec.size(); i = j + 1) {
        j = std::min(spec.find('.', i), spec.size());
        t.push_back(spec.substr(i, j - i));
    }
    std::string type = t[0], codes, lay = "aos";
    for (size_t i = 1; i < t.size(); i++) {
        (isdigit(t[i][0]) || t[i] == "aos" ? lay : codes) = t[i];
    }
    int rows = lay == "aos" ? 1 : atoi(lay.c_str());
    if (type == "q4_0" && (codes.empty() || codes == "u4" || codes == "i4")) return {q4_0(codes == "i4" ? twos(4) : offset(8), lay), QK, rows};
    if (type == "q8_0" && codes.empty()) return {q8_0(lay), QK, rows};
    if (type == "f16" && t.size() == 1) return {fp16()};
    if (type == "f32" && t.size() == 1) return {};
    throw std::invalid_argument("unknown format " + spec);
}

}  // namespace ggml
