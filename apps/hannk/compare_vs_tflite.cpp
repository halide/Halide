#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <random>
#include <unistd.h>

#include "delegate/hannk_delegate.h"
#include "halide_benchmark.h"
#include "interpreter/interpreter.h"
#include "tflite/tflite_parser.h"
#include "util/buffer_util.h"
#include "util/error_util.h"
#include "util/file_util.h"

// IMPORTANT: use only the TFLite C API here.
#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/c/common.h"

namespace hannk {

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

class DelegatePtr {
    void *delegate_lib_ = nullptr;
    TfLiteDelegate *delegate_ = nullptr;
    TfLiteDelegate *(*create_delegate_)(char **options_keys,
                                        char **options_values,
                                        size_t num_options,
                                        void (*report_error)(const char *)) = nullptr;
    void (*destroy_delegate_)(TfLiteDelegate *delegate) = nullptr;

public:
    DelegatePtr() = default;

    bool init(const std::string &external_delegate_path, int verbosity) {
        CHECK(delegate_lib_ == nullptr);
        // Look for it in the normal library path if no explicit path specified
        std::string path = external_delegate_path.empty() ? "libHannkDelegate.so" : external_delegate_path;
        delegate_lib_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!delegate_lib_) {
            std::cerr << "Unable to dlopen(" << path << "): " << dlerror() << "\n";
            return false;
        }

        create_delegate_ = (decltype(create_delegate_))dlsym(delegate_lib_, "tflite_plugin_create_delegate");
        if (!create_delegate_) {
            std::cerr << "Unable to find tflite_plugin_create_delegate: " << dlerror() << "\n";
            return false;
        }

        destroy_delegate_ = (decltype(destroy_delegate_))dlsym(delegate_lib_, "tflite_plugin_destroy_delegate");
        if (!destroy_delegate_) {
            std::cerr << "Unable to find tflite_plugin_destroy_delegate: " << dlerror() << "\n";
            return false;
        }

        constexpr size_t num_options = 1;
        std::pair<std::string, std::string> options_strs[num_options] = {
            {"verbosity", std::to_string(verbosity)},
        };
        char *keys[num_options];
        char *values[num_options];
        for (size_t i = 0; i < num_options; i++) {
            keys[i] = const_cast<char *>(options_strs[i].first.c_str());
            values[i] = const_cast<char *>(options_strs[i].second.c_str());
        }

        delegate_ = (*create_delegate_)(keys, values, num_options, nullptr);
        if (!delegate_) {
            std::cerr << "tflite_plugin_create_delegate returned nullptr\n";
            return false;
        }
        return true;
    }

    TfLiteDelegate *get() {
        return delegate_;
    }

    ~DelegatePtr() {
        if (destroy_delegate_) {
            (*destroy_delegate_)(delegate_);
        }
    }
};

}  // namespace

enum WhichRun {
    kTfLite,
    kHannk,
    kExternalDelegate,
    kInternalDelegate,

    kNumRuns  // keep last
};

static const char *const RunNames[kNumRuns] = {
    "TfLite",
    "Hannk",
    "HannkExternalDelegate",
    "HannkInternalDelegate",
};

struct Runner {
    int seed = 0;
    int threads = 1;
    int verbosity = 0;
    bool do_run[kNumRuns];  // no way to default-init everything to anything but zero, alas
    bool do_benchmark = true;
    bool do_compare_results = true;
    std::string external_delegate_path;

    Runner() {
        for (int i = 0; i < kNumRuns; i++) {
            do_run[i] = true;
        }
    }

    void run(const std::string &filename);

private:
    std::map<std::string, int> seeds_;

    int seed_for_name(const std::string &name);

    struct RunResult {
        std::vector<Buffer<const void>> outputs;
        std::chrono::duration<double> time{0};
    };
    RunResult run_in_hannk(const std::vector<char> &buffer);
    RunResult run_in_tflite(const std::vector<char> &buffer, TfLiteDelegate *delegate = nullptr);
    void compare_results(const std::string &msg, const RunResult &a, const RunResult &b);
};

int Runner::seed_for_name(const std::string &name) {
    auto it = seeds_.find(name);
    if (it != seeds_.end()) {
        return it->second;
    }
    const int seed_here = seed++;
    seeds_[name] = seed_here;
    return seed_here;
}

