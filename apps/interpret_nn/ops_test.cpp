#include <chrono>
#include <fstream>
#include <iostream>
#include <random>

#include "app_util.h"
#include "buffer_util.h"
#include "halide_benchmark.h"
#include "ops.h"

namespace interpret_nn {
namespace {

using Halide::Runtime::Buffer;

std::chrono::duration<double> bench(std::function<void()> f) {
    auto result = Halide::Tools::benchmark(f);
    return std::chrono::duration<double>(result.wall_time);
}

template<typename T>
struct MinMax {
    T min, max;
};

template<typename T>
MinMax<T> get_activation_min_max(ActivationFunction activation, int zero_point, double scale) {
    double a_min, a_max;
    bool has_min = false, has_max = false;
    if (activation == ActivationFunction::Relu) {
        a_min = 0.0;
        has_min = true;
    } else if (activation == ActivationFunction::Relu6) {
        a_min = 0.0;
        a_max = 6.0;
        has_min = has_max = true;
    } else if (activation == ActivationFunction::ReluN1To1) {
        a_min = -1.0;
        a_max = 1.0;
        has_min = has_max = true;
    }
    T t_min = std::numeric_limits<T>::min();
    T t_max = std::numeric_limits<T>::max();
    if (has_min) {
        t_min = std::max(t_min, (T)(zero_point + std::round(a_min / scale)));
    }
    if (has_max) {
        t_max = std::min(t_max, (T)(zero_point + std::round(a_max / scale)));
    }
    APP_CHECK(t_min <= t_max);

    return MinMax<T>{t_min, t_max};
}

template<typename T>
MinMax<T> get_output_range(ActivationFunction activation, Tensor *output) {
    const int output_offset = output->quantization().zero.at(0);
    const float output_scale = output->quantization().scale.at(0);
    return get_activation_min_max<T>(activation, output_offset, output_scale);
}

// ----------------------

struct ReferenceOp {
    // Union of all interesting fields in all real ops, to simplify this code.
    // Not all are used for each instance here.
    std::vector<std::shared_ptr<Tensor>> inputs;
    std::vector<std::shared_ptr<Tensor>> outputs;
    std::vector<int> stride;
    std::vector<int> dilation;
    std::vector<int> filter_size;
    Padding padding = Padding::Same;
    ActivationFunction activation = ActivationFunction::None;
    int depth_multiplier = 0;

    virtual void execute() = 0;

    ReferenceOp() = default;
    virtual ~ReferenceOp() = default;
};

struct TestCase {
    std::string name;
    std::unique_ptr<ReferenceOp> reference_op;
    std::unique_ptr<Op> actual_op;
};

using TestCaseFactory = std::function<std::unique_ptr<TestCase>()>;

// ----------------------

struct TensorData {
    std::string name;
    TensorType type;
    std::vector<int> shape;
    float scale;
    int zero_point;
};

std::vector<std::shared_ptr<Tensor>> build_tensors(const std::vector<TensorData> &tds) {
    std::vector<std::shared_ptr<Tensor>> v;
    for (const auto &td : tds) {
        std::vector<halide_dimension_t> shape(td.shape.size());
        size_t shape_size = 1;
        for (size_t i = 0; i < shape.size(); i++) {
            shape[i].min = 0;
            shape[i].extent = td.shape.at(shape.size() - i - 1);
            shape[i].stride = shape_size;
            shape_size *= shape[i].extent;
        }
        std::vector<uint8_t> data;
        QuantizationInfo quantization;
        quantization.dimension = 0;  // TODO -- do we use this?
        quantization.scale.push_back(td.scale);
        quantization.zero.push_back(td.zero_point);
        v.push_back(std::make_shared<Tensor>(td.name,
                                             td.type,
                                             std::move(shape),
                                             std::move(data),
                                             std::move(quantization)));
        v.back()->allocate();
    }
    return v;
}

// ----------------------

template<typename T>
struct Add_ReferenceOp : public ReferenceOp {
    Add_ReferenceOp() = default;

