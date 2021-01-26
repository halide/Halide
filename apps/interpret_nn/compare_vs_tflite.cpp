#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <random>
#include <unistd.h>

#include "halide_benchmark.h"
#include "interpreter/interpreter.h"
#include "tflite/tflite_parser.h"
#include "util/buffer_util.h"
#include "util/error_util.h"
#include "util/file_util.h"

// IMPORTANT: use only the TFLite C API here.
#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/c/common.h"

namespace interpret_nn {

using Halide::Runtime::Buffer;

namespace {

std::chrono::duration<double> bench(std::function<void()> f) {
    auto result = Halide::Tools::benchmark(f);
    return std::chrono::duration<double>(result.wall_time);
}

halide_type_t tf_lite_type_to_halide_type(TfLiteType t) {
    switch (t) {
    case kTfLiteBool:
        return halide_type_t(halide_type_uint, 1);
    case kTfLiteFloat16:
        return halide_type_t(halide_type_float, 16);
    case kTfLiteFloat32:
        return halide_type_t(halide_type_float, 32);
    case kTfLiteFloat64:
        return halide_type_t(halide_type_float, 64);
    case kTfLiteInt16:
        return halide_type_t(halide_type_int, 16);
    case kTfLiteInt32:
        return halide_type_t(halide_type_int, 32);
    case kTfLiteInt64:
        return halide_type_t(halide_type_int, 64);
    case kTfLiteInt8:
        return halide_type_t(halide_type_int, 8);
    case kTfLiteUInt8:
        return halide_type_t(halide_type_uint, 8);

    case kTfLiteString:
    case kTfLiteNoType:
    case kTfLiteComplex64:
    case kTfLiteComplex128:
    default:
        CHECK(0) << "Unsupported TfLiteType: " << TfLiteTypeGetName(t);
        return halide_type_t();
    }
}

Buffer<void> wrap_tf_lite_tensor_with_halide_buffer(const TfLiteTensor *t) {
    // Wrap a Halide buffer around it.
    std::vector<halide_dimension_t> shape(t->dims->size);
    size_t shape_size = 1;
    for (int i = 0; i < (int)shape.size(); i++) {
        shape[i].min = 0;
        shape[i].extent = t->dims->data[shape.size() - 1 - i];
        shape[i].stride = shape_size;
        shape_size *= shape[i].extent;
    }
    void *buffer_data = t->data.data;

    halide_type_t type = tf_lite_type_to_halide_type(t->type);
    Buffer<void> b(type, buffer_data, shape.size(), shape.data());
    assert(b.size_in_bytes() == t->bytes);
    return b;
}

struct DelegateFactory {
    TfLiteDelegate *(*create_delegate)(char **options_keys,
                                       char **options_values,
                                       size_t num_options,
                                       void (*report_error)(const char *));
    void (*destroy_delegate)(TfLiteDelegate *delegate);
};

}  // namespace

void run_all(const std::string &filename, int seed, int threads, bool verbose, DelegateFactory *delegate_factory) {
    std::cout << "Comparing " << filename << "\n";

    std::vector<char> buffer = read_entire_file(filename);

    struct RunResult {
        std::vector<Buffer<const void>> outputs;
        std::chrono::duration<double> time;
    };

    std::map<std::string, int> seeds;

    RunResult halide_result;

    // ----- Run in Halide
    {
        Model model = parse_tflite_model_from_buffer(buffer.data());
        if (verbose) {
            model.dump(std::cout);
        }

        ModelInterpreter interpreter(std::move(model));

        // Fill in the inputs with random data (but with the same seeds as above).
        for (Tensor *t : interpreter.inputs()) {
            if (t->is_constant()) {
                // Skip constant buffers, just like TFlite above.
                continue;
            }
            int seed_here = seed++;
            seeds[t->name()] = seed_here;
            auto input_buf = t->data<void>();
            dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf, seed_here);
            if (verbose) {
                std::cout << "HALIDE input " << t->name() << " inited with seed = " << seed_here << " type " << input_buf.type() << "\n";
            }
        }

        // No: we won't be parallelizing withing Halide code, that will be done within
        // our interpreter. Leaving this here as an example of what *not* to do.
        // halide_set_num_threads(threads);

        // Execute once, to prime the pump
        interpreter.execute();

        // Now benchmark it
        halide_result.time = bench([&interpreter]() {
            interpreter.execute();
        });

        // Save the outputs
        for (Tensor *t : interpreter.outputs()) {
            if (verbose) {
                std::cout << "HALIDE output is " << t->name() << " type " << to_string(t->type()) << "\n";
            }
            // Make a copy since the Buffer might reference memory owned by the interpreter
            halide_result.outputs.emplace_back(t->data<const void>().copy());
        }
    }

