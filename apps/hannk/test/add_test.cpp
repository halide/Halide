#include "op_test_helper.h"

namespace hannk {
namespace {

template<typename T>
struct Add_ReferenceOp : public op_test::ReferenceOp {
    Add_ReferenceOp() = default;

    int in2_sign = 1;

    void execute() override {
        const Tensor *in1 = inputs.at(0).get();
        const Tensor *in2 = inputs.at(1).get();
        Tensor *out = outputs.at(0).get();

        CHECK(
            in1->is_type<T>() &&
            in2->is_type<T>() &&
            out->is_type<T>());

        auto in1_buf = in1->buffer<const T>();
        auto in2_buf = in2->buffer<const T>();
        auto out_buf = out->buffer<T>();

        const int in1_offset = in1->quantization().zero.at(0);
        const int in2_offset = in2->quantization().zero.at(0);
        const int out_offset = out->quantization().zero.at(0);

        const float in1_scale = in1->quantization().scale.at(0);
        const float in2_scale = in2->quantization().scale.at(0);
        const float out_scale = out->quantization().scale.at(0);

        const double twice_max_input_scale = 2 * std::max(in1_scale, in2_scale);
        const double in1_multiplier = in1_scale / twice_max_input_scale;
        const double in2_multiplier = in2_scale / twice_max_input_scale;
        const double out_multiplier = twice_max_input_scale / out_scale;

        const auto out_range = op_test::get_output_range<T>(activation, out);
        out_buf.for_each_element([&](int c, int x, int y, int b) {
            const double in1_val = in1_buf(c, x, y, b);
            const double in2_val = in2_buf(c, x, y, b);
            const double raw_sum = (in1_val - in1_offset) * in1_multiplier + in2_sign * (in2_val - in2_offset) * in2_multiplier;
            double raw_out = raw_sum * out_multiplier + out_offset;
            if (std::is_integral<T>::value) {
                raw_out = std::round(raw_out);
            }
            const double clamped_out = std::min((double)out_range.max, std::max((double)out_range.min, raw_out));
            out_buf(c, x, y, b) = (T)(clamped_out);
        });
    }
};

struct AddOpTestFactory : public op_test::TestCaseFactory {
    AddOpTestFactory() {
        init_tensors({
            {"MobilenetV2/expanded_conv_2/project/add_fold", halide_type_of<uint8_t>(), {24, 56, 56, 1}, 0.401493, 136},
            {"MobilenetV2/expanded_conv_1/project/add_fold", halide_type_of<uint8_t>(), {24, 56, 56, 1}, 0.275834, 119},
            {"MobilenetV2/expanded_conv_2/add", halide_type_of<uint8_t>(), {24, 56, 56, 1}, 0.432169, 133},
        });
    }

    struct AddOpTestTemplate {
        int in1, in2, out;
        int in2_sign;
        ActivationFunction activation;
    };
    std::vector<AddOpTestTemplate> test_templates = {
        // First case is taken from Mobilenet.
        {0, 1, 2, 1, ActivationFunction::None},
        // The rest are just permutations to test the test harness...
        {0, 2, 1, 1, ActivationFunction::None},
        {1, 0, 2, 1, ActivationFunction::None},
        {1, 2, 0, 1, ActivationFunction::None},
        {2, 0, 1, 1, ActivationFunction::None},
        {2, 1, 0, 1, ActivationFunction::None},
    };
    size_t test_index = 0;

    std::unique_ptr<op_test::TestCase> get_next_test() override {
        if (test_index >= test_templates.size()) {
            return nullptr;
        }
        const auto &test_template = test_templates[test_index++];

        auto in1 = tensors[test_template.in1];
        auto in2 = tensors[test_template.in2];
        auto out = tensors[test_template.out];

        auto r = ::hannk::make_unique<Add_ReferenceOp<uint8_t>>();
        r->inputs.push_back(in1);
        r->inputs.push_back(in2);
        r->outputs.push_back(out);
        r->in2_sign = test_template.in2_sign;
        r->activation = test_template.activation;

        auto test = ::hannk::make_unique<op_test::TestCase>();
        test->name = "AddOp<uint8>/" + std::to_string(test_index - 1);
        test->actual_op = ::hannk::make_unique<AddOp>(
            in1.get(),
            in2.get(),
            out.get(),
            test_template.in2_sign,
            test_template.activation);
        test->reference_op = std::move(r);

        return test;
    }
};

}  // namespace
}  // namespace hannk

int main(int argc, char **argv) {
    hannk::AddOpTestFactory factory;
    return hannk::op_test::op_test_main(argc, argv, factory);
}