Runner::RunResult Runner::run_in_hannk(const std::vector<char> &buffer) {
    RunResult result;

    std::unique_ptr<OpGroup> model = parse_tflite_model_from_buffer(buffer.data());
    if (verbosity) {
        model->dump(std::cout);
    }

    Interpreter interpreter(std::move(model));

    // Fill in the inputs with pseudorandom data (save the seeds for later).
    for (TensorPtr t : interpreter.inputs()) {
        if (t->is_constant()) {
            // Skip constant buffers, just like TFlite does later on.
            continue;
        }
        const int seed_here = seed_for_name(t->name());
        auto input_buf = t->buffer();
        dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf, seed_here);
        if (verbosity) {
            std::cout << "HALIDE input " << t->name() << " inited with seed = " << seed_here << " type " << input_buf.type() << "\n";
        }
    }

    // No: we won't be parallelizing within Halide code, that will be done within
    // our interpreter. Leaving this here as an example of what *not* to do.
    // halide_set_num_threads(threads);

    // Execute once, to prime the pump
    interpreter.execute();

    // Save the outputs from that execution (before benchmarking)
    for (TensorPtr t : interpreter.outputs()) {
        if (verbosity) {
            std::cout << "HALIDE output is " << t->name() << " type " << t->type() << "\n";
        }
        // Make a copy since the Buffer might reference memory owned by the interpreter
        result.outputs.emplace_back(t->buffer().copy());
    }

    // Now benchmark it
    if (do_benchmark) {
        result.time = bench([&interpreter]() {
            interpreter.execute();
        });
    }

    return result;
}

Runner::RunResult Runner::run_in_tflite(const std::vector<char> &buffer, TfLiteDelegate *delegate) {
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
    CHECK(tf_interpreter != nullptr);

    // The options/model can be deleted immediately after interpreter creation.
    TfLiteInterpreterOptionsDelete(tf_options);
    TfLiteModelDelete(tf_model);

    TfLiteStatus status;
    CHECK((status = TfLiteInterpreterAllocateTensors(tf_interpreter)) == kTfLiteOk) << status;

    const int inputs = TfLiteInterpreterGetInputTensorCount(tf_interpreter);
    const int outputs = TfLiteInterpreterGetOutputTensorCount(tf_interpreter);

    // Fill in the inputs with the same pseudorandom data as before.
    for (int i = 0; i < inputs; i++) {
        TfLiteTensor *t = TfLiteInterpreterGetInputTensor(tf_interpreter, i);
        if (t->allocation_type == kTfLiteMmapRo) {
            // The Tensor references data from the flatbuffer and is read-only;
            // presumably it is data we want to keep as-is
            if (verbosity) {
                std::cout << "TFLITE input " << t->name << " is being used as-is\n";
            }
            continue;
        }
        const int seed_here = seed_for_name(t->name);
        auto input_buf = wrap_tf_lite_tensor_with_halide_buffer(t);
        dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf, seed_here);
        if (verbosity) {
            std::cout << "TFLITE input " << t->name << " inited with seed = " << seed_here
                      << " type " << input_buf.type() << " from " << TfLiteTypeGetName(t->type) << "\n";
        }
    }

    // Execute once, to prime the pump
    CHECK((status = TfLiteInterpreterInvoke(tf_interpreter)) == kTfLiteOk) << status;

    // Save the outputs from that execution (before benchmarking)
    for (int i = 0; i < outputs; i++) {
        const TfLiteTensor *t = TfLiteInterpreterGetOutputTensor(tf_interpreter, i);
        if (verbosity) {
            std::cout << "TFLITE output is " << t->name << " type " << TfLiteTypeGetName(t->type) << "\n";
        }
        // Make a copy since the Buffer might reference memory owned by the tf_interpreter
        result.outputs.emplace_back(wrap_tf_lite_tensor_with_halide_buffer(t).copy());
    }

    // Now benchmark it
    if (do_benchmark) {
        result.time = bench([&tf_interpreter]() {
            TfLiteStatus status;
            CHECK((status = TfLiteInterpreterInvoke(tf_interpreter)) == kTfLiteOk) << status;
        });
    }

    TfLiteInterpreterDelete(tf_interpreter);

    return result;
}

void Runner::compare_results(const std::string &msg, const RunResult &a, const RunResult &b) {
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
        const float tolerance = 1.0f / 256.0f;
#else
        // TFLite on x86 (on desktop platforms, at least) appears to mostly
        // use the reference implementations, which don't have the same
        // rounding-shift behavior. We'll bump up the 'close' value for these.
        // This is a lttle hand-wavy but is a decent proxy for now.
        const float tolerance = 1.0f / 100.0f;
#endif
        options.close_thresh = std::ceil((1ull << tflite_buf.type().bits) * tolerance);
        options.max_diffs_to_log = 8;
        std::cout << msg;
        CompareBuffersResult r = dynamic_type_dispatch<CompareBuffers>(tflite_buf.type(), tflite_buf, halide_buf, options);
        if (r.ok) {
            if (verbosity >= 2) {
                std::cout << "MATCHING output " << i << " is:\n";
                dynamic_type_dispatch<DumpBuffer>(halide_buf.type(), halide_buf);
            } else {
                std::cout << "OK!\n";
            }
        }
    }
};

