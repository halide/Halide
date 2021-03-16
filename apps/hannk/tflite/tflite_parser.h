#ifndef TFLITE_PARSER_H_
#define TFLITE_PARSER_H_

#include <memory>

#include "interpreter/model.h"

namespace tflite {

struct Model;

}  // namespace tflite

namespace interpret_nn {

// Translate from a tflite::Model to our own model representation.
Model parse_tflite_model(const tflite::Model *model);

// Call tflite::GetModel() and then call parse_tflite_model() on the result --
// avoids the need for client to include any tflite-specific files.
Model parse_tflite_model_from_buffer(const void *model);

}  // namespace interpret_nn

#endif  // TFLITE_PARSER_H_