    // ----- Run in TFLite
    const auto run_in_tflite = [&seeds](const std::vector<char> &buffer, int seed, int threads, bool verbose, TfLiteDelegate *delegate) -> RunResult {
        RunResult result;

        TfLiteModel *tf_model = TfLiteModelCreate(buffer.data(), buffer.size());
        CHECK(tf_model);

        TfLiteInterpreterOptions *tf_options = TfLiteInterpreterOptionsCreate();
        CHECK(tf_options);
        TfLiteInterpreterOptionsSetNumThreads(tf_options, threads);
        if (delegate) {
            TfLiteInterpreterOptionsAddDelegate(tf_options, delegate);
        }

        TfLiteInterpreter *tf_interpreter = TfLiteInterpreterCreate(tf_model, tf_options);

        // The options/model can be deleted immediately after interpreter creation.
        TfLiteInterpreterOptionsDelete(tf_options);
        TfLiteModelDelete(tf_model);

        TfLiteStatus status;
        CHECK((status = TfLiteInterpreterAllocateTensors(tf_interpreter)) == kTfLiteOk) << status;

        const int inputs = TfLiteInterpreterGetInputTensorCount(tf_interpreter);
        const int outputs = TfLiteInterpreterGetOutputTensorCount(tf_interpreter);

        // Fill in the inputs with random data, remembering the seeds so we can do the
        // same for the Halide inputs.
        for (int i = 0; i < inputs; i++) {
            TfLiteTensor *t = TfLiteInterpreterGetInputTensor(tf_interpreter, i);
            if (t->allocation_type == kTfLiteMmapRo) {
                // The Tensor references data from the flatbuffer and is read-only;
                // presumably it is data we want to keep as-is
                if (verbose) {
                    std::cout << "TFLITE input " << t->name << " is being used as-is\n";
                }
                continue;
            }
            auto seed_i = seeds.find(t->name);
            assert(seed_i != seeds.end());
            int seed_here = seed_i->second;
            auto input_buf = wrap_tf_lite_tensor_with_halide_buffer(t);
            dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf, seed_here);
            if (verbose) {
                std::cout << "TFLITE input " << t->name << " inited with seed = " << seed_here
                          << " type " << input_buf.type() << " from " << TfLiteTypeGetName(t->type) << "\n";
            }
        }

        // Execute once, to prime the pump
        CHECK((status = TfLiteInterpreterInvoke(tf_interpreter)) == kTfLiteOk) << status;

        // TODO: left in for now for when we re-add DynamicTensor usage
        // int input_dims[4] = { 2, 28, 28, 32};
        // CHECK((status = TfLiteInterpreterResizeInputTensor(tf_interpreter, 0, input_dims, 4)) == kTfLiteOk) << status;
        // CHECK((status = TfLiteInterpreterResizeInputTensor(tf_interpreter, 1, input_dims, 4)) == kTfLiteOk) << status;
        // CHECK((status = TfLiteInterpreterAllocateTensors(tf_interpreter)) == kTfLiteOk) << status;
        // CHECK((status = TfLiteInterpreterInvoke(tf_interpreter)) == kTfLiteOk) << status;
        // CHECK((status = TfLiteInterpreterInvoke(tf_interpreter)) == kTfLiteOk) << status;

        // Now benchmark it
        result.time = bench([&tf_interpreter]() {
            TfLiteStatus status;
            CHECK((status = TfLiteInterpreterInvoke(tf_interpreter)) == kTfLiteOk) << status;
        });

        // Save the outputs
        for (int i = 0; i < outputs; i++) {
            const TfLiteTensor *t = TfLiteInterpreterGetOutputTensor(tf_interpreter, i);
            if (verbose) {
                std::cout << "TFLITE output is " << t->name << " type " << TfLiteTypeGetName(t->type) << "\n";
            }
            // Make a copy since the Buffer might reference memory owned by the tf_interpreter
            result.outputs.emplace_back(wrap_tf_lite_tensor_with_halide_buffer(t).copy());
        }

        TfLiteInterpreterDelete(tf_interpreter);

