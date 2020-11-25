#include "op_test_helper.h"

namespace interpret_nn {
namespace {

template<typename T>
struct Conv2D_ReferenceOp : public op_test::ReferenceOp {
    Conv2D_ReferenceOp() = default;

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

        const int input_depth = input_buf.dim(0).extent();
        const int input_width = input_buf.dim(1).extent();
        const int input_height = input_buf.dim(2).extent();
        const int filter_width = filter_buf.dim(1).extent();
        const int filter_height = filter_buf.dim(2).extent();
        const int output_width = output_buf.dim(1).extent();
        const int output_height = output_buf.dim(2).extent();

        if (padding == Padding::Same) {
            const int dilated_filter_width = dilation[0] * (filter_width - 1) + 1;
            const int dilated_filter_height = dilation[1] * (filter_height - 1) + 1;

            const int pad_width =
                std::max(0, ((output_width - 1) * stride[0] + dilated_filter_width - input_width) / 2);
            const int pad_height =
                std::max(0, ((output_height - 1) * stride[1] + dilated_filter_height - input_height) / 2);

            input_buf.translate({0, pad_width, pad_height, 0});
        }

        CHECK(out->type() == TensorType::UInt8) << "This reference implementation is only tested for uint8";

        const auto out_range = op_test::get_output_range<T>(activation, out);
        output_buf.for_each_element([&](int output_c, int x, int y, int b) {
            double output_value = bias_buf(output_c);

            for (int filter_y = 0; filter_y < filter_height; filter_y++) {
                for (int filter_x = 0; filter_x < filter_width; filter_x++) {
                    for (int input_c = 0; input_c < input_depth; input_c++) {
                        const int x_offset = x * stride[0] + filter_x * dilation[0];
                        const int y_offset = y * stride[1] + filter_y * dilation[1];
                        const bool is_inside = ((x_offset >= 0) && (x_offset < input_width) && (y_offset >= 0) && (y_offset < input_height));
                        const double input_value = is_inside ? (double)input_buf(input_c, x_offset, y_offset, b) - input_offset : 0;
                        const double filter_value = (double)filter_buf(input_c, filter_x, filter_y, output_c) - filter_offset;
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
            output_buf(output_c, x, y, b) = (T)(clamped_output);
        });
    }
};

struct Conv2DOpTestFactory : public op_test::TestCaseFactory {
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
            107, 97, 113, 101, 87, 113, 116, 112, 118, 142, 158, 133, 148, 169, 134,
            135, 137, 127, 115, 109, 119, 122, 120, 122, 116, 115, 120, 69, 27, 93,
            178, 221, 156, 115, 110, 116, 48, 3, 91, 179, 220, 147, 137, 145, 126,
            113, 115, 115, 146, 154, 137, 112, 100, 117, 130, 41, 154, 67, 118, 162,
            57, 154, 145, 159, 68, 108, 77, 153, 125, 34, 192, 141, 185, 81, 93,
            128, 142, 104, 76, 176, 133, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 129, 102, 135, 127, 103, 137, 121, 120, 125, 126, 106, 134,
            126, 102, 139, 124, 115, 127, 122, 119, 125, 124, 117, 125, 124, 116, 126,
            117, 112, 119, 120, 120, 121, 121, 122, 122, 111, 104, 118, 122, 122, 122,
            121, 121, 122, 115, 113, 119, 124, 125, 123, 122, 121, 121, 128, 113, 143,
            110, 103, 120, 122, 125, 134, 118, 107, 118, 66, 67, 61, 115, 115, 112,
            120, 120, 130, 123, 108, 120, 122, 129, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 124, 125, 135, 153, 126, 115, 108, 120,
            157, 179, 139, 165, 194, 143, 145, 158, 133, 90, 63, 106, 61, 23, 98,
            106, 95, 112, 144, 159, 133, 156, 178, 138, 137, 142, 127, 98, 83, 111,
            96, 73, 109, 108, 102, 116, 121, 123, 121, 122, 126, 122, 121, 121, 122,
            38, 33, 84, 124, 128, 131, 140, 122, 124, 118, 119, 113, 128, 127, 127,
            129, 122, 127, 138, 118, 114, 124, 122, 124, 127, 129, 123, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 125, 125, 120, 125, 137, 124, 122, 132, 122, 123, 124, 122,
            139, 145, 130, 125, 120, 122, 115, 116, 121, 122, 117, 121, 119, 116, 119,
            114, 110, 120, 134, 138, 126, 123, 122, 121, 107, 99, 117, 143, 161, 130,
            118, 117, 120, 111, 107, 120, 138, 144, 128, 117, 115, 119, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 115, 115, 119, 112, 102, 117,
            137, 149, 129, 113, 112, 118, 108, 93, 115, 145, 163, 133, 115, 115, 121,
            116, 110, 120, 135, 140, 128, 127, 105, 88, 117, 119, 94, 122, 146, 97,
            122, 116, 99, 116, 112, 105, 121, 132, 108, 124, 135, 110, 125, 124, 118,
            121, 122, 127, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 135, 114, 115, 133, 122, 111, 131, 129, 119, 131, 117, 112, 129, 125,
            108, 127, 129, 122, 127, 120, 116, 125, 125, 110, 121, 128, 119, 124, 123,
            110, 132, 125, 111, 130, 125, 113, 129, 124, 108, 134, 124, 117, 126, 123,
            114, 128, 124, 117, 126, 123, 125, 120, 122, 92, 66, 103, 177, 198, 138,
            90, 97, 125, 139, 201, 150, 76, 22, 91, 156, 152, 125, 137, 102, 115,
            111, 144, 133, 117, 118, 118, 124, 122, 132, 134, 120, 147, 145, 121, 162,
            111, 124, 108, 118, 121, 118, 130, 125, 134, 103, 134, 89, 110, 134, 100,
            123, 140, 114, 113, 109, 117, 114, 109, 118, 120, 121, 120, 117, 116, 120,
            117, 115, 120, 123, 121, 122, 121, 122, 122, 121, 122, 122, 123, 123, 122,
            128, 132, 125, 130, 135, 126, 129, 133, 127, 108, 94, 116, 126, 130, 122,
            122, 121, 121, 91, 72, 110, 131, 138, 128, 125, 126, 124, 122, 122, 121,
            122, 122, 121, 122, 122, 121, 122, 122, 121, 122, 122, 121, 122, 122, 121,
            122, 122, 121, 122, 122, 121, 122, 122, 121, 106, 91, 111, 189, 238, 165,
            71, 38, 89, 126, 125, 126, 199, 247, 159, 44, 1, 83, 97, 79, 106,
            151, 158, 140, 115, 125, 116, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122,
            122, 122, 122, 120, 123, 123, 118, 123, 125, 126, 118, 123, 103, 132, 131,
            103, 130, 134, 116, 124, 127, 105, 131, 130, 107, 129, 131, 112, 128, 126,
            200, 255, 174, 142, 123, 108, 107, 97, 115, 113, 120, 129, 125, 103, 126,
            108, 118, 131, 102, 115, 114, 123, 133, 118, 107, 99, 114, 132, 122, 117,
            127, 133, 125, 113, 126, 124, 139, 111, 116, 131, 111, 117, 128, 120, 125,
            132, 119, 108, 122, 123, 120, 118, 121, 122};
        constexpr size_t filter_data_size = sizeof(filter_data) / sizeof(filter_data[0]);
        auto buf = t.data<uint8_t>();
        assert(buf.size_in_bytes() == filter_data_size);
        memcpy(buf.data(), filter_data, filter_data_size);
    }