    void execute() override {
        const Tensor *in1 = inputs.at(0).get();
        const Tensor *in2 = inputs.at(1).get();
        Tensor *out = outputs.at(0).get();

        APP_CHECK(
            in1->type() == to_tensor_type<T>() &&
            in2->type() == to_tensor_type<T>() &&
            out->type() == to_tensor_type<T>());

        auto in1_buf = in1->data<T>();
        auto in2_buf = in2->data<T>();
        auto out_buf = out->data<T>();

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

        const auto out_range = get_output_range<T>(activation, out);
        out_buf.for_each_element([&](int c, int x, int y, int b) {
            const double in1_val = in1_buf(c, x, y, b);
            const double in2_val = in2_buf(c, x, y, b);
            const double raw_sum = (in1_val - in1_offset) * in1_multiplier + (in2_val - in2_offset) * in2_multiplier;
            double raw_out = raw_sum * out_multiplier + out_offset;
            if (std::is_integral<T>::value) {
                raw_out = std::round(raw_out);
            }
            const double clamped_out = std::min((double)out_range.max, std::max((double)out_range.min, raw_out));
            out_buf(c, x, y, b) = (T)(clamped_out);
        });
    }
};

struct AddOpTestFactory {
    std::vector<std::shared_ptr<Tensor>> tensors = build_tensors({
        {"MobilenetV2/expanded_conv_2/project/add_fold", TensorType::UInt8, {1, 56, 56, 24}, 0.401493, 136},
        {"MobilenetV2/expanded_conv_1/project/add_fold", TensorType::UInt8, {1, 56, 56, 24}, 0.275834, 119},
        {"MobilenetV2/expanded_conv_2/add", TensorType::UInt8, {1, 56, 56, 24}, 0.432169, 133},
    });

    struct AddOpTestTemplate {
        int in1, in2, out;
        ActivationFunction activation;
    };
    std::vector<AddOpTestTemplate> test_templates = {
        // First case is taken from Mobilenet.
        {0, 1, 2, ActivationFunction::None},
        // The rest are just permutations to test the test harness...
        {0, 2, 1, ActivationFunction::None},
        {1, 0, 2, ActivationFunction::None},
        {1, 2, 0, ActivationFunction::None},
        {2, 0, 1, ActivationFunction::None},
        {2, 1, 0, ActivationFunction::None},
    };
    size_t test_index = 0;

    std::unique_ptr<TestCase> operator()() {
        if (test_index >= test_templates.size()) {
            return nullptr;
        }
        const auto &test_template = test_templates[test_index++];

        auto in1 = tensors[test_template.in1];
        auto in2 = tensors[test_template.in2];
        auto out = tensors[test_template.out];

        auto r = make_unique<Add_ReferenceOp<uint8_t>>();
        r->inputs.push_back(in1);
        r->inputs.push_back(in2);
        r->outputs.push_back(out);
        r->activation = test_template.activation;

        auto test = make_unique<TestCase>();
        test->name = "AddOp<uint8>/" + std::to_string(test_index - 1);
        test->actual_op = make_unique<AddOp>(
            in1.get(),
            in2.get(),
            out.get(),
            test_template.activation);
        test->reference_op = std::move(r);

        return test;
    }
};

// ----------------------

template<typename T>
struct AveragePool_ReferenceOp : public ReferenceOp {
    AveragePool_ReferenceOp() = default;

