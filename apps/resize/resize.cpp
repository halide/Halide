#include <iostream>
#include <limits>

#include "HalideBuffer.h"
#include "halide_image_io.h"
#include "halide_benchmark.h"

#include "resize_box_up.h"
#include "resize_cubic_up.h"
#include "resize_linear_up.h"
#include "resize_lanczos_up.h"
#include "resize_box_down.h"
#include "resize_cubic_down.h"
#include "resize_linear_down.h"
#include "resize_lanczos_down.h"

std::string infile, outfile, interpolation_type;
float scale_factor = 1.0f;

void show_usage_and_exit() {
    fprintf(stderr,
            "Usage:\n"
            "\t./resample [-f scalefactor] [-t box|linear|cubic|lanczos] in.png out.png\n");
    exit(1);
}

void parse_commandline(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-f" && i+1 < argc) {
            scale_factor = atof(argv[++i]);
        } else if (arg == "-t" && i+1 < argc) {
            interpolation_type = argv[++i];
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

    printf("Loading '%s'\n", infile.c_str());
    Halide::Runtime::Buffer<float> in = Halide::Tools::load_and_convert_image(infile);
    int out_width = in.width() * scale_factor;
    int out_height = in.height() * scale_factor;
    Halide::Runtime::Buffer<float> out(out_width, out_height, 3);

    printf("Resampling '%s' from %dx%d to %dx%d\n",
           infile.c_str(),
           in.width(), in.height(),
           out_width, out_height);

    auto resize_fn = resize_box_up;
    if (interpolation_type == "box") {
        if (scale_factor > 1.0f) resize_fn = resize_box_up;
        else resize_fn = resize_box_down;
    } else if (interpolation_type == "linear") {
        if (scale_factor > 1.0f) resize_fn = resize_linear_up;
        else resize_fn = resize_linear_down;
    } else if (interpolation_type == "cubic") {
        if (scale_factor > 1.0f) resize_fn = resize_cubic_up;
        else resize_fn = resize_cubic_down;
    } else if (interpolation_type == "lanczos") {
        if (scale_factor > 1.0f) resize_fn = resize_lanczos_up;
        else resize_fn = resize_lanczos_down;
    } else {
        fprintf(stderr, "Unknown interpolation type: %s\n", interpolation_type.c_str());
        show_usage_and_exit();
    }

    double time = Halide::Tools::benchmark(10, 10, [&]() { resize_fn(in, scale_factor, out); });
    printf("Time: %f ms\n", time * 1000);

    Halide::Tools::convert_and_save_image(out, outfile);

    return 0;
}
