#include <chrono>
#include <dlfcn.h>
#include <iostream>
#include <random>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "util/model_runner.h"

#if HANNK_BUILD_TFLITE
#include "delegate/hannk_delegate.h"
#endif
#include "halide_benchmark.h"
#include "interpreter/interpreter.h"
#include "tflite/tflite_parser.h"
#include "util/buffer_util.h"
#include "util/error_util.h"
#include "util/file_util.h"

#if HANNK_BUILD_TFLITE
// IMPORTANT: use only the TFLite C API here.
#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/c/common.h"
#endif

namespace hannk {
namespace {

std::chrono::duration<double> bench(std::function<void()> f) {
    auto result = Halide::Tools::benchmark(f);
    return std::chrono::duration<double>(result.wall_time);
}

#if HANNK_BUILD_TFLITE
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
        HCHECK(0) << "Unsupported TfLiteType: " << TfLiteTypeGetName(t);
        return halide_type_t();
    }
}

HalideBuffer<void> wrap_tf_lite_tensor_with_halide_buffer(const TfLiteTensor *t) {
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
    HalideBuffer<void> b(type, buffer_data, shape.size(), shape.data());
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
        HCHECK(delegate_lib_ == nullptr);
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
#endif

static const char *const RunNames[ModelRunner::kNumRuns] = {
    "TfLite",
    "Hannk",
    "HannkExternalDelegate",
    "HannkInternalDelegate",
};

}  // namespace

int FlagProcessor::handle_nonflag(const std::string &s) {
    // just ignore it
    return 0;
}

int FlagProcessor::handle_unknown_flag(const std::string &s) {
    std::cerr << "Unknown flag '" << s << "'\n";
    return -1;
}

int FlagProcessor::handle_missing_value(const std::string &s) {
    std::cerr << "Missing value for flag '" << s << "'\n";
    return -1;
}

int FlagProcessor::process(int argc, char **argv) const {
    int r;
    for (int i = 1; i < argc; i++) {
        std::string flag = argv[i];
        if (flag[0] != '-') {
            r = nonflag_handler(flag);
            if (r != 0) {
                return r;
            } else {
                continue;
            }
        }
        flag = flag.substr(1);
        if (flag[0] == '-') {
            flag = flag.substr(1);
        }

        std::string value;
        auto eq = flag.find('=');
        if (eq != std::string::npos) {
            value = flag.substr(eq + 1);
            flag = flag.substr(0, eq);
        } else if (i + 1 < argc) {
            value = argv[++i];
        } else {
            r = missing_value_handler(flag);
            if (r != 0) {
                return r;
            } else {
                continue;
            }
        }
        auto it = flag_handlers.find(flag);
        if (it == flag_handlers.end()) {
            r = unknown_flag_handler(flag);
            if (r != 0) {
                return r;
            } else {
                continue;
            }
        }
        r = it->second(value);
        if (r != 0) {
            return r;
        }
    }
    return 0;
}

void SeedTracker::reset(int seed) {
    next_seed_ = seed;
    seeds_.clear();
}

int SeedTracker::next_seed() const {
    return next_seed_;
}

int SeedTracker::seed_for_name(const std::string &name) {
    auto it = seeds_.find(name);
    if (it != seeds_.end()) {
        return it->second;
    }
    const int seed_here = next_seed_++;
    seeds_[name] = seed_here;
    return seed_here;
}

#if HANNK_BUILD_TFLITE
/*static*/ void TfLiteModelRunner::ErrorReporter(void *user_data, const char *format, va_list args) {
    TfLiteModelRunner *self = (TfLiteModelRunner *)user_data;
    if (self->verbose_output_) {
        // 1k of error message ought to be enough for anybody...
        char buffer[1024];

        va_list args_copy;
        va_copy(args_copy, args);
        vsnprintf(buffer, sizeof(buffer), format, args_copy);
        va_end(args_copy);

        *self->verbose_output_ << buffer;
    }
}