void Runner::run(const std::string &filename) {
    std::cout << "Processing " << filename << " ...\n";

    const std::vector<char> buffer = read_entire_file(filename);

    std::map<WhichRun, RunResult> results;

    const std::array<WhichRun, kNumRuns> all_runs = {kTfLite, kHannk, kExternalDelegate, kInternalDelegate};

    std::vector<WhichRun> active_runs;
    for (WhichRun i : all_runs) {
        if (do_run[i]) {
            active_runs.push_back(i);
        }
    }

    const auto exec_tflite = [this, &buffer]() {
        return run_in_tflite(buffer);
    };
    const auto exec_hannk = [this, &buffer]() {
        return run_in_hannk(buffer);
    };
    const auto exec_hannk_external_delegate = [this, &buffer]() {
        DelegatePtr delegate_ptr;
        CHECK(delegate_ptr.init(external_delegate_path, verbosity));
        return run_in_tflite(buffer, delegate_ptr.get());
    };
    const auto exec_hannk_internal_delegate = [this, &buffer]() {
        HannkDelegateOptions options;
        options.verbosity = verbosity;
        TfLiteDelegate *delegate = HannkDelegateCreate(&options);
        auto result = run_in_tflite(buffer, delegate);
        HannkDelegateDelete(delegate);
        return result;
    };
    const std::map<WhichRun, std::function<RunResult()>> execs = {
        {kTfLite, exec_tflite},
        {kHannk, exec_hannk},
        {kExternalDelegate, exec_hannk_external_delegate},
        {kInternalDelegate, exec_hannk_internal_delegate},
    };

    std::cout << '\n';
    for (WhichRun i : active_runs) {
        std::cout << "Executing in " << RunNames[i] << " ...\n";
        results[i] = execs.at(i)();
    }

    // ----- Log benchmark times
    if (do_benchmark) {

        std::cout << '\n';
        for (WhichRun i : active_runs) {
            std::cout << RunNames[i] << " Time: " << std::chrono::duration_cast<std::chrono::microseconds>(results[i].time).count() << " us"
                      << "\n";
        }

        std::cout << '\n';
        for (WhichRun i : active_runs) {
            if (i == kTfLite) {
                continue;
            }
            double ratio = (results[i].time / results[kTfLite].time);
            std::cout << RunNames[i] << " = " << ratio * 100.0 << "% of " << RunNames[kTfLite];
            if (ratio > 1.0) {
                std::cout << "  *** " << RunNames[i] << " IS SLOWER";
            }
            std::cout << "\n";
        }
    }

    // ----- Now compare the outputs
    if (do_compare_results && do_run[kTfLite]) {
        std::cout << '\n';

        for (WhichRun i : active_runs) {
            if (i == kTfLite) {
                continue;
            }
            std::ostringstream msg;
            msg << "Comparing " << RunNames[kTfLite] << " vs " << RunNames[i] << ":";
            compare_results(msg.str(), results[kTfLite], results[i]);
        }
    }
}

}  // namespace hannk

int main(int argc, char **argv) {
    hannk::Runner runner;
    runner.seed = time(nullptr);
    std::vector<const char *> files;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) {
            runner.seed = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--enable")) {
            for (int i = 0; i < hannk::kNumRuns; i++) {
                runner.do_run[i] = false;
            }
            std::string opts = argv[++i];
            for (char c : opts) {
                switch (c) {
                case 't':
                    runner.do_run[hannk::kTfLite] = true;
                    break;
                case 'h':
                    runner.do_run[hannk::kHannk] = true;
                    break;
                case 'x':
                    runner.do_run[hannk::kExternalDelegate] = true;
                    break;
                case 'i':
                    runner.do_run[hannk::kInternalDelegate] = true;
                    break;
                default: {
                    std::cerr << "Unknown option to --enable: " << c << "\n";
                    return -1;
                }
                }
            }
            continue;
        }
        if (!strcmp(argv[i], "--external_delegate_path")) {
            runner.external_delegate_path = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--threads")) {
            runner.threads = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--compare")) {
            runner.do_compare_results = atoi(argv[++i]) != 0;
            continue;
        }
        if (!strcmp(argv[i], "--benchmark")) {
            runner.do_benchmark = atoi(argv[++i]) != 0;
            continue;
        }
        if (!strcmp(argv[i], "--verbose")) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                runner.verbosity = atoi(argv[++i]);
            } else {
                runner.verbosity = 1;
            }
            continue;
        }
        files.push_back(argv[i]);
    }
    if (runner.threads <= 0) {
#ifdef _WIN32
        char *num_cores = getenv("NUMBER_OF_PROCESSORS");
        runner.threads = num_cores ? atoi(num_cores) : 8;
#else
        runner.threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    }

    std::cout << "Using random seed: " << runner.seed << "\n";
    std::cout << "Using threads: " << runner.threads << "\n";

    {
        std::string tf_ver = TfLiteVersion();
        std::cout << "Using TFLite version: " << tf_ver << "\n";
        std::string expected = std::to_string(TFLITE_VERSION_MAJOR) + "." + std::to_string(TFLITE_VERSION_MINOR) + ".";
        if (tf_ver.find(expected) != 0) {
            std::cerr << "*** WARNING: compare_vs_tflite has been tested against TFLite v" << expected << "x, "
                      << "but is using " << tf_ver << "; results may be inaccurate or wrong.\n";
        }
    }

    for (auto f : files) {
        runner.run(f);
        halide_profiler_report(nullptr);
        halide_profiler_reset();
        std::cout << "\n";
    }

    std::cout << "Done!\n";
    return 0;
}
