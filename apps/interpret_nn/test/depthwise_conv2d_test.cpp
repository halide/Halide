#include "op_test_helper.h"

namespace interpret_nn {
namespace {

template<typename T>
struct DepthwiseConv2D_ReferenceOp : public op_test::ReferenceOp {
    DepthwiseConv2D_ReferenceOp() = default;

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

        const double input_product_scale = (double)input_scale * (double)filter_scale;
        assert(std::abs(input_product_scale - bias_scale) <=
               std::min(input_product_scale, (double)bias_scale) * 1e-6);

        const double output_multiplier = input_product_scale / output_scale;

        const int input_depth = input_buf.dim(0).extent();
        const int input_width = input_buf.dim(1).extent();
        const int input_height = input_buf.dim(2).extent();
        const int filter_width = filter_buf.dim(1).extent();
        const int filter_height = filter_buf.dim(2).extent();
        const int output_width = output_buf.dim(1).extent();
        const int output_height = output_buf.dim(2).extent();

        int pad_width = 0;
        int pad_height = 0;
        if (padding == Padding::Same) {
            const int dilated_filter_width = dilation[0] * (filter_width - 1) + 1;
            const int dilated_filter_height = dilation[1] * (filter_height - 1) + 1;

            pad_width =
                std::max(0, ((output_width - 1) * stride[0] + dilated_filter_width - input_width) / 2);
            pad_height =
                std::max(0, ((output_height - 1) * stride[1] + dilated_filter_height - input_height) / 2);
        }

        CHECK(out->type() == TensorType::UInt8) << "This reference implementation is only tested for uint8";

        const auto out_range = op_test::get_output_range<T>(activation, out);
        output_buf.for_each_element([&](int c, int x, int y, int b) {
            double output_value = bias_buf(c);
            int input_c = c / depth_multiplier;
            assert(input_c < input_depth);
            for (int filter_y = 0; filter_y < filter_height; filter_y++) {
                for (int filter_x = 0; filter_x < filter_width; filter_x++) {
                    const int x_offset = x * stride[0] + filter_x * dilation[0] - pad_width;
                    const int y_offset = y * stride[1] + filter_y * dilation[1] - pad_height;
                    if ((x_offset >= 0) && (x_offset < input_width) && (y_offset >= 0) && (y_offset < input_height)) {
                        const double input_value = (double)input_buf(input_c, x_offset, y_offset, b) - input_offset;
                        const double filter_value = (double)filter_buf(c, filter_x, filter_y, b) - filter_offset;
                        output_value += input_value * filter_value;
                        // TODO: do we need to use std::round here too?
                    }
                }
            }

            output_value *= output_multiplier;
            output_value += output_offset;
            if (std::is_integral<T>::value) {
                output_value = std::round(output_value);
            }
            const double clamped_output = std::min((double)out_range.max, std::max((double)out_range.min, output_value));
            output_buf(c, x, y, b) = (T)(clamped_output);
        });
    }
};

struct DepthwiseConv2DOpTestFactory : public op_test::TestCaseFactory {
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

    static void fill_filter_mobilenet(Tensor &t, int seed) {
        static const uint8_t filter_data[] = {
            165, 165, 162, 42, 156, 165, 164, 137, 166, 165, 165, 166, 124, 133, 165, 165, 123, 165, 160, 197,
            165, 164, 166, 166, 165, 165, 165, 166, 166, 159, 164, 165, 163, 166, 164, 6, 163, 165, 164, 142,
            167, 166, 165, 167, 130, 133, 165, 165, 110, 165, 162, 235, 165, 160, 166, 166, 158, 159, 165, 166,
            167, 173, 164, 166, 165, 165, 167, 8, 165, 165, 164, 148, 166, 165, 164, 168, 124, 108, 165, 165,
            94, 165, 163, 135, 165, 171, 166, 165, 165, 170, 165, 165, 165, 165, 164, 166, 164, 165, 164, 1,
            174, 173, 161, 164, 166, 165, 164, 168, 86, 212, 164, 170, 148, 169, 168, 204, 165, 158, 166, 166,
            165, 165, 165, 164, 168, 155, 164, 166, 170, 161, 165, 15, 171, 165, 176, 198, 160, 163, 165, 169,
            128, 224, 170, 162, 136, 162, 169, 255, 165, 157, 158, 165, 165, 170, 165, 162, 166, 174, 169, 168,
            164, 165, 166, 22, 162, 157, 167, 184, 166, 165, 168, 165, 68, 146, 164, 164, 134, 165, 164, 156,
            165, 168, 166, 165, 165, 160, 165, 165, 164, 169, 165, 166, 165, 165, 167, 49, 170, 165, 164, 156,
            165, 165, 165, 166, 117, 223, 165, 165, 103, 165, 171, 110, 165, 174, 166, 165, 165, 165, 165, 165,
            165, 164, 165, 160, 165, 166, 166, 43, 165, 165, 163, 190, 165, 169, 164, 166, 127, 205, 164, 165,
            106, 165, 166, 136, 165, 168, 166, 165, 172, 165, 164, 166, 164, 165, 165, 165, 165, 165, 165, 43,
            160, 165, 163, 172, 165, 164, 165, 165, 97, 156, 165, 165, 98, 165, 160, 106, 165, 165, 166, 165,
            165, 165, 165, 165, 165, 163, 167, 165};
        auto buf = t.data<uint8_t>();
        assert(buf.size_in_bytes() == sizeof(filter_data));
        memcpy(buf.data(), filter_data, sizeof(filter_data));
    }

