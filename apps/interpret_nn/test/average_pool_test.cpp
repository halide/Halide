#include "op_test_helper.h"

namespace interpret_nn {
namespace {

template<typename T>
struct AveragePool_ReferenceOp : public op_test::ReferenceOp {
    AveragePool_ReferenceOp() = default;

    void execute() override {
        const Tensor *in = inputs.at(0).get();
        Tensor *out = outputs.at(0).get();

        CHECK(
            in->type() == to_tensor_type<T>() &&
            out->type() == to_tensor_type<T>());

        auto in_buf = in->data<T>();
        auto out_buf = out->data<T>();

        // TODO: does this need to handle Padding::Same?
        CHECK(padding == Padding::Valid) << "AveragePoolOp doesn't handle all paddings yet";

        const int pad_width = 0;
        const int pad_height = 0;

        const auto out_range = op_test::get_output_range<T>(activation, out);
        out_buf.for_each_element([&](int c, int out_x, int out_y, int b) {
            const int in_x_origin = (out_x * stride[0]) - pad_width;
            const int in_y_origin = (out_y * stride[1]) - pad_height;
            const int filter_x_start = std::max(0, -in_x_origin);
            const int filter_x_end = std::min(filter_size[0], in_buf.dim(1).extent() - in_x_origin);
            const int filter_y_start = std::max(0, -in_y_origin);
            const int filter_y_end = std::min(filter_size[1], in_buf.dim(2).extent() - in_y_origin);
            double total = 0.f;
            double filter_count = 0;
            for (int filter_y = filter_y_start; filter_y < filter_y_end; ++filter_y) {
                for (int filter_x = filter_x_start; filter_x < filter_x_end; ++filter_x) {
                    const int in_x = in_x_origin + filter_x;
                    const int in_y = in_y_origin + filter_y;
                    total += in_buf(c, in_x, in_y, b);
                    filter_count++;
                }
            }
            double average = total / filter_count;
            if (std::is_integral<T>::value) {
                average = std::round(average);
            }
            const double clamped_out = std::min((double)out_range.max, std::max((double)out_range.min, average));
            out_buf(c, out_x, out_y, b) = (T)(clamped_out);
        });
    }
};

struct AveragePoolOpTestFactory : public op_test::TestCaseFactory {
    AveragePoolOpTestFactory() {
        init_tensors({
            {"MobilenetV2/Conv_1/Relu6", TensorType::UInt8, {1, 7, 7, 1280}, 0.023528, 0},
            {"MobilenetV2/Logits/AvgPool", TensorType::UInt8, {1, 1, 1, 1280}, 0.023528, 0},
        });
    }

    struct AveragePoolOpTestTemplate {
        int in, out;
        std::vector<int> stride;
        std::vector<int> filter_size;
        Padding padding;
        ActivationFunction activation;
    };
    std::vector<AveragePoolOpTestTemplate> test_templates = {
        // First case is taken from Mobilenet.
        {0, 1, {1, 1}, {7, 7}, Padding::Valid, ActivationFunction::None},
    };
    size_t test_index = 0;

    std::unique_ptr<op_test::TestCase> get_next_test() override {
        if (test_index >= test_templates.size()) {
            return nullptr;
        }
        const auto &test_template = test_templates[test_index++];

        auto in = tensors[test_template.in];
        auto out = tensors[test_template.out];

        auto r = make_unique<AveragePool_ReferenceOp<uint8_t>>();
        r->inputs.push_back(in);
        r->outputs.push_back(out);
        r->stride = test_template.stride;
        r->filter_size = test_template.filter_size;
        r->padding = test_template.padding;
        r->activation = test_template.activation;

        auto test = make_unique<op_test::TestCase>();
        test->name = "AveragePoolOp<uint8>/" + std::to_string(test_index - 1);
        test->actual_op = make_unique<AveragePoolOp>(
            in.get(),
            out.get(),
            test_template.stride,
            test_template.filter_size,
            test_template.padding,
            test_template.activation);
        test->reference_op = std::move(r);

        return test;
    }
};

}  // namespace
}  // namespace interpret_nn

int main(int argc, char **argv) {
    interpret_nn::AveragePoolOpTestFactory factory;
    return interpret_nn::op_test::op_test_main(argc, argv, factory);
}
