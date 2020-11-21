#ifndef OP_TEST_HELPER_H
#define OP_TEST_HELPER_H

#include <chrono>
#include <iostream>
#include <random>

#include "buffer_util.h"
#include "error_util.h"
#include "halide_benchmark.h"
#include "ops.h"

namespace interpret_nn {
namespace op_test {

inline std::chrono::duration<double> bench(std::function<void()> f) {
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
    CHECK(t_min <= t_max);

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

inline std::vector<std::shared_ptr<Tensor>> build_tensors(const std::vector<TensorData> &tds) {
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

inline bool run_next_test(TestCaseFactory &factory, int seed) {
    auto test = factory();
    if (!test) {
        return false;  // we're done
    }

    std::vector<Halide::Runtime::Buffer<const void>> reference_outputs, actual_outputs;

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

    const auto save_outputs = [&test](std::vector<Halide::Runtime::Buffer<const void>> &outputs) {
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
    CHECK(reference_outputs.size() == actual_outputs.size());
    for (size_t i = 0; i < reference_outputs.size(); ++i) {
        const Halide::Runtime::Buffer<const void> &tflite_buf = reference_outputs[i];
        const Halide::Runtime::Buffer<const void> &halide_buf = actual_outputs[i];
        CHECK(tflite_buf.type() == halide_buf.type());
        CHECK(tflite_buf.dimensions() == halide_buf.dimensions());
        for (int d = 0; d < tflite_buf.dimensions(); d++) {
            CHECK(tflite_buf.dim(d).min() == halide_buf.dim(d).min());
            CHECK(tflite_buf.dim(d).extent() == halide_buf.dim(d).extent());
            CHECK(tflite_buf.dim(d).stride() == halide_buf.dim(d).stride());  // TODO: must the strides match?
        }
        dynamic_type_dispatch<CompareBuffers>(tflite_buf.type(), tflite_buf, halide_buf);
    }

    return true;
}

inline void run_all_tests(TestCaseFactory factory, int seed) {
    while (run_next_test(factory, seed)) {
        // nothing
    }
}

int op_test_main(int argc, char **argv, TestCaseFactory factory) {
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

    interpret_nn::op_test::run_all_tests(std::move(factory), seed);

    std::cout << "Done!\n";
    return 0;
}

}  // namespace op_test
}  // namespace interpret_nn

#endif  // OP_TEST_HELPER_H
