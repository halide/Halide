#pragma once

// The format registry. A format is "<type>[.<codes>][.<layout>]": a GGML type
// name and its scheme, optionally with another code encoding ("u4": GGML's
// offset-binary nibbles, the default; "i4": two's complement, as GGML's
// repacked q4_0) and layout ("aos": GGML's block structs, the default;
// "<rows>x<chunk>": Interleave; "soa": one array per field), chosen
// independently. `block`: values per
// record along a row; `rows`: rows per record (f32: no scheme, one value);
// `chunk`: Interleave's bytes per row piece (0 for aos). A kernel may ask for
// `scaled` codes: the same bytes, decoded to codes at 2^(8 - bits) times their
// value (in the top bits of an int8) and scales divided alike (Q4_0Quant).
#include "legacy.h"

namespace ggml {

struct Format {
    Approximation scheme;
    int block = 1, rows = 1, chunk = 0;
};

// `aos`: the same format with one row per record.
inline Format format(const std::string &spec, bool aos = false, bool scaled = false) {
    std::vector<std::string> t;
    for (size_t i = 0, j; i <= spec.size(); i = j + 1) {
        j = std::min(spec.find('.', i), spec.size());
        t.push_back(spec.substr(i, j - i));
    }
    std::string type = t[0], codes, lay = "aos";
    for (size_t i = 1; i < t.size(); i++) {
        (isdigit(t[i][0]) || t[i] == "aos" || t[i] == "soa" ? lay : codes) = t[i];
    }
    if (aos) lay = "aos";
    int rows = 1, chunk = 0;
    sscanf(lay.c_str(), "%dx%d", &rows, &chunk);
    if (type == "q4_0" && (codes.empty() || codes == "u4" || codes == "i4")) return {q4_0(codes == "i4" ? twos(4, scaled * 4) : offset(8, scaled * 4), lay, scaled * 4), QK, rows, chunk};
    if (type == "q8_0" && codes.empty()) return {q8_0(lay), QK, rows, chunk};
    if (type == "f16" && t.size() == 1) return {fp16()};
    if (type == "f32" && t.size() == 1) return {};
    throw std::invalid_argument("unknown format " + spec);
}

}  // namespace ggml
