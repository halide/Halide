#ifndef TFLITE_PARSER_H_
#define TFLITE_PARSER_H_

#include <memory>

#include "interpret_nn.h"

namespace tflite {

struct Model;

}  // namespace tflite

namespace interpret_nn {

Model ParseTfLiteModel(const tflite::Model *model);

}  // namespace interpret_nn

#endif  // TFLITE_PARSER_H_
