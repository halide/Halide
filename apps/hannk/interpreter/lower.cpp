#include "interpreter/lower.h"
#include "interpreter/ops.h"

namespace hannk {

// Implement an LSTM from its constituent parts. This is extremely specific
// to TFlite's LSTM op, which according to Benoit Jacob, is deprecated.
// TODO: We could potentially lower this to individual elementwise ops instead,
// and remove the 'LstmElementwiseOp' op.
std::unique_ptr<OpGroup> lower_tflite_lstm(TensorPtr data_input, TensorPtr prev_activ_input, TensorPtr weights_input, TensorPtr biases_input, TensorPtr prev_state_input,
                                           TensorPtr activ_output, TensorPtr state_output, TensorPtr concat_temp, TensorPtr activ_temp) {
    std::vector<TensorPtr> inputs = {data_input, prev_activ_input, weights_input, biases_input, prev_state_input};
    std::vector<TensorPtr> outputs = {activ_output, state_output};

    std::vector<std::unique_ptr<Op>> ops;

    std::vector<TensorPtr> concat_inputs = {data_input, prev_activ_input};
    ops.push_back(::hannk::make_unique<ConcatenationOp>(concat_inputs, concat_temp, 0));
    ops.push_back(::hannk::make_unique<FullyConnectedOp>(concat_temp, weights_input, biases_input, activ_temp));
    ops.push_back(::hannk::make_unique<LstmElementwiseOp>(activ_temp, prev_state_input, state_output, activ_output));

    return ::hannk::make_unique<OpGroup>(std::move(inputs), std::move(outputs), std::move(ops));
}

}  // namespace hannk
