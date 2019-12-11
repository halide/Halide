#include <iostream>
#include <limits>

#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

#include "resize_box_float32_down.h"
#include "resize_box_float32_up.h"
#include "resize_box_uint16_down.h"
#include "resize_box_uint16_up.h"
#include "resize_box_uint8_down.h"
#include "resize_box_uint8_up.h"
#include "resize_cubic_float32_down.h"
#include "resize_cubic_float32_up.h"
#include "resize_cubic_uint16_down.h"
#include "resize_cubic_uint16_up.h"
#include "resize_cubic_uint8_down.h"
#include "resize_cubic_uint8_up.h"
#include "resize_lanczos_float32_down.h"
#include "resize_lanczos_float32_up.h"
#include "resize_lanczos_uint16_down.h"
#include "resize_lanczos_uint16_up.h"
#include "resize_lanczos_uint8_down.h"
#include "resize_lanczos_uint8_up.h"
#include "resize_linear_float32_down.h"
#include "resize_linear_float32_up.h"
#include "resize_linear_uint16_down.h"
#include "resize_linear_uint16_up.h"
#include "resize_linear_uint8_down.h"
#include "resize_linear_uint8_up.h"

std::string infile, outfile, input_type, interpolation_type;
float scale_factor = 1.0f;
int benchmark_iters = 10;
bool packed = true;

void show_usage_and_exit() {
    fprintf(stderr,
            "Usage:\n"
            "\t./resample [-f scalefactor] "
            "[-b benchmark_iterations] "
            "[-i box|linear|cubic|lanczos] "
            "[-t float32|uint8|uint16] in.png out.png\n");
    exit(1);
}

void parse_commandline(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-f" && i + 1 < argc) {
            scale_factor = atof(argv[++i]);
        } else if (arg == "-i" && i + 1 < argc) {
            interpolation_type = argv[++i];
        } else if (arg == "-t" && i + 1 < argc) {
            input_type = argv[++i];
        } else if (arg == "-b" && i + 1 < argc) {
            benchmark_iters = atoi(argv[++i]);
        } else if (arg == "-p" && i + 1 < argc) {
            packed = atoi(argv[++i]) != 0;
        } else if (infile.empty()) {
            infile = arg;
        } else if (outfile.empty()) {
            outfile = arg;
        } else {
            fprintf(stderr, "Unexpected command line option '%s'.\n", arg.c_str());
            show_usage_and_exit();
        }
    }

    if (infile.empty() || outfile.empty() || interpolation_type.empty()) {
        show_usage_and_exit();
    }
}

int main(int argc, char **argv) {
    parse_commandline(argc, argv);

    Halide::Runtime::Buffer<> in = Halide::Tools::load_image(infile);
    int out_width = in.width() * scale_factor;
    int out_height = in.height() * scale_factor;

    decltype(&resize_box_float32_up) variants[3][2][4] =
        {
            {{&resize_box_float32_up,
              &resize_cubic_float32_up,
              &resize_linear_float32_up,
              &resize_lanczos_float32_up},
             {&resize_box_float32_down,
              &resize_cubic_float32_down,
              &resize_linear_float32_down,
              &resize_lanczos_float32_down}},
            {{&resize_box_uint8_up,
              &resize_cubic_uint8_up,
              &resize_linear_uint8_up,
              &resize_lanczos_uint8_up},
             {&resize_box_uint8_down,
              &resize_cubic_uint8_down,
              &resize_linear_uint8_down,
              &resize_lanczos_uint8_down}},
            {{&resize_box_uint16_up,
              &resize_cubic_uint16_up,
              &resize_linear_uint16_up,
              &resize_lanczos_uint16_up},
             {&resize_box_uint16_down,
              &resize_cubic_uint16_down,
              &resize_linear_uint16_down,
              &resize_lanczos_uint16_down}}};

    int interpolation_idx = 0;
    if (interpolation_type == "box") {
        interpolation_idx = 0;
    } else if (interpolation_type == "cubic") {
        interpolation_idx = 1;
    } else if (interpolation_type == "linear") {
        interpolation_idx = 2;
    } else if (interpolation_type == "lanczos") {
        interpolation_idx = 3;
    } else {
        fprintf(stderr, "Unknown interpolation type: %s\n", interpolation_type.c_str());
        show_usage_and_exit();
    }

    int upsample_idx = scale_factor > 1.0f ? 0 : 1;

    // Instead of just adapting to the actual type of the input, we'll
    // convert it to the requested type to make it easier to benchmark
    // lots of different types.
    int type_idx = 0;
    if (input_type == "float32") {
        in = Halide::Tools::ImageTypeConversion::convert_image(in, halide_type_of<float>());
        type_idx = 0;
    } else if (input_type == "uint8") {
        in = Halide::Tools::ImageTypeConversion::convert_image(in, halide_type_of<uint8_t>());
        type_idx = 1;
    } else if (input_type == "uint16") {
        in = Halide::Tools::ImageTypeConversion::convert_image(in, halide_type_of<uint16_t>());
        type_idx = 2;
    } else {
        fprintf(stderr, "Unhandled type: %s\n", input_type.c_str());
        show_usage_and_exit();
    }

    Halide::Runtime::Buffer<> out(in.type(), out_width, out_height, 3);

    auto resize_fn = variants[type_idx][upsample_idx][interpolation_idx];

    double time = Halide::Tools::benchmark(benchmark_iters, benchmark_iters, [&]() { resize_fn(in, scale_factor, out); });
    printf("planar  %8s  %8s  %1.2f  time: %f ms\n",
           interpolation_type.c_str(), input_type.c_str(), scale_factor, time * 1000);

    Halide::Tools::convert_and_save_image(out, outfile);

    if (packed) {
        // Also benchmark a packed memory layout. Don't bother to copy the
        // actual data over, because we won't save the result. We just
        // want to measure the runtime.
        auto in_packed =
            Halide::Runtime::Buffer<>::make_interleaved(in.type(), in.width(), in.height(), in.channels());
        auto out_packed =
            Halide::Runtime::Buffer<>::make_interleaved(out.type(), out.width(), out.height(), out.channels());
        time = Halide::Tools::benchmark(benchmark_iters, benchmark_iters, [&]() { resize_fn(in_packed, scale_factor, out_packed); });
        printf("packed  %8s  %8s  %1.2f  time: %f ms\n",
               interpolation_type.c_str(), input_type.c_str(), scale_factor, time * 1000);
    }

    return 0;
}
