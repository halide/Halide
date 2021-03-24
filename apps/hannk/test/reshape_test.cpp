#include "op_test_helper.h"

namespace hannk {
namespace {

template<typename T>
struct Reshape_ReferenceOp : public op_test::ReferenceOp {
    Reshape_ReferenceOp() = default;

    void execute() override {
        const Tensor *in = inputs.at(0).get();
        const Tensor *shape = inputs.at(1).get();
        Tensor *out = outputs.at(0).get();

        CHECK(
            in->is_type<T>() &&
            shape->is_type<int32_t>() &&
            out->is_type<T>());

        auto in_buf = in->buffer<const T>();
        auto shape_buf = shape->buffer<const int32_t>();
        auto out_buf = out->buffer<T>();

        CHECK(shape_buf.dimensions() == 1);
        CHECK(shape_buf.dim(0).extent() == out_buf.dimensions());
        for (int d = 0; d < out_buf.dimensions(); d++) {
            CHECK(shape_buf(d) == out_buf.dim(d).extent());
        }

        CHECK(in_buf.number_of_elements() == out_buf.number_of_elements());

        const size_t in_size_bytes = in_buf.size_in_bytes();
        CHECK(in_size_bytes == out_buf.size_in_bytes());

        memcpy(out_buf.data(), in_buf.data(), in_size_bytes);
    }
};

struct ReshapeOpTestFactory : public op_test::TestCaseFactory {
    static void fill_shape(Tensor &t, int seed) {
        auto buf = t.buffer<int32_t>();
        buf(0) = 768;
        buf(1) = 1;
    }

    ReshapeOpTestFactory() {
        init_tensors({
            {"input", halide_type_of<uint8_t>(), {64, 4, 3, 1}, 1.0, 0},
            // shape must be of shape {N}, where N = rank(output)
            {"shape", halide_type_of<int32_t>(), {2}, 1.0, 0, fill_shape},
            {"output", halide_type_of<uint8_t>(), {768, 1}, 1.0, 0},
        });
    }

    struct ReshapeOpTestTemplate {
        int in, shape, out;
    };
    std::vector<ReshapeOpTestTemplate> test_templates = {
        {0, 1, 2},
    };
    size_t test_index = 0;

    std::unique_ptr<op_test::TestCase> get_next_test() override {
        if (test_index >= test_templates.size()) {
            return nullptr;
        }
        const auto &test_template = test_templates[test_index++];

        auto in = tensors[test_template.in];
        auto shape = tensors[test_template.shape];
        auto out = tensors[test_template.out];

        auto r = ::hannk::make_unique<Reshape_ReferenceOp<uint8_t>>();
        r->inputs.push_back(in);
        r->inputs.push_back(shape);
        r->outputs.push_back(out);

        std::vector<int> shape_vals;
        auto shape_buf = shape->buffer<const int32_t>();
        for (int i = 0; i < shape_buf.dim(0).extent(); i++) {
            shape_vals.push_back(shape_buf(i));
        }

        auto test = ::hannk::make_unique<op_test::TestCase>();
        test->name = "ReshapeOp<uint8>/" + std::to_string(test_index - 1);
        test->actual_op = ::hannk::make_unique<ReshapeOp>(
            in.get(),
            out.get(),
            shape_vals);
        test->reference_op = std::move(r);
        // This op should always be 100% exact
        test->compare_opts.require_exact();

        return test;
    }
};

}  // namespace
}  // namespace hannk

int main(int argc, char **argv) {
    hannk::ReshapeOpTestFactory factory;
    return hannk::op_test::op_test_main(argc, argv, factory);
}
