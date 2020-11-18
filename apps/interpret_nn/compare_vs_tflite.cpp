#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "app_util.h"
#include "halide_benchmark.h"
#include "interpreter.h"
#include "tflite_parser.h"

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"

namespace interpret_nn {

using Halide::Runtime::Buffer;

namespace {

struct TfLiteReporter : public tflite::ErrorReporter {
    int Report(const char *format, va_list args) override {
        vfprintf(stderr, format, args);
        abort();
    }
};

size_t NumProcessorsOnline() {
#ifdef _WIN32
    char *num_cores = getenv("NUMBER_OF_PROCESSORS");
    return num_cores ? atoi(num_cores) : 8;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

inline std::ostream &operator<<(std::ostream &stream, const halide_type_t &type) {
    if (type.code == halide_type_uint && type.bits == 1) {
        stream << "bool";
    } else {
        assert(type.code >= 0 && type.code <= 3);
        static const char *const names[4] = {"int", "uint", "float", "handle"};
        stream << names[type.code] << (int)type.bits;
    }
    if (type.lanes > 1) {
        stream << "x" << (int)type.lanes;
    }
    return stream;
}

std::chrono::duration<double> bench(std::function<void()> f) {
#if 1
    auto result = Halide::Tools::benchmark(f);
    return std::chrono::duration<double>(result.wall_time);
#else
    auto begin = std::chrono::high_resolution_clock::now();
    auto end = begin;
    int loops = 0;
    do {
        f();
        loops++;
        end = std::chrono::high_resolution_clock::now();
    } while (end - begin < std::chrono::seconds(1));
    return (end - begin) / loops;
#endif
}

halide_type_t TfLiteTypeToHalideType(TfLiteType t) {
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
    case kTfLiteUInt64:
        return halide_type_t(halide_type_uint, 64);

    case kTfLiteString:
    case kTfLiteNoType:
    case kTfLiteComplex64:
    case kTfLiteComplex128:
        APP_FATAL << "Unsupported TfLiteType: " << TfLiteTypeGetName(t);
        return halide_type_t();
    }
}

// Must be constexpr to allow use in case clauses.
inline constexpr int halide_type_code(halide_type_code_t code, int bits) {
    return (((int)code) << 8) | bits;
}

// dynamic_type_dispatch is a utility for functors that want to be able
// to dynamically dispatch a halide_type_t to type-specialized code.
// To use it, a functor must be a *templated* class, e.g.
//
//     template<typename T> class MyFunctor { int operator()(arg1, arg2...); };
//
// dynamic_type_dispatch() is called with a halide_type_t as the first argument,
// followed by the arguments to the Functor's operator():
//
//     auto result = dynamic_type_dispatch<MyFunctor>(some_halide_type, arg1, arg2);
//
// Note that this means that the functor must be able to instantiate its
// operator() for all the Halide scalar types; it also means that all those
// variants *will* be instantiated (increasing code size), so this approach
// should only be used when strictly necessary.
template<template<typename> class Functor, typename... Args>
auto dynamic_type_dispatch(const halide_type_t &type, Args &&...args)
    -> decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...)) {

#define HANDLE_CASE(CODE, BITS, TYPE)  \
    case halide_type_code(CODE, BITS): \
        return Functor<TYPE>()(std::forward<Args>(args)...);
    switch (halide_type_code((halide_type_code_t)type.code, type.bits)) {
        // HANDLE_CASE(halide_type_float, 16, float)  // TODO
        HANDLE_CASE(halide_type_float, 32, float)
        HANDLE_CASE(halide_type_float, 64, double)
        HANDLE_CASE(halide_type_int, 8, int8_t)
        HANDLE_CASE(halide_type_int, 16, int16_t)
        HANDLE_CASE(halide_type_int, 32, int32_t)
        HANDLE_CASE(halide_type_int, 64, int64_t)
        HANDLE_CASE(halide_type_uint, 1, bool)
        HANDLE_CASE(halide_type_uint, 8, uint8_t)
        HANDLE_CASE(halide_type_uint, 16, uint16_t)
        HANDLE_CASE(halide_type_uint, 32, uint32_t)
        HANDLE_CASE(halide_type_uint, 64, uint64_t)
    default:
        APP_FATAL << "Unsupported type";
        using ReturnType = decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...));
        return ReturnType();
    }