    static void fill_bias_mobilenet(Tensor &t, int seed) {
        // Note: assumes little-endian
        static const uint8_t bias_data[] = {
            167, 0, 0, 0, 245, 1, 0, 0, 238, 255, 255, 255, 237, 255, 255, 255, 97, 1, 0, 0, 130, 1, 0, 0, 63,
            0, 0, 0, 228, 255, 255, 255, 173, 1, 0, 0, 85, 0, 0, 0, 23, 0, 0, 0, 211, 255, 255, 255, 202, 255,
            255, 255, 126, 255, 255, 255, 38, 1, 0, 0, 51, 1, 0, 0, 192, 255, 255, 255, 165, 0, 0, 0, 58, 1, 0,
            0, 88, 255, 255, 255, 127, 0, 0, 0, 96, 1, 0, 0, 19, 0, 0, 0, 65, 255, 255, 255, 122, 1, 0, 0, 126,
            1, 0, 0, 1, 0, 0, 0, 167, 1, 0, 0, 190, 255, 255, 255, 254, 0, 0, 0, 175, 255, 255, 255, 73, 253,
            255, 255};
        auto buf = t.data<int32_t>();
        assert(buf.size_in_bytes() == sizeof(bias_data));
        memcpy(buf.data(), bias_data, sizeof(bias_data));
    }
    DepthwiseConv2DOpTestFactory() {
        init_tensors({
            {"input", TensorType::UInt8, {32, 112, 112, 1}, 0.02352847718, 0},
            {"filter_mobilenet", TensorType::UInt8, {32, 3, 3, 1}, 0.3436955214, 165, fill_filter_mobilenet},
            {"bias_mobilenet", TensorType::Int32, {32}, 0.008086632006, 0, fill_bias_mobilenet},
            {"output", TensorType::UInt8, {32, 112, 112, 1}, 0.02352847718, 0},
            {"filter_random", TensorType::UInt8, {32, 3, 3, 1}, 0.3436955214, 165, fill_tensor_with_random},
            {"bias_random", TensorType::Int32, {32}, 0.008086632006, 0, fill_tensor_with_random_bias},
        });
    }

    struct DepthwiseConv2DOpTestTemplate {
        int in, filt, bias, out;
        std::vector<int> stride, dilation;
        int depth_multiplier;
        Padding padding;
        ActivationFunction activation;
    };
    std::vector<DepthwiseConv2DOpTestTemplate> test_templates = {
        // First case is taken from Mobilenet, with well-defined data for filter and bias
        {0, 1, 2, 3, {1, 1}, {1, 1}, 1, Padding::Same, ActivationFunction::None},

        // Second case is like the first, but with random data for the filter and bias inputs.
        // TODO: find ways to improve random input; many runs are correct but uninteresting
        {0, 4, 5, 3, {1, 1}, {1, 1}, 1, Padding::Same, ActivationFunction::None},
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
        assert(filt);
        assert(bias);
        assert(out);
        assert(in);

        auto r = make_unique<DepthwiseConv2D_ReferenceOp<uint8_t>>();
        r->inputs.push_back(in);
        r->inputs.push_back(filt);
        r->inputs.push_back(bias);
        r->outputs.push_back(out);
        r->stride = test_template.stride;
        r->dilation = test_template.dilation;
        r->padding = test_template.padding;
        r->activation = test_template.activation;
        r->depth_multiplier = test_template.depth_multiplier;

        auto test = make_unique<op_test::TestCase>();
        test->name = "DepthwiseConv2DOp<uint8>/" + std::to_string(test_index - 1);

        test->actual_op = make_unique<DepthwiseConv2DOp>(
            in.get(),
            filt.get(),
            bias.get(),
            out.get(),
            test_template.depth_multiplier,
            test_template.stride,
            test_template.dilation,
            test_template.padding,
            test_template.activation);
        test->reference_op = std::move(r);

        return test;
    }
};

}  // namespace
}  // namespace interpret_nn

int main(int argc, char **argv) {
    interpret_nn::DepthwiseConv2DOpTestFactory factory;
    return interpret_nn::op_test::op_test_main(argc, argv, factory);
}
