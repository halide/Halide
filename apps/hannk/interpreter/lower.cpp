#include "interpreter/lower.h"
#include "interpreter/elementwise_program.h"
#include "interpreter/ops.h"

namespace hannk {

// Implement an LSTM from its constituent parts. This is extremely specific
// to TFlite's LSTM op, which according to Benoit Jacob, is deprecated.
// TODO: We could potentially lower this to individual elementwise ops instead,
// and remove the 'LstmElementwiseOp' op.
OpPtr lower_tflite_lstm(TensorPtr data_input, TensorPtr prev_activ_input, TensorPtr weights_input, TensorPtr biases_input, TensorPtr prev_state_input,
                        TensorPtr activ_output, TensorPtr state_output, TensorPtr concat_temp, TensorPtr activ_temp,
                        ActivationFunction activation) {
    std::vector<TensorPtr> inputs = {data_input, prev_activ_input, weights_input, biases_input, prev_state_input};
    std::vector<TensorPtr> outputs = {activ_output, state_output};

    std::vector<OpPtr> ops;

    std::vector<TensorPtr> concat_inputs = {data_input, prev_activ_input};
    ops.push_back(make_op<ConcatenationOp>(concat_inputs, concat_temp, 0));
    ops.push_back(lower_tflite_fullyconnected(concat_temp, weights_input, biases_input, activ_temp, activation));

    // Split activ_temp into the 4 ops we need.
    Box elementwise_bounds = activ_temp->bounds();
    elementwise_bounds[0].set_extent(elementwise_bounds[0].extent() / 4);
    // Tensor names don't have to be unique, but basing these on activ_temp's name makes debugging a little easier.
    TensorPtr input_gate_buf =
        std::make_shared<Tensor>(activ_temp->name() + ".input_gate", activ_temp->type(), elementwise_bounds, activ_temp->quantization());
    TensorPtr input_modulation_gate_buf =
        std::make_shared<Tensor>(activ_temp->name() + ".input_modulation_gate", activ_temp->type(), elementwise_bounds, activ_temp->quantization());
    TensorPtr forget_gate_buf =
        std::make_shared<Tensor>(activ_temp->name() + ".forget_gate", activ_temp->type(), elementwise_bounds, activ_temp->quantization());
    TensorPtr output_gate_buf =
        std::make_shared<Tensor>(activ_temp->name() + ".output_gate", activ_temp->type(), elementwise_bounds, activ_temp->quantization());
    std::vector<TensorPtr> split_outputs = {input_gate_buf, input_modulation_gate_buf, forget_gate_buf, output_gate_buf};
    ops.push_back(make_op<SplitOp>(activ_temp, split_outputs, 0));

    // Implements the elementwise compute part of the 'LSTM' TFlite operation.
    // This is extremely specific to TFlite's implementation choices, which are
    // documented here: https://github.com/tensorflow/tensorflow/blob/cbeddb59c4c836637f64b3eb5c639d7db8ca4005/tensorflow/lite/kernels/internal/reference/reference_ops.h#L758-L830
    // According to Benoit Jacob, this approach of specific LSTM ops is deprecated,
    // and most future LSTMs should just arrive as individual elementwise ops.
    std::array<int16_t, 256> program_buffer;
    ElementwiseAssembler p(program_buffer);

    std::vector<TensorPtr> elementwise_inputs = {input_gate_buf, input_modulation_gate_buf, forget_gate_buf, output_gate_buf, prev_state_input};
    std::vector<TensorPtr> elementwise_outputs = {activ_output, state_output};
    auto input_gate = p.input(0);
    auto input_modulation_gate = p.input(1);
    auto forget_gate = p.input(2);
    auto output_gate = p.input(3);
    auto prev_state = p.input(4);

    const int16_t q = 15;
    auto input_gate_output = p.logistic(q, input_gate, q - 3);
    auto input_modulation_gate_output = p.tanh(q, input_modulation_gate, q - 3);
    auto forget_gate_output = p.logistic(q, forget_gate, q - 3);
    auto output_gate_output = p.logistic(q, output_gate, q - 3);

    auto input_times_input_modulation = p.mul_shift(input_gate_output, input_modulation_gate_output, q + 4);
    auto prev_state_times_forget_state = p.mul(forget_gate_output, prev_state);

    auto state = p.add(input_times_input_modulation, prev_state_times_forget_state);
    auto activ = p.mul_add(output_gate_output, p.tanh(7, state, q - 4), 128);
    // Reload new_state so it's in the right place for the outputs.
    // TODO: Make the assembler smart enough to do this itself.
    state = p.add(state, 0);

    auto program_buf = p.assemble({activ, state});
    program_buf = program_buf.copy();

    ops.push_back(make_op<ElementwiseProgramOp>(elementwise_inputs, elementwise_outputs, program_buf));

    return make_op<OpGroup>(std::move(inputs), std::move(outputs), std::move(ops));
}

namespace {

TensorPtr make_shape_tensor(const TensorPtr &t) {
    const auto &b = t->buffer();
    const int dims = b.dimensions();
    HalideBuffer<int32_t> data(dims);
    for (int d = 0; d < dims; d++) {
        data.data()[d] = b.extent(dims - d - 1);
    }
    TensorPtr shape_tensor = std::make_shared<Tensor>(t->name() + ".shape_tensor", std::move(data));
    shape_tensor->set_constant();
    return shape_tensor;
}

}  // namespace

// Implement FullyConnected op using Hannk's Conv op.
OpPtr lower_tflite_fullyconnected(const TensorPtr &input, const TensorPtr &filter, const TensorPtr &bias,
                                  const TensorPtr &output, ActivationFunction activation) {
    const std::array<int, 2> stride = {{1, 1}};
    const std::array<int, 2> dilation_factor = {{1, 1}};

    if (input->rank() == 2) {
        return make_op<ConvOp>(input, filter, bias, output,
                               stride, dilation_factor, Padding::Same, activation);
    } else {
        // Sometimes, fully connected op inputs contain extra dimensions, with the expectation that they
        // are reshaped into a flat buffer.
        Box bounds = input->bounds();
#ifndef NDEBUG
        for (const auto &i : bounds) {
            assert(i.min == 0);
        }
#endif
        // It's important that we preserve the invariants:
        //      input_reshaped[0] == filter[0]
        //      input_reshaped[1] = input->number_of_elements()/input_reshaped[0]
        int c_extent = filter->bounds()[0].extent();
        int num_elems = 1;
        for (size_t i = 0; i < bounds.size(); i++) {
            num_elems *= bounds[i].extent();
        }
        const int b_extent = num_elems / c_extent;
        HCHECK(c_extent * b_extent == num_elems);

        Box reshaped_bounds = {{0, c_extent - 1}, {0, b_extent - 1}};
        TensorPtr input_reshaped =
            std::make_shared<Tensor>(input->name() + ".reshaped", input->type(), std::move(reshaped_bounds), input->quantization());
        OpPtr reshape_input_op = make_op<ReshapeOp>(input, make_shape_tensor(input_reshaped), input_reshaped);

        OpPtr conv_op = make_op<ConvOp>(input_reshaped, filter, bias, output,
                                        stride, dilation_factor, Padding::Same, activation);

        std::vector<TensorPtr> inputs = {input, filter, bias};
        std::vector<TensorPtr> outputs = {output};
        // std::initializer_list doesn't work well with move-only types, alas
        std::vector<OpPtr> ops(2);
        ops[0] = std::move(reshape_input_op);
        ops[1] = std::move(conv_op);
        return make_op<OpGroup>(std::move(inputs), std::move(outputs), std::move(ops));
    }
}

}  // namespace hannk
