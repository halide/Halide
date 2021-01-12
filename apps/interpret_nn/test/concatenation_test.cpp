#include "op_test_helper.h"

namespace interpret_nn {
namespace {

template<typename T>
struct Concatenation_ReferenceOp : public op_test::ReferenceOp {
    Concatenation_ReferenceOp() = default;

    void execute() override {
        Tensor *out = outputs.at(0).get();
        CHECK(out->type() == to_tensor_type<T>());

        auto out_buf = out->data<T>();
        const int dims = out_buf.dimensions();
        assert(axis >= 0 && axis < dims);

        int pos_out_offset = 0;
        for (size_t i = 0; i < inputs.size(); i++) {
            const Tensor *in = inputs.at(i).get();
            CHECK(in->type() == to_tensor_type<T>());
            auto in_buf = in->data<const T>();
            CHECK(in_buf.dimensions() == dims);
            for (int j = 0; j < dims; j++) {
                if (j != axis) {
                    CHECK(in_buf.dim(j).min() == out_buf.dim(j).min());
                    CHECK(in_buf.dim(j).extent() == out_buf.dim(j).extent());
                }
            }

            std::vector<int> pos_out(dims, 0);
            in_buf.for_each_element([&](const int *pos_in) {
                memcpy(pos_out.data(), pos_in, dims * sizeof(int));
                pos_out[axis] += pos_out_offset;
                out_buf(pos_out.data()) = in_buf(pos_in);
            });
            pos_out_offset += in_buf.dim(axis).extent();
        }
        CHECK(pos_out_offset == out_buf.dim(axis).extent());
    }
};

struct ConcatenationOpTestFactory : public op_test::TestCaseFactory {
    ConcatenationOpTestFactory() {
        init_tensors({
            {"input1", TensorType::UInt8, {128, 16, 16, 1}, 1.0, 0},
            {"input2", TensorType::UInt8, {128, 16, 16, 1}, 1.0, 0},
            {"output", TensorType::UInt8, {256, 16, 16, 1}, 1.0, 0},
        });
    }

    struct ConcatenationOpTestTemplate {
        std::vector<int> in;
        int out;
        int axis;
        ActivationFunction activation;
    };
    std::vector<ConcatenationOpTestTemplate> test_templates = {
        {{0, 1}, 2, /*axis*/ 0, ActivationFunction::None},
    };
    size_t test_index = 0;

    std::unique_ptr<op_test::TestCase> get_next_test() override {
        if (test_index >= test_templates.size()) {
            return nullptr;
        }
        const auto &test_template = test_templates[test_index++];

        std::vector<std::shared_ptr<Tensor>> inputs_reference;
        std::vector<Tensor *> inputs_actual;
        for (int i : test_template.in) {
            inputs_reference.push_back(tensors.at(i));
            inputs_actual.push_back(tensors.at(i).get());
        }
        auto out = tensors[test_template.out];

        auto r = make_unique<Concatenation_ReferenceOp<uint8_t>>();
        r->inputs = std::move(inputs_reference);
        r->outputs.push_back(out);
        r->axis = test_template.axis;
        r->activation = test_template.activation;

        auto test = make_unique<op_test::TestCase>();
        test->name = "ConcatenationOp<uint8>/" + std::to_string(test_index - 1);
        test->actual_op = make_unique<ConcatenationOp>(
            std::move(inputs_actual),
            out.get(),
            test_template.axis,
            test_template.activation);
        test->reference_op = std::move(r);
        // This op should always be 100% exact
        test->compare_opts.require_exact();

        return test;
    }
};

}  // namespace
}  // namespace interpret_nn

int main(int argc, char **argv) {
    interpret_nn::ConcatenationOpTestFactory factory;
    return interpret_nn::op_test::op_test_main(argc, argv, factory);
}