        return result;
    };

    RunResult tflite_result = run_in_tflite(buffer, seed, threads, verbose, nullptr);

    RunResult delegate_result;
    if (delegate_factory) {
        constexpr size_t num_options = 1;
        std::pair<std::string, std::string> options_strs[num_options] = {
            {"verbosity", std::to_string(verbose ? 1 : 0)},
        };
        char *keys[num_options];
        char *values[num_options];
        for (size_t i = 0; i < num_options; i++) {
            keys[i] = const_cast<char *>(options_strs[i].first.c_str());
            values[i] = const_cast<char *>(options_strs[i].second.c_str());
        }
        TfLiteDelegate *delegate = delegate_factory->create_delegate(keys, values, num_options, nullptr);
        assert(delegate);
        delegate_result = run_in_tflite(buffer, seed, threads, verbose, delegate);
        delegate_factory->destroy_delegate(delegate);
    }

    // ----- Log benchmark times
    std::cout << "TFLITE-DIRECT   Time: " << std::chrono::duration_cast<std::chrono::microseconds>(tflite_result.time).count() << " us"
              << "\n";
    std::cout << "HALIDE-DIRECT   Time: " << std::chrono::duration_cast<std::chrono::microseconds>(halide_result.time).count() << " us"
              << "\n";
    if (delegate_factory) {
        std::cout << "HALIDE-DELEGATE Time: " << std::chrono::duration_cast<std::chrono::microseconds>(delegate_result.time).count() << " us"
                  << "\n";
    }

    double ratio = (halide_result.time / tflite_result.time);
    std::cout << "HALIDE = " << ratio * 100.0 << "% of TFLITE";
    if (ratio > 1.0) {
        std::cout << "  *** HALIDE IS SLOWER";
    }
    std::cout << "\n";

    if (delegate_factory) {
        ratio = (delegate_result.time / tflite_result.time);
        std::cout << "DELEGATE = " << ratio * 100.0 << "% of TFLITE";
        if (ratio > 1.0) {
            std::cout << "  *** DELEGATE IS SLOWER";
        }
        std::cout << "\n";
    }

    // ----- Now compare the outputs
    const auto compare_results = [](const RunResult &a, const RunResult &b, bool verbose) {
        CHECK(a.outputs.size() == b.outputs.size());
        for (size_t i = 0; i < a.outputs.size(); ++i) {
            const Buffer<const void> &tflite_buf = a.outputs[i];
            const Buffer<const void> &halide_buf = b.outputs[i];
            CHECK(tflite_buf.type() == halide_buf.type()) << "Expected type " << tflite_buf.type() << "; saw type " << halide_buf.type();
            CHECK(tflite_buf.dimensions() == halide_buf.dimensions());
            for (int d = 0; d < tflite_buf.dimensions(); d++) {
                CHECK(tflite_buf.dim(d).min() == halide_buf.dim(d).min());
                CHECK(tflite_buf.dim(d).extent() == halide_buf.dim(d).extent());
                CHECK(tflite_buf.dim(d).stride() == halide_buf.dim(d).stride());  // TODO: must the strides match?
            }
            CompareBuffersOptions options;
#if defined(__arm__) || defined(__aarch64__)
            // TFLite on Arm devices generally uses the rounding-shift instructions,
            // which should match our results exactly (since we mimic the same result,
            // whether or not we actually generate those specific instructions).
            // So leave the options at their default.
#else
            // TFLite on x86 (on desktop platforms, at least) appears to mostly
            // use the reference implementations, which don't have the same
            // rounding-shift behavior. We'll bump up the 'close' value for these.
            // This is a lttle hand-wavy but is a decent proxy for now.
            options.close_thresh = 3.0;
#endif
            CompareBuffersResult r = dynamic_type_dispatch<CompareBuffers>(tflite_buf.type(), tflite_buf, halide_buf, options);
            if (r.ok) {
                if (verbose) {
                    std::cout << "MATCHING output " << i << " is:\n";
                    dynamic_type_dispatch<DumpBuffer>(halide_buf.type(), halide_buf);
                }
            }
        }
    };
    compare_results(tflite_result, halide_result, verbose);
    if (delegate_factory) {
        compare_results(tflite_result, delegate_result, verbose);
    }
}

}  // namespace interpret_nn

int main(int argc, char **argv) {
    int seed = time(nullptr);
    int threads = 1;
    bool use_delegate = true;
    bool verbose = false;
    std::vector<const char *> files;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) {
            seed = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--use_delegate")) {
            use_delegate = atoi(argv[++i]) != 0;
            continue;
        }
        if (!strcmp(argv[i], "--threads")) {
            threads = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--verbose")) {
            verbose = true;
            continue;
        }
        files.push_back(argv[i]);
    }
    if (threads <= 0) {
#ifdef _WIN32
        char *num_cores = getenv("NUMBER_OF_PROCESSORS");
        threads = num_cores ? atoi(num_cores) : 8;
#else
        threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    }

    std::cout << "Using random seed: " << seed << "\n";
    std::cout << "Using threads: " << threads << "\n";

    void *delegate_lib = nullptr;
    interpret_nn::DelegateFactory delegate_factory;
    if (use_delegate) {
        delegate_lib = dlopen("libHalideDelegate.so", RTLD_NOW | RTLD_LOCAL);
        if (!delegate_lib) {
            std::cerr << "Unable to open Halide Delegate library: " << dlerror() << "\n";
            return 1;
        }
        assert(delegate_lib);
        delegate_factory.create_delegate = (decltype(delegate_factory.create_delegate))dlsym(delegate_lib, "tflite_plugin_create_delegate");
        assert(delegate_factory.create_delegate);
        delegate_factory.destroy_delegate = (decltype(delegate_factory.destroy_delegate))dlsym(delegate_lib, "tflite_plugin_destroy_delegate");
        assert(delegate_factory.destroy_delegate);
    }

    for (auto f : files) {
        interpret_nn::run_all(f, seed, threads, verbose, use_delegate ? &delegate_factory : nullptr);
        std::cout << "\n";
    }

    std::cout << "Done!\n";
    return 0;
}
