#include "op_test_helper.h"

namespace interpret_nn {
namespace {

template<typename T>
struct Pad_ReferenceOp : public op_test::ReferenceOp {
    Pad_ReferenceOp() = default;

    void execute() override {
        const Tensor *in = inputs.at(0).get();
        const Tensor *pad = inputs.at(1).get();
        Tensor *out = outputs.at(0).get();

        CHECK(
            in->type() == to_tensor_type<T>() &&
            pad->type() == TensorType::Int32 &&
            out->type() == to_tensor_type<T>());

        auto in_buf = in->data<T>();
        auto pad_buf = pad->data<int32_t>();
        auto out_buf = out->data<T>();

        const int dims = in_buf.dimensions();
        CHECK(out_buf.dimensions() == dims);

        CHECK(pad_buf.dimensions() == 2);
        CHECK(pad_buf.dim(0).extent() == 2);
        CHECK(pad_buf.dim(1).extent() == dims);

        for (int d = 0; d < in_buf.dimensions(); d++) {
            CHECK(in_buf.dim(d).extent() + pad_buf(0, d) + pad_buf(1, d) == out_buf.dim(d).extent());
        }

        // TODO: is this the correct pad value?
        const int pad_value = in->quantization().zero.at(0);
        out_buf.fill((T)pad_value);

        for (int d = 0; d < dims; d++) {
            in_buf.translate(d, pad_buf(0, d));
        }
        out_buf.copy_from(in_buf);
    }
};

struct PadOpTestFactory : public op_test::TestCaseFactory {
    static void fill_padding(Tensor &t, int seed) {
        auto buf = t.data<int32_t>();
        buf.fill(0);
        buf(0, 0) = 4;   // add 4 values before startof dim(0)
        buf(1, 0) = 12;  // add 12 values after end of dim(0)
    }

    PadOpTestFactory() {
        init_tensors({
            {"input", TensorType::UInt8, {16, 48, 48, 1}, 1.0, 0},
            // padding must be of shape {2, N}, where N = rank(input)
            {"padding", TensorType::Int32, {2, 4}, 1.0, 0, fill_padding},
            {"output", TensorType::UInt8, {32, 48, 48, 1}, 1.0, 0},
        });
    }

    struct PadOpTestTemplate {
        int in, pad, out;
    };
    std::vector<PadOpTestTemplate> test_templates = {
        {0, 1, 2},
    };
    size_t test_index = 0;

    std::unique_ptr<op_test::TestCase> get_next_test() override {
        if (test_index >= test_templates.size()) {
            return nullptr;
        }
        const auto &test_template = test_templates[test_index++];

        auto in = tensors[test_template.in];
        auto pad = tensors[test_template.pad];
        auto out = tensors[test_template.out];

        auto r = make_unique<Pad_ReferenceOp<uint8_t>>();
        r->inputs.push_back(in);
        r->inputs.push_back(pad);
        r->outputs.push_back(out);

        auto test = make_unique<op_test::TestCase>();
        test->name = "PadOp<uint8>/" + std::to_string(test_index - 1);
        test->actual_op = make_unique<PadOp>(
            in.get(),
            pad.get(),
            out.get());
        test->reference_op = std::move(r);
        // This op should always be 100% exact
        test->compare_opts.require_exact();

        return test;
    }
};

}  // namespace
}  // namespace interpret_nn

int main(int argc, char **argv) {
    interpret_nn::PadOpTestFactory factory;
    return interpret_nn::op_test::op_test_main(argc, argv, factory);
}