TfLiteModelRunner::TfLiteModelRunner(const std::vector<char> &buffer,
                                     int threads,
                                     SeedTracker &seed_tracker,
                                     std::ostream *verbose_output,
                                     TfLiteDelegate *delegate)
    : verbose_output_(verbose_output) {
    tf_model_ = TfLiteModelCreate(buffer.data(), buffer.size());
    HCHECK(tf_model_);

    tf_options_ = TfLiteInterpreterOptionsCreate();
    HCHECK(tf_options_);
    TfLiteInterpreterOptionsSetNumThreads(tf_options_, threads);
    TfLiteInterpreterOptionsSetErrorReporter(tf_options_, ErrorReporter, (void *)this);
    if (delegate) {
        TfLiteInterpreterOptionsAddDelegate(tf_options_, delegate);
    }

    tf_interpreter_ = TfLiteInterpreterCreate(tf_model_, tf_options_);
    HCHECK(tf_interpreter_);

    // The options/model can be deleted immediately after interpreter creation.
    TfLiteInterpreterOptionsDelete(tf_options_);
    tf_options_ = nullptr;
    TfLiteModelDelete(tf_model_);
    tf_model_ = nullptr;

    TfLiteStatus status;
    HCHECK((status = TfLiteInterpreterAllocateTensors(tf_interpreter_)) ==
           kTfLiteOk)
        << status;

    const int inputs = TfLiteInterpreterGetInputTensorCount(tf_interpreter_);

    // Fill in the inputs with predictable pseudorandom data as before.
    for (int i = 0; i < inputs; i++) {
        TfLiteTensor *t = TfLiteInterpreterGetInputTensor(tf_interpreter_, i);
        if (t->allocation_type == kTfLiteMmapRo) {
            // The Tensor references data from the flatbuffer and is read-only;
            // presumably it is data we want to keep as-is
            if (verbose_output_) {
                *verbose_output_ << "TFLITE input " << t->name
                                 << " is being used as-is\n";
            }
            continue;
        }
        const int seed_here = seed_tracker.seed_for_name(t->name);
        auto input_buf = wrap_tf_lite_tensor_with_halide_buffer(t);
        dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf,
                                              seed_here);
        if (verbose_output_) {
            *verbose_output_ << "TFLITE input " << t->name
                             << " inited with seed = " << seed_here << " type "
                             << input_buf.type() << " from "
                             << TfLiteTypeGetName(t->type) << "\n";
        }
    }
}

void TfLiteModelRunner::run_once() {
    TfLiteStatus status;
    HCHECK((status = TfLiteInterpreterInvoke(tf_interpreter_)) == kTfLiteOk) << status;
}

std::vector<HalideBuffer<const void>> TfLiteModelRunner::copy_outputs() {
    std::vector<HalideBuffer<const void>> results;
    const int outputs = TfLiteInterpreterGetOutputTensorCount(tf_interpreter_);
    for (int i = 0; i < outputs; i++) {
        const TfLiteTensor *t =
            TfLiteInterpreterGetOutputTensor(tf_interpreter_, i);
        if (verbose_output_) {
            *verbose_output_ << "TFLITE output is " << t->name << " type "
                             << TfLiteTypeGetName(t->type) << "\n";
        }
        // Make a copy since the Buffer might reference memory owned by the
        // tf_interpreter_
        results.emplace_back(wrap_tf_lite_tensor_with_halide_buffer(t).copy());
    }
    return results;
}

TfLiteModelRunner::~TfLiteModelRunner() {
    if (tf_interpreter_) {
        TfLiteInterpreterDelete(tf_interpreter_);
    }
    if (tf_options_) {
        TfLiteInterpreterOptionsDelete(tf_options_);
    }
    if (tf_model_) {
        TfLiteModelDelete(tf_model_);
    }
}
#endif

ModelRunner::ModelRunner() {
    for (int i = 0; i < kNumRuns; i++) {
#if HANNK_BUILD_TFLITE
        do_run[i] = true;
#else
        do_run[i] = (i == kHannk);
#endif
    }
#if defined(__arm__) || defined(__aarch64__)
    // TFLite on Arm devices generally uses the rounding-shift instructions,
    // which should match our results exactly (since we mimic the same result,
    // whether or not we actually generate those specific instructions).
    // So leave the options at their default.
    tolerance = 1.0 / 256.0;
#else
    // TFLite on x86 (on desktop platforms, at least) appears to mostly
    // use the reference implementations, which don't have the same
    // rounding-shift behavior. We'll bump up the 'close' value for these.
    // This is a lttle hand-wavy but is a decent proxy for now.
    tolerance = 1.0 / 100.0;
#endif
}

void ModelRunner::set_seed(int seed) {
    seed_tracker_.reset(seed);
}

void ModelRunner::status() {
    if (verbosity > 0) {
        std::cout << "Using random seed: " << seed_tracker_.next_seed() << "\n";
        std::cout << "Using threads: " << threads << "\n";

#if HANNK_BUILD_TFLITE
        std::string tf_ver = TfLiteVersion();
        std::cout << "Using TFLite version: " << tf_ver << "\n";
        std::string expected = std::to_string(TFLITE_VERSION_MAJOR) + "." + std::to_string(TFLITE_VERSION_MINOR) + ".";
        if (tf_ver.find(expected) != 0) {
            std::cerr << "*** WARNING: compare_vs_tflite has been tested against TFLite v" << expected << "x, "
                      << "but is using " << tf_ver << "; results may be inaccurate or wrong.\n";
        }
#else
        std::cout << "Built without TFLite support.\n";
#endif
    }
}

