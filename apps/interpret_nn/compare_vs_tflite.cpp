#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "buffer_util.h"
#include "error_util.h"
#include "file_util.h"
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
    case kTfLiteUInt64:
        return halide_type_t(halide_type_uint, 64);

    case kTfLiteString:
    case kTfLiteNoType:
    case kTfLiteComplex64:
    case kTfLiteComplex128:
        LOG_FATAL << "Unsupported TfLiteType: " << TfLiteTypeGetName(t);
        return halide_type_t();
    }
}

Buffer<void> wrap_tf_lite_tensor_with_halide_buffer(TfLiteTensor *t) {
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

}  // namespace

void run_both(const std::string &filename, int seed, int threads, bool verbose) {
    std::cout << "Comparing " << filename << "\n";

    std::vector<char> buffer = read_entire_file(filename);

    flatbuffers::Verifier verifier((const uint8_t *)buffer.data(), buffer.size());
    CHECK(tflite::VerifyModelBuffer(verifier));

    const tflite::Model *tf_model = tflite::GetModel(buffer.data());
    CHECK(tf_model);

    std::vector<Buffer<const void>> tflite_outputs, halide_outputs;
    std::chrono::duration<double> tflite_time, halide_time;
    std::map<std::string, int> seeds;

    // ----- Run in TFLite
    {
        TfLiteReporter tf_reporter;
        tflite::ops::builtin::BuiltinOpResolver tf_resolver;
        std::unique_ptr<tflite::Interpreter> tf_interpreter;
        tflite::InterpreterBuilder builder(tf_model, tf_resolver, &tf_reporter);
        TfLiteStatus status;
        CHECK((status = builder(&tf_interpreter)) == kTfLiteOk) << status;
        CHECK((status = tf_interpreter->AllocateTensors()) == kTfLiteOk) << status;
        CHECK((status = tf_interpreter->SetNumThreads(threads)) == kTfLiteOk) << status;

        // Fill in the inputs with random data, remembering the seeds so we can do the
        // same for the Halide inputs.
        for (int i : tf_interpreter->inputs()) {
            TfLiteTensor *t = tf_interpreter->tensor(i);
            if (t->allocation_type == kTfLiteMmapRo) {
                // The Tensor references data from the flatbuffer and is read-only;
                // presumably it is data we want to keep as-is
                if (verbose) {
                    std::cout << "TFLITE input " << t->name << " is being used as-is\n";
                }
                continue;
            }
            int seed_here = seed++;
            seeds[t->name] = seed_here;
            auto input_buf = wrap_tf_lite_tensor_with_halide_buffer(t);
            dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf, seed_here);
            if (verbose) {
                std::cout << "TFLITE input " << t->name << " inited with seed = " << seed_here
                          << " type " << input_buf.type() << " from " << TfLiteTypeGetName(t->type) << "\n";
            }
        }

        // Execute once, to prime the pump
        CHECK((status = tf_interpreter->Invoke()) == kTfLiteOk) << status;

        // Now benchmark it
        tflite_time = bench([&tf_interpreter]() {
            TfLiteStatus status;
            CHECK((status = tf_interpreter->Invoke()) == kTfLiteOk) << status;
        });

        // Save the outputs
        for (int i : tf_interpreter->outputs()) {
            TfLiteTensor *t = tf_interpreter->tensor(i);
            if (verbose) {
                std::cout << "TFLITE output is " << t->name << " type " << TfLiteTypeGetName(t->type) << "\n";
            }
            // Make a copy since the Buffer might reference memory owned by the tf_interpreter
            tflite_outputs.emplace_back(wrap_tf_lite_tensor_with_halide_buffer(t).copy());
        }
    }

    // ----- Run in Halide
    {
        Model model = parse_tflite_model(tf_model);
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
            auto seed_i = seeds.find(t->name());
            assert(seed_i != seeds.end());
            int seed_here = seed_i->second;
            auto input_buf = t->data<void>();
            dynamic_type_dispatch<FillWithRandom>(input_buf.type(), input_buf, seed_here);
            if (verbose) {
                std::cout << "HALIDE input " << t->name() << " inited with seed = " << seed_here << " type " << input_buf.type() << "\n";
            }
        }

        halide_set_num_threads(threads);

        // Execute once, to prime the pump
        interpreter.execute();

        // Now benchmark it
        halide_time = bench([&interpreter]() {
            interpreter.execute();
        });

        // Save the outputs
        for (Tensor *t : interpreter.outputs()) {
            if (verbose) {
                std::cout << "HALIDE output is " << t->name() << " type " << to_string(t->type()) << "\n";
            }
            // Make a copy since the Buffer might reference memory owned by the interpreter
            halide_outputs.emplace_back(t->data<const void>().copy());
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
    CHECK(tflite_outputs.size() == halide_outputs.size());
    for (size_t i = 0; i < tflite_outputs.size(); ++i) {
        const Buffer<const void> &tflite_buf = tflite_outputs[i];
        const Buffer<const void> &halide_buf = halide_outputs[i];
        CHECK(tflite_buf.type() == halide_buf.type()) << "Expected type " << tflite_buf.type() << "; saw type " << halide_buf.type();
        CHECK(tflite_buf.dimensions() == halide_buf.dimensions());
        for (int d = 0; d < tflite_buf.dimensions(); d++) {
            CHECK(tflite_buf.dim(d).min() == halide_buf.dim(d).min());
            CHECK(tflite_buf.dim(d).extent() == halide_buf.dim(d).extent());
            CHECK(tflite_buf.dim(d).stride() == halide_buf.dim(d).stride());  // TODO: must the strides match?
        }
        CompareBuffersResult r = dynamic_type_dispatch<CompareBuffers>(tflite_buf.type(), tflite_buf, halide_buf, CompareBuffersOptions());
        if (r.ok) {
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
#ifdef _WIN32
        char *num_cores = getenv("NUMBER_OF_PROCESSORS");
        threads = num_cores ? atoi(num_cores) : 8;
#else
        threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif
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
        interpret_nn::run_both(argv[i], seed, threads, verbose);
        std::cout << "\n";
    }

    std::cout << "Done!\n";
    return 0;
}
