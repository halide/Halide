#include "op_test_helper.h"

namespace interpret_nn {
namespace {

template<typename T>
struct FullyConnected_ReferenceOp : public op_test::ReferenceOp {
    FullyConnected_ReferenceOp() = default;

    void execute() override {
        const Tensor *in = inputs.at(0).get();
        const Tensor *filt = inputs.at(1).get();
        const Tensor *bias = inputs.at(2).get();
        Tensor *out = outputs.at(0).get();

        // TODO: is bias always int32?
        CHECK(
            in->type() == to_tensor_type<T>() &&
            filt->type() == to_tensor_type<T>() &&
            bias->type() == TensorType::Int32 &&
            out->type() == to_tensor_type<T>());

        auto input_buf = in->data<T>();
        auto filter_buf = filt->data<T>();
        auto bias_buf = bias->data<int32_t>();
        auto output_buf = out->data<T>();

        const int input_offset = in->quantization().zero.at(0);
        const int filter_offset = filt->quantization().zero.at(0);
        const int output_offset = out->quantization().zero.at(0);

        const float input_scale = in->quantization().scale.at(0);
        const float filter_scale = filt->quantization().scale.at(0);
        const float bias_scale = bias->quantization().scale.at(0);
        const float output_scale = out->quantization().scale.at(0);

        const double input_product_scale = input_scale * filter_scale;
        assert(std::abs(input_product_scale - bias_scale) <=
               std::min(input_product_scale, (double)bias_scale) * 1e-6);

        const double output_multiplier = input_product_scale / output_scale;

        CHECK(out->type() == TensorType::UInt8) << "This reference implementation is only tested for uint8";

        const int filter_depth = filter_buf.dim(0).extent();

        // TODO: I don't think this is quite right vs. tflite implementation.
        // Recheck carefully.
        const auto out_range = op_test::get_output_range<T>(activation, out);
        output_buf.for_each_element([&](int c, int b) {
            double output_value = bias_buf(c);
            for (int d = 0; d < filter_depth; d++) {
                const double input_value = (double)input_buf(d, b) - (double)input_offset;
                const double filter_value = (double)filter_buf(d, c) - (double)filter_offset;
                output_value += filter_value * input_value;
            }
            output_value *= output_multiplier;
            output_value += output_offset;
            if (std::is_integral<T>::value) {
                output_value = std::round(output_value);
            }
            const double clamped_output = std::min((double)out_range.max, std::max((double)out_range.min, output_value));
            output_buf(c, b) = (T)(clamped_output);
        });
    }
};

struct FullyConnectedOpTestFactory : public op_test::TestCaseFactory {
    static void fill_tensor_with_random_bias(Tensor &t, int seed) {
        // bias is an int32, but using values outside the int16 range tends to
        // overflow and make uninteresting results.
        auto buf = t.data<int32_t>();
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int32_t> dis(-32767, 32767);
        buf.for_each_value([&rng, &dis](int32_t &value) {
            value = dis(rng);
        });
    }

    FullyConnectedOpTestFactory() {
        init_tensors({
            {"input", TensorType::UInt8, {1, 1280}, 0.02352941222, 0},
            {"filter", TensorType::UInt8, {1000, 1280}, 0.001603011042, 0},
            {"bias", TensorType::Int32, {1000}, 0.0000377179058, 0, fill_tensor_with_random_bias},
            {"output", TensorType::UInt8, {1, 1000}, 0.08106886595, 0},
        });
    }

    struct FullyConnectedOpTestTemplate {
        int in, filt, bias, out;
        ActivationFunction activation;
    };
    std::vector<FullyConnectedOpTestTemplate> test_templates = {
        {0, 1, 2, 3, ActivationFunction::None},

    };
    size_t test_index = 0;

    std::unique_ptr<op_test::TestCase> get_next_test() override {
        if (test_index >= test_templates.size()) {
            return nullptr;
        }
        const auto &test_template = test_templates[test_index++];

        auto in = tensors[test_template.in];
        auto filt = tensors[test_template.filt];
        auto bias = tensors[test_template.bias];
        auto out = tensors[test_template.out];

        auto r = make_unique<FullyConnected_ReferenceOp<uint8_t>>();
        r->inputs.push_back(in);
        r->inputs.push_back(filt);
        r->inputs.push_back(bias);
        r->outputs.push_back(out);
        r->activation = test_template.activation;

        auto test = make_unique<op_test::TestCase>();
        test->name = "FullyConnectedOp<uint8>/" + std::to_string(test_index - 1);

        test->actual_op = make_unique<FullyConnectedOp>(
            in.get(),
            filt.get(),
            bias.get(),
            out.get(),
            test_template.activation);
        test->reference_op = std::move(r);

        return test;
    }
};

}  // namespace
}  // namespace interpret_nn

int main(int argc, char **argv) {
    std::cerr << "(fully_connected_test is not yet complete; skipping)\n";
    return 0;
    // interpret_nn::FullyConnectedOpTestFactory factory;
    // return interpret_nn::op_test::op_test_main(argc, argv, factory);
}
