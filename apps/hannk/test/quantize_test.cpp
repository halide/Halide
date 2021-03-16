#include "op_test_helper.h"

namespace interpret_nn {
namespace {

template<typename InT, typename OutT>
struct Quantize_ReferenceOp : public op_test::ReferenceOp {
    Quantize_ReferenceOp() = default;

    void execute() override {
        const Tensor *in = inputs.at(0).get();
        Tensor *out = outputs.at(0).get();

        CHECK(
            in->type() == to_tensor_type<InT>() &&
            out->type() == to_tensor_type<OutT>());

        auto in_buf = in->data<InT>();
        auto out_buf = out->data<OutT>();
        check_shapes_match(in_buf, out_buf);

        const int in_offset = in->quantization().zero.at(0);
        const int out_offset = out->quantization().zero.at(0);

        const float in_scale = in->quantization().scale.at(0);
        const float out_scale = out->quantization().scale.at(0);

        const double out_multiplier = in_scale / out_scale;

        // TODO: are 0..1 the correct min/max values for FP types?
        constexpr double min_val = std::is_integral<OutT>::value ? (double)std::numeric_limits<OutT>::min() : 0.0;
        constexpr double max_val = std::is_integral<OutT>::value ? (double)std::numeric_limits<OutT>::max() : 1.0;

        out_buf.for_each_element([&](const int *pos) {
            const double in_val = in_buf(pos) - in_offset;
            double out_val = in_val * out_multiplier + out_offset;
            if (std::is_integral<OutT>::value) {
                out_val = std::round(out_val);
            }
            const double clamped_out = std::min(max_val, std::max(min_val, out_val));
            out_buf(pos) = (OutT)(clamped_out);
        });
    }
};

struct QuantizeOpTestFactory : public op_test::TestCaseFactory {
    QuantizeOpTestFactory() {
        init_tensors({
            {"input", TensorType::UInt8, {1000, 1}, 0.00390625, 128},
            {"output", TensorType::UInt8, {1000, 1}, 0.00390625, 0},
        });
    }

    struct QuantizeOpTestTemplate {
        int in, out;
    };
    std::vector<QuantizeOpTestTemplate> test_templates = {
        // TODO: this is a very weak test; it just 'requantizes' a uint8 by shifting its
        // offset. Really need a float->uint8 or int16->int8, etc test here, but the
        // Halide QuantizeOp implementation doesn't support anything but uint8->uint8 yet.
        {0, 1},
    };
    size_t test_index = 0;

    std::unique_ptr<op_test::TestCase> get_next_test() override {
        if (test_index >= test_templates.size()) {
            return nullptr;
        }
        const auto &test_template = test_templates[test_index++];

        auto in = tensors[test_template.in];
        auto out = tensors[test_template.out];

        auto r = make_unique<Quantize_ReferenceOp<uint8_t, uint8_t>>();
        r->inputs.push_back(in);
        r->outputs.push_back(out);

        auto test = make_unique<op_test::TestCase>();
        test->name = "QuantizeOp<uint8>/" + std::to_string(test_index - 1);
        test->actual_op = make_unique<QuantizeOp>(
            in.get(),
            out.get());
        test->reference_op = std::move(r);

        return test;
    }
};

}  // namespace
}  // namespace interpret_nn

int main(int argc, char **argv) {
    interpret_nn::QuantizeOpTestFactory factory;
    return interpret_nn::op_test::op_test_main(argc, argv, factory);
}
