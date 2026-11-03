#pragma once

// The port tags (Halide::ApproximationTags) of the schemes' approximations:
// a quantizer's codes, and the scale and offset (a min, or a zero point) of
// its blocks. Schemes tag their ports with them, and schedules find the
// decode of each component by them.
#include <string>

namespace ggml::tag {

inline const std::string codes = "codes", scale = "scale", offset = "offset";

}  // namespace ggml::tag
