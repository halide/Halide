#ifndef HANNK_LOWER_H_
#define HANNK_LOWER_H_

#include "interpreter/model.h"
#include "interpreter/ops.h"

namespace hannk {

// Implement an LSTM from its constituent parts.
std::unique_ptr<OpGroup> lower_tflite_lstm(TensorPtr data_input, TensorPtr prev_activ_input, TensorPtr weights_input, TensorPtr biases_input, TensorPtr prev_state_input,
                                           TensorPtr activ_output, TensorPtr state_output, TensorPtr concat_temp, TensorPtr activ_temp,
                                           ActivationFunction activation = ActivationFunction::None);

}  // namespace hannk

#endif  // HANNK_LOWER_H_