    static void fill_bias_mobilenet(Tensor &t, int seed) {
        // Note: assumes little-endian
        static const uint8_t bias_data[] = {
            68, 33, 0, 0, 145, 42, 0, 0, 161, 236, 255, 255, 27, 238, 255,
            255, 103, 51, 0, 0, 196, 49, 0, 0, 231, 152, 255, 255, 146, 218,
            255, 255, 192, 39, 0, 0, 177, 32, 0, 0, 214, 217, 255, 255, 154,
            251, 255, 255, 32, 253, 255, 255, 57, 236, 255, 255, 75, 42, 0, 0,
            203, 44, 0, 0, 26, 0, 0, 0, 205, 36, 0, 0, 232, 186, 255,
            255, 189, 236, 255, 255, 137, 38, 0, 0, 121, 51, 0, 0, 74, 31,
            0, 0, 229, 251, 255, 255, 189, 44, 0, 0, 40, 45, 0, 0, 113,
            2, 0, 0, 98, 41, 0, 0, 74, 1, 0, 0, 216, 35, 0, 0,
            74, 217, 255, 255, 149, 68, 0, 0};
        constexpr size_t bias_data_size = sizeof(bias_data) / sizeof(bias_data[0]);
        auto buf = t.data<int32_t>();
        assert(buf.size_in_bytes() == bias_data_size);
        memcpy(buf.data(), bias_data, bias_data_size);
    }
    Conv2DOpTestFactory() {
        init_tensors({
            {"input", TensorType::UInt8, {3, 224, 224, 1}, 0.0078125, 128},
            {"filter_mobilenet", TensorType::UInt8, {3, 3, 3, 32}, 0.03396892548, 122, fill_filter_mobilenet},
            {"bias_mobilenet", TensorType::Int32, {32}, 0.0002653822303, 0, fill_bias_mobilenet},
            {"output", TensorType::UInt8, {32, 112, 112, 1}, 0.02352847718, 0},
            {"filter_random", TensorType::UInt8, {3, 3, 3, 32}, 0.03396892548, 122, fill_tensor_with_random},
            {"bias_random", TensorType::Int32, {32}, 0.0002653822303, 0, fill_tensor_with_random_bias},
        });
    }

    struct Conv2DOpTestTemplate {
        int in, filt, bias, out;
        std::vector<int> stride, dilation;
        Padding padding;
        ActivationFunction activation;
    };
    std::vector<Conv2DOpTestTemplate> test_templates = {
        // First case is taken from Mobilenet, with well-defined data for filter and bias
        {0, 1, 2, 3, {2, 2}, {1, 1}, Padding::Same, ActivationFunction::None},

        // Second case is like the first, but with random data for the filter and bias inputs.
        // TODO: find ways to improve random input; many runs are correct but uninteresting
        {0, 4, 5, 3, {2, 2}, {1, 1}, Padding::Same, ActivationFunction::None},
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

        auto r = make_unique<Conv2D_ReferenceOp<uint8_t>>();
        r->inputs.push_back(in);
        r->inputs.push_back(filt);
        r->inputs.push_back(bias);
        r->outputs.push_back(out);
        r->stride = test_template.stride;
        r->dilation = test_template.dilation;
        r->padding = test_template.padding;
        r->activation = test_template.activation;

        auto test = make_unique<op_test::TestCase>();
        test->name = "Conv2DOp<uint8>/" + std::to_string(test_index - 1);

        test->actual_op = make_unique<Conv2DOp>(
            in.get(),
            filt.get(),
            bias.get(),
            out.get(),
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
    interpret_nn::Conv2DOpTestFactory factory;
    return interpret_nn::op_test::op_test_main(argc, argv, factory);
}