#undef HANDLE_CASE
}

template<typename T>
struct CompareBuffers {
public:
    uint64_t operator()(const Buffer<const void> &tflite_buf_dynamic, const Buffer<const void> &halide_buf_dynamic) {
        Buffer<const T> tflite_buf = tflite_buf_dynamic;
        Buffer<const T> halide_buf = halide_buf_dynamic;
        uint64_t diffs = 0;
        constexpr uint64_t max_diffs_to_show = 32;
        tflite_buf.for_each_element([&](const int *pos) {
            T tflite_buf_val = tflite_buf(pos);
            T halide_buf_val = halide_buf(pos);
            // TODO: this is terrible, we should compare with some threshold instead of equality
            if (tflite_buf_val != halide_buf_val) {
                diffs++;
                if (diffs > max_diffs_to_show) {
                    return;
                }
                std::cerr << "*** Mismatch at (";
                for (int i = 0; i < tflite_buf.dimensions(); ++i) {
                    if (i > 0) std::cerr << ", ";
                    std::cerr << pos[i];
                }
                std::cerr << "): tflite " << 0 + tflite_buf_val << " halide " << 0 + halide_buf_val << "\n";
            }
        });
        if (diffs > max_diffs_to_show) {
            std::cerr << "(" << (diffs - max_diffs_to_show) << " diffs suppressed)\n";
        }
        return diffs;
    }
};

template<typename T>
void FillWithRandomImpl(Buffer<T> &b, std::mt19937 &rng) {
    std::uniform_int_distribution<T> dis(std::numeric_limits<T>::min(),
                                         std::numeric_limits<T>::max());
    b.for_each_value([&rng, &dis](T &value) {
        value = dis(rng);
    });
}

template<>
void FillWithRandomImpl(Buffer<float> &b, std::mt19937 &rng) {
    // Floating point. We arbitrarily choose to use the range [0.0, 1.0].
    std::uniform_real_distribution<float> dis(0.0, 1.0);
    b.for_each_value([&rng, &dis](float &value) {
        value = dis(rng);
    });
}

template<>
void FillWithRandomImpl(Buffer<double> &b, std::mt19937 &rng) {
    // Floating point. We arbitrarily choose to use the range [0.0, 1.0].
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    b.for_each_value([&rng, &dis](double &value) {
        value = dis(rng);
    });
}

template<>
void FillWithRandomImpl(Buffer<bool> &b, std::mt19937 &rng) {
    std::uniform_int_distribution<int> dis(0, 1);
    b.for_each_value([&rng, &dis](bool &value) {
        value = static_cast<bool>(dis(rng));
    });
}

template<typename T>
struct FillWithRandom {
public:
    void operator()(Buffer<> &b_dynamic, int seed) {
        Buffer<T> b = b_dynamic;
        std::mt19937 rng(seed);
        FillWithRandomImpl<T>(b, rng);
    }
};

template<typename T>
struct DumpBuffer {
public:
    void operator()(const Buffer<const void> &buf_dynamic) {
        Buffer<const T> buf = buf_dynamic;
        buf.for_each_element([&](const int *pos) {
            T val = buf(pos);
            std::cerr << "Value at (";
            for (int i = 0; i < buf.dimensions(); ++i) {
                if (i > 0) std::cerr << ", ";
                std::cerr << pos[i];
            }
            std::cerr << "): " << 0 + val << "\n";
        });
    }
};