    void execute() override {
        const Tensor *in = inputs.at(0).get();
        Tensor *out = outputs.at(0).get();

        APP_CHECK(
            in->type() == to_tensor_type<T>() &&
            out->type() == to_tensor_type<T>());

        auto in_buf = in->data<T>();
        auto out_buf = out->data<T>();

        // TODO: does this need to handle Padding::Same?
        APP_CHECK(padding == Padding::Valid) << "AveragePoolOp doesn't handle all paddings yet";

        const int pad_width = 0;
        const int pad_height = 0;

        const auto out_range = get_output_range<T>(activation, out);
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

struct AveragePoolOpTestFactory {
    std::vector<std::shared_ptr<Tensor>> tensors = build_tensors({
        {"MobilenetV2/Conv_1/Relu6", TensorType::UInt8, {1, 7, 7, 1280}, 0.023528, 0},
        {"MobilenetV2/Logits/AvgPool", TensorType::UInt8, {1, 1, 1, 1280}, 0.023528, 0},
    });

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

    std::unique_ptr<TestCase> operator()() {
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

        auto test = make_unique<TestCase>();
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

// ----------------------

bool run_test(TestCaseFactory &factory, int seed) {
    auto test = factory();
    if (!test) {
        return false;  // we're done
    }

    std::vector<Buffer<const void>> reference_outputs, actual_outputs;

    const auto fill_with_random = [&test](int seed) {
        for (auto &t : test->reference_op->inputs) {
            seed++;
            auto buf = t->data<void>();
            dynamic_type_dispatch<FillWithRandom>(buf.type(), buf, seed);
        }
        for (auto &t : test->reference_op->outputs) {
            seed++;
            auto buf = t->data<void>();
            dynamic_type_dispatch<FillWithRandom>(buf.type(), buf, seed);
        }
    };

    const auto save_outputs = [&test](std::vector<Buffer<const void>> &outputs) {
        for (auto &t : test->reference_op->outputs) {
            outputs.emplace_back(t->data<const void>().copy());
        }
    };

    // Run the reference op
    {
        fill_with_random(seed);

        // We don't care about benchmarking the reference
        test->reference_op->execute();

        save_outputs(reference_outputs);
    }

    // Run the real op
    {
        fill_with_random(seed);

        // Execute once, to prime the pump
        Box empty_crop;
        test->actual_op->execute(empty_crop);

        // Now benchmark it
        auto halide_time = bench([&]() {
            test->actual_op->execute(empty_crop);
        });

        // ----- Log benchmark times
        std::cout << "Op: " << test->name << " Time: " << std::chrono::duration_cast<std::chrono::microseconds>(halide_time).count() << " us"
                  << "\n";

        save_outputs(actual_outputs);
    }

    // ----- Now compare the outputs
    APP_CHECK(reference_outputs.size() == actual_outputs.size());
    for (size_t i = 0; i < reference_outputs.size(); ++i) {
        const Buffer<const void> &tflite_buf = reference_outputs[i];
        const Buffer<const void> &halide_buf = actual_outputs[i];
        APP_CHECK(tflite_buf.type() == halide_buf.type());
        APP_CHECK(tflite_buf.dimensions() == halide_buf.dimensions());
        for (int d = 0; d < tflite_buf.dimensions(); d++) {
            APP_CHECK(tflite_buf.dim(d).min() == halide_buf.dim(d).min());
            APP_CHECK(tflite_buf.dim(d).extent() == halide_buf.dim(d).extent());
            APP_CHECK(tflite_buf.dim(d).stride() == halide_buf.dim(d).stride());  // TODO: must the strides match?
        }
        dynamic_type_dispatch<CompareBuffers>(tflite_buf.type(), tflite_buf, halide_buf);
    }

    return true;
}

void run_all_tests(TestCaseFactory factory, int seed) {
    while (interpret_nn::run_test(factory, seed)) {
        // nothing
    }
}

}  // namespace
}  // namespace interpret_nn

int main(int argc, char **argv) {
    int seed = time(nullptr);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) {
            seed = atoi(argv[++i]);
            continue;
        }
        std::cerr << "Usage: TODO\n";
        return -1;
    }

    std::cout << "Using random seed: " << seed << "\n";

    interpret_nn::run_all_tests(interpret_nn::AddOpTestFactory(), seed);
    interpret_nn::run_all_tests(interpret_nn::AveragePoolOpTestFactory(), seed);

    std::cout << "Done!\n";
    return 0;
}
