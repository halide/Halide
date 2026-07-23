#ifndef HALIDE_APPS_HALLMARK_WEIGHTS_LOADER_H_
#define HALIDE_APPS_HALLMARK_WEIGHTS_LOADER_H_

#include "HalideBuffer.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "contrib/llm_params.h"
#include "contrib/llm_weights.h"

namespace hallmark {

absl::StatusOr<LlmWeights> LoadLlmWeights(absl::string_view tflite_path, const LlmParams &params);

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_WEIGHTS_LOADER_H_
