#ifndef TFLITE_PARSER_H_
#define TFLITE_PARSER_H_

#include <memory>

#include "model.h"

namespace tflite {

struct Model;

}  // namespace tflite

namespace interpret_nn {

// Translate from a tflite::Model to our own model representation.
Model ParseTfLiteModel(const tflite::Model *model);

}  // namespace interpret_nn

#endif  // TFLITE_PARSER_H_