ModelRunner::RunResult ModelRunner::run_in_hannk(const std::vector<char> &buffer) {
    RunResult result;

    std::unique_ptr<OpGroup> model = parse_tflite_model_from_buffer(buffer.data());
    if (verbosity) {
        std::cout << "Model after parsing:\n";
        model->dump(std::cout);
    }

    InterpreterOptions options;
    options.verbosity = verbosity;
    Interpreter interpreter(std::move(model), std::move(options));
    if (!interpreter.prepare()) {
        std::cerr << "hannk::Interpreter::prepare() failed\n";
        // TODO: probably better form to return an error here, but for now, this is fine.
        exit(1);
    }

    // Fill in the inputs with pseudorandom data (save the seeds for later).
    for (TensorPtr t : interpreter.inputs()) {
        if (t->is_constant()) {
            // Skip constant buffers, just like TFlite does later on.
            continue;
        }
        const int seed_here = seed_tracker_.seed_for_name(t->name());
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

#if HANNK_BUILD_TFLITE
ModelRunner::RunResult ModelRunner::run_in_tflite(const std::vector<char> &buffer, TfLiteDelegate *delegate) {
    RunResult result;

    TfLiteModelRunner tfrunner(buffer, threads, seed_tracker_, verbosity >= 1 ? &std::cout : nullptr, delegate);

    // Execute once, to prime the pump
    tfrunner.run_once();

    // Save the outputs from that execution (before benchmarking)
    result.outputs = tfrunner.copy_outputs();

    // Now benchmark it
    if (do_benchmark) {
        result.time = bench([&tfrunner]() {
            tfrunner.run_once();
        });
    }

    return result;
}
#endif

bool ModelRunner::compare_results(const std::string &name_a, const std::string &name_b, const RunResult &a, const RunResult &b) {
    bool all_matched = true;
    HCHECK(a.outputs.size() == b.outputs.size());
    for (size_t i = 0; i < a.outputs.size(); ++i) {
        const HalideBuffer<const void> &tflite_buf = a.outputs[i];
        const HalideBuffer<const void> &halide_buf = b.outputs[i];
        HCHECK(tflite_buf.type() == halide_buf.type()) << "Expected type " << tflite_buf.type() << "; saw type " << halide_buf.type();
        HCHECK(tflite_buf.dimensions() == halide_buf.dimensions());
        for (int d = 0; d < tflite_buf.dimensions(); d++) {
            HCHECK(tflite_buf.dim(d).min() == halide_buf.dim(d).min());
            HCHECK(tflite_buf.dim(d).extent() == halide_buf.dim(d).extent());
            HCHECK(tflite_buf.dim(d).stride() == halide_buf.dim(d).stride());  // TODO: must the strides match?
        }
        CompareBuffersOptions options;
        options.close_thresh = std::ceil((1ull << tflite_buf.type().bits) * tolerance);
        options.max_diffs_to_log = 8;
        options.verbose = !csv_output;
        CompareBuffersResult r = dynamic_type_dispatch<CompareBuffers>(tflite_buf.type(), tflite_buf, halide_buf, options);
        if (r.ok) {
            if (verbosity >= 2) {
                std::cout << "Comparing " << name_a << " vs " << name_b << ": MATCHING output " << i << " is:\n";
                dynamic_type_dispatch<DumpBuffer>(halide_buf.type(), halide_buf);
            }
        } else {
            all_matched = false;
        }
    }
    return all_matched;
};

int ModelRunner::parse_flags(int argc, char **argv, std::vector<std::string> &files_to_process) {
    int seed = time(nullptr);

    FlagProcessor fp;

    fp.nonflag_handler = [&files_to_process](const std::string &value) -> int {
        // Assume it's a file.
        files_to_process.push_back(value);
        return 0;
    };

    fp.flag_handlers = FlagProcessor::FnMap{
        {"benchmark", [this](const std::string &value) {
             this->do_benchmark = std::stoi(value) != 0;
             return 0;
         }},
        {"compare", [this](const std::string &value) {
             this->do_compare_results = std::stoi(value) != 0;
             return 0;
         }},
        {"csv", [this](const std::string &value) {
             this->csv_output = std::stoi(value) != 0;
             return 0;
         }},
        {"enable", [this](const std::string &value) {
             for (int i = 0; i < ModelRunner::kNumRuns; i++) {
                 this->do_run[i] = false;
             }
             for (char c : value) {
                 switch (c) {
                 case 'h':
                     this->do_run[ModelRunner::kHannk] = true;
                     break;
#if HANNK_BUILD_TFLITE
                 case 't':
                     this->do_run[ModelRunner::kTfLite] = true;
                     break;
                 case 'x':
                     this->do_run[ModelRunner::kExternalDelegate] = true;
                     break;
                 case 'i':
                     this->do_run[ModelRunner::kInternalDelegate] = true;
                     break;
#else
                 case 't':
                 case 'x':
                 case 'i':
                    std::cerr << "Unsupported option to --enable (TFLite is not enabled in this build): " << c << "\n";
                    return -1;
                    break;
#endif
                 default:
                     std::cerr << "Unknown option to --enable: " << c << "\n";
                     return -1;
                 }
             }
             return 0;
         }},
        {"external_delegate_path", [this](const std::string &value) {
             this->external_delegate_path = value;
             return 0;
         }},
        {"keep_going", [this](const std::string &value) {
             this->keep_going = std::stoi(value) != 0;
             return 0;
         }},
        {"seed", [&seed](const std::string &value) {
             seed = std::stoi(value);
             return 0;
         }},
        {"threads", [this](const std::string &value) {
             this->threads = std::stoi(value);
             return 0;
         }},
        {"tolerance", [this](const std::string &value) {
             this->tolerance = std::stof(value);
             return 0;
         }},
        {"verbose", [this](const std::string &value) {
             this->verbosity = std::stoi(value);
             return 0;
         }},
    };

    int r = fp.process(argc, argv);
    if (r != 0) {
        return r;
    }

    if (this->threads <= 0) {
#ifdef _WIN32
        char *num_cores = getenv("NUMBER_OF_PROCESSORS");
        this->threads = num_cores ? atoi(num_cores) : 8;
#else
        this->threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    }

    this->set_seed(seed);
    return 0;
}

void ModelRunner::run(const std::string &filename) {
    std::map<WhichRun, RunResult> results;

    if (run_count == 0) {
        run_count++;

        for (int i = 0; i < kNumRuns; i++) {
            if (do_run[i]) {
                active_runs.push_back((WhichRun)i);
            }
        }

        if (csv_output) {
            // Output column headers
            std::cout << "Filename";
            if (do_benchmark) {
                for (WhichRun i : active_runs) {
                    std::cout << ',' << RunNames[i] << "_time_us";
                }
            }
            if (do_compare_results && do_run[kTfLite]) {
                for (WhichRun i : active_runs) {
                    if (i == kTfLite) {
                        continue;
                    }
                    std::cout << ',' << RunNames[i] << "_matches_tflite";
                }
            }
            std::cout << "\n";
        }
    }

    if (csv_output) {
        // Try to print just the filename rather than a full pathname
        const auto n = filename.rfind('/');
        std::cout << (n == std::string::npos ? filename : filename.substr(n + 1));
    } else {
        std::cout << "Processing " << filename << " ...\n";
    }

    const std::vector<char> buffer = read_entire_file(filename);

#if HANNK_BUILD_TFLITE
    const auto exec_tflite = [this, &buffer]() {
        return run_in_tflite(buffer);
    };
    const auto exec_hannk = [this, &buffer]() {
        return run_in_hannk(buffer);
    };
    const auto exec_hannk_external_delegate = [this, &buffer]() {
        DelegatePtr delegate_ptr;
        HCHECK(delegate_ptr.init(external_delegate_path, verbosity));
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
#endif

    for (WhichRun i : active_runs) {
#if HANNK_BUILD_TFLITE
        results[i] = execs.at(i)();
#else
        if (i != kHannk) {
            std::cerr << "Only kHannk is available in this build.\n";
            exit(1);
        }
        results[i] = run_in_hannk(buffer);
#endif
    }

    // ----- Log benchmark times
    if (do_benchmark) {
        for (WhichRun i : active_runs) {
            const auto t = std::chrono::duration_cast<std::chrono::microseconds>(results[i].time).count();
            if (csv_output) {
                std::cout << ',' << t;
            } else {
                std::cout << RunNames[i] << " Time: " << t << " us\n";
            }
        }
    }

    // ----- Now compare the outputs
    if (do_compare_results && do_run[kTfLite]) {
        bool all_matched = true;
        for (WhichRun i : active_runs) {
            if (i == kTfLite) {
                continue;
            }
            const bool matched = compare_results(RunNames[kTfLite], RunNames[i], results[kTfLite], results[i]);
            if (csv_output) {
                std::cout << ',' << (matched ? '1' : '0');
            }
            if (!matched) {
                all_matched = false;
            }
        }

        if (!all_matched && !keep_going) {
            exit(1);
        }
    }

    if (csv_output) {
        std::cout << "\n";
    }
}

}  // namespace hannk