Buffer<void> WrapTfLiteTensorWithHalideBuffer(TfLiteTensor *t) {
    APP_CHECK(t->dims);
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
    APP_CHECK(buffer_data);

    halide_type_t type = TfLiteTypeToHalideType(t->type);
    Buffer<void> b(type, buffer_data, shape.size(), shape.data());
    APP_CHECK(b.size_in_bytes() == t->bytes);
    return b;
}

}  // namespace

void RunBoth(const std::string &filename, int seed, int threads, bool verbose) {
    std::cout << "Comparing " << filename << "\n";

    std::vector<char> buffer = app_util::ReadEntireFile(filename);

    flatbuffers::Verifier verifier((const uint8_t *)buffer.data(), buffer.size());
    APP_CHECK(tflite::VerifyModelBuffer(verifier));

    const tflite::Model *tf_model = tflite::GetModel(buffer.data());
    APP_CHECK(tf_model);

    std::vector<Buffer<const void>> tflite_outputs, halide_outputs;
    std::chrono::duration<double> tflite_time, halide_time;

    // ----- Run in TFLite
    {
        TfLiteReporter tf_reporter;
        tflite::ops::builtin::BuiltinOpResolver tf_resolver;
        std::unique_ptr<tflite::Interpreter> tf_interpreter;
        tflite::InterpreterBuilder builder(tf_model, tf_resolver, &tf_reporter);
        TfLiteStatus status;
        APP_CHECK((status = builder(&tf_interpreter)) == kTfLiteOk) << status;
        APP_CHECK((status = tf_interpreter->AllocateTensors()) == kTfLiteOk) << status;
        APP_CHECK((status = tf_interpreter->SetNumThreads(threads)) == kTfLiteOk) << status;

        // Fill in the inputs with random data (but with a predictable seed,
        // so we can do the same for the Halide inputs).
        int seed_here = seed;
        for (int i : tf_interpreter->inputs()) {
            seed_here++;
            TfLiteTensor *t = tf_interpreter->tensor(i);
            if (t->allocation_type == kTfLiteMmapRo) {
                // The Tensor references data from the flatbuffer and is read-only;
                // presumably it is data we want to keep as-is
                if (verbose) {
                    std::cout << "TFLITE input " << t->name << " is being used as-is\n";
                }
                continue;
            }
            auto input_buf = WrapTfLiteTensorWithHalideBuffer(t);
            dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf, seed_here);
            if (verbose) {
                std::cout << "TFLITE input " << t->name << " inited with seed = " << seed_here
                          << " type " << input_buf.type() << " from " << TfLiteTypeGetName(t->type) << "\n";
            }
        }

        // Execute once, to prime the pump
        APP_CHECK((status = tf_interpreter->Invoke()) == kTfLiteOk) << status;

        // Now benchmark it
        tflite_time = bench([&tf_interpreter]() {
            TfLiteStatus status;
            APP_CHECK((status = tf_interpreter->Invoke()) == kTfLiteOk) << status;
        });

        // Save the outputs
        for (int i : tf_interpreter->outputs()) {
            TfLiteTensor *t = tf_interpreter->tensor(i);
            if (verbose) {
                std::cout << "TFLITE output is " << t->name << " type " << TfLiteTypeGetName(t->type) << "\n";
            }
            // Make a copy since the Buffer might reference memory owned by the tf_interpreter
            tflite_outputs.emplace_back(WrapTfLiteTensorWithHalideBuffer(t).copy());
        }
    }

    // ----- Run in Halide
    {
        Model model = ParseTfLiteModel(tf_model);
        if (verbose) {
            model.Dump(std::cout);
        }

        // TODO: this is a little ugly. Maybe it would be better to have Tensor
        // have a flag for whether it was pre-inited during parsing?
        std::set<Tensor *> read_only_tensors;
        for (auto &t : model.tensors) {
            if (t->IsAllocated()) {
                // It has data from the Model; keep track so we don't fill it with randomness
                read_only_tensors.insert(t.get());
            } else {
                t->Allocate();
            }
        }

        ModelInterpreter interpreter(std::move(model));

        // Fill in the inputs with random data (but with the same seeds as above).
        int seed_here = seed;
        for (Tensor *t : interpreter.Inputs()) {
            seed_here++;
            if (read_only_tensors.count(t)) {
                // It has data from the Model -- leave it as-is
                if (verbose) {
                    std::cout << "HALIDE input " << t->Name() << " is being used as-is\n";
                }
                continue;
            }
            auto input_buf = t->Data<void>();
            dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf, seed_here);
            if (verbose) {
                std::cout << "HALIDE input " << t->Name() << " inited with seed = " << seed_here << " type " << input_buf.type() << "\n";
            }
        }

        halide_set_num_threads(threads);

        // Execute once, to prime the pump
        interpreter.Execute();

        // Now benchmark it
        halide_time = bench([&interpreter]() {
            interpreter.Execute();
        });

        // Save the outputs
        for (Tensor *t : interpreter.Outputs()) {
            if (verbose) {
                std::cout << "HALIDE output is " << t->Name() << " type " << TensorTypeToString(t->Type()) << "\n";
            }
            // Make a copy since the Buffer might reference memory owned by the interpreter
            halide_outputs.emplace_back(t->Data<const void>().copy());
        }
    }

    // ----- Log benchmark times
    std::cout << "TFLITE Time: " << std::chrono::duration_cast<std::chrono::microseconds>(tflite_time).count() << " us"
              << "\n";
    std::cout << "HALIDE Time: " << std::chrono::duration_cast<std::chrono::microseconds>(halide_time).count() << " us"
              << "\n";

    double ratio = (halide_time / tflite_time);
    std::cout << "HALIDE = " << ratio * 100.0 << "% of TFLITE";
    if (ratio > 1.0) {
        std::cout << "  *** HALIDE IS SLOWER";
    }
    std::cout << "\n";

    // ----- Now compare the outputs
    APP_CHECK(tflite_outputs.size() == halide_outputs.size());
    for (size_t i = 0; i < tflite_outputs.size(); ++i) {
        const Buffer<const void> &tflite_buf = tflite_outputs[i];
        const Buffer<const void> &halide_buf = halide_outputs[i];
        APP_CHECK(tflite_buf.type() == halide_buf.type());
        APP_CHECK(tflite_buf.dimensions() == halide_buf.dimensions());
        for (int d = 0; d < tflite_buf.dimensions(); d++) {
            APP_CHECK(tflite_buf.dim(d).min() == halide_buf.dim(d).min());
            APP_CHECK(tflite_buf.dim(d).extent() == halide_buf.dim(d).extent());
            APP_CHECK(tflite_buf.dim(d).stride() == halide_buf.dim(d).stride());  // TODO: must the strides match?
        }
        uint64_t diffs = dynamic_type_dispatch<CompareBuffers>(tflite_buf.type(), tflite_buf, halide_buf);
        if (diffs == 0) {
            if (verbose) {
                std::cout << "MATCHING output " << i << " is:\n";
                dynamic_type_dispatch<DumpBuffer>(halide_buf.type(), halide_buf);
            }
        }
    }
}

}  // namespace interpret_nn

int main(int argc, char **argv) {
    int seed = time(nullptr);
    int threads = 1;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) {
            seed = atoi(argv[++i]);
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
    }
    if (threads <= 0) {
        threads = interpret_nn::NumProcessorsOnline();
    }

    std::cout << "Using random seed: " << seed << "\n";
    std::cout << "Using threads: " << threads << "\n";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") || !strcmp(argv[i], "--threads")) {
            i++;
            continue;
        }
        if (!strcmp(argv[i], "--verbose")) {
            continue;
        }
        interpret_nn::RunBoth(argv[i], seed, threads, verbose);
        std::cout << "\n";
    }

    std::cout << "Done!\n";
    return 0;
}
