#ifndef OP_TEST_HELPER_H
#define OP_TEST_HELPER_H

#include <chrono>
#include <iostream>
#include <random>

#include "halide_benchmark.h"
#include "interpreter/ops.h"
#include "util/buffer_util.h"
#include "util/error_util.h"

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

using TensorInitFn = std::function<void(Tensor &, int seed)>;

struct ReferenceOp {
    // Union of all interesting fields in all real ops, to simplify this code.
    // Not all are used for each instance here.
    std::vector<std::shared_ptr<Tensor>> inputs;
    std::vector<TensorInitFn> input_init_fns;
    std::vector<std::shared_ptr<Tensor>> outputs;
    std::vector<int> stride;
    std::vector<int> dilation;
    std::vector<int> filter_size;
    Padding padding = Padding::Same;
    ActivationFunction activation = ActivationFunction::None;
    int depth_multiplier = 0;
    int axis = 0;

    virtual void execute() = 0;

    ReferenceOp() = default;
    virtual ~ReferenceOp() = default;
};

// ----------------------

struct TestCase {
    std::string name;
    std::unique_ptr<ReferenceOp> reference_op;
    std::unique_ptr<Op> actual_op;
    std::function<void()> reset_tensors_fn;
    CompareBuffersOptions compare_opts;
};

struct TestCaseFactory {
protected:
    std::vector<std::shared_ptr<Tensor>> tensors;
    std::vector<TensorInitFn> tensor_init_fns;
    int num_failures = 0;

    static void fill_tensor_with_random(Tensor &t, int seed) {
        auto buf = t.data<void>();
        dynamic_type_dispatch<FillWithRandom>(buf.type(), buf, seed);
    }

    struct TensorData {
        std::string name;
        TensorType type;
        std::vector<int> shape;
        float scale;
        int zero_point;
        TensorInitFn init_fn;
    };

    void init_tensors(const std::vector<TensorData> &tds) {
        tensors.clear();
        tensor_init_fns.clear();
        for (const auto &td : tds) {
            std::vector<halide_dimension_t> shape(td.shape.size());
            size_t shape_size = 1;
            for (size_t i = 0; i < shape.size(); i++) {
                shape[i].min = 0;
                shape[i].extent = td.shape.at(i);
                shape[i].stride = shape_size;
                shape_size *= shape[i].extent;
            }
            std::vector<uint8_t> data;
            QuantizationInfo quantization;
            quantization.dimension = 0;  // TODO -- do we use this?
            quantization.scale.push_back(td.scale);
            quantization.zero.push_back(td.zero_point);
            tensors.push_back(std::make_shared<Tensor>(td.name,
                                                       td.type,
                                                       std::move(shape),
                                                       std::move(data),
                                                       std::move(quantization)));
            tensors.back()->allocate();

            if (td.init_fn == nullptr) {
                tensor_init_fns.push_back(fill_tensor_with_random);
            } else {
                tensor_init_fns.push_back(td.init_fn);
            }
        }
    }

    bool run_next_test(int seed, bool verbose = false) {
        const auto reset_tensors = [this](int seed) {
            for (size_t i = 0; i < this->tensors.size(); i++) {
                seed++;
                Tensor *t = this->tensors.at(i).get();
                TensorInitFn f = this->tensor_init_fns.at(i);
                assert(t != nullptr);
                assert(f != nullptr);
                f(*t, seed);
            }
        };

        // Call reset_tensors() before get_next_test(), since some ops
        // (e.g. ReshapeOp) rely on the contents of a Tensor to fill in the op.
        reset_tensors(seed);

        std::unique_ptr<TestCase> test = get_next_test();
        if (!test) {
            return false;  // we're done
        }

        std::vector<Halide::Runtime::Buffer<const void>> reference_outputs, actual_outputs;

        const auto save_outputs = [&test](std::vector<Halide::Runtime::Buffer<const void>> &outputs) {
            for (auto &t : test->reference_op->outputs) {
                outputs.emplace_back(t->data<const void>().copy());
            }
        };

        // Run the reference op
        {
            reset_tensors(seed);

            // We don't care about benchmarking the reference
            test->reference_op->execute();

            save_outputs(reference_outputs);
        }

        // Run the real op
        {
            reset_tensors(seed);

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
            CompareBuffersResult r = dynamic_type_dispatch<CompareBuffers>(tflite_buf.type(), tflite_buf, halide_buf, test->compare_opts);
            if (r.ok) {
                if (verbose) {
                    std::cout << "MATCHING output " << i << " is:\n";
                    dynamic_type_dispatch<DumpBuffer>(halide_buf.type(), halide_buf);
                }
            } else {
                num_failures++;
            }
        }

        return true;  // keep going
    }

public:
    int run_all_tests(int seed, bool verbose = false) {
        while (run_next_test(seed, verbose)) {
            // nothing
        }
        if (verbose && num_failures > 0) {
            std::cerr << "num_failures is: " << num_failures << "\n";
        }
        return num_failures;
    }

    virtual std::unique_ptr<op_test::TestCase> get_next_test() = 0;
    virtual ~TestCaseFactory() = default;
};

int op_test_main(int argc, char **argv, TestCaseFactory &factory) {
    int seed = time(nullptr);
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) {
            seed = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--verbose")) {
            verbose = true;
            continue;
        }
        std::cerr << "Usage: TODO\n";
        return -1;
    }

    std::cout << "Using random seed: " << seed << "\n";

    return factory.run_all_tests(seed, verbose);
}

}  // namespace op_test
}  // namespace interpret_nn

#endif  // OP_TEST_HELPER_H
