#include <iostream>
#include <limits>

#include "HalideBuffer.h"
#include "halide_image_io.h"

#include "auto_viz_demo_complex_down.h"
#include "auto_viz_demo_complex_up.h"
#include "auto_viz_demo_lessnaive_down.h"
#include "auto_viz_demo_lessnaive_up.h"
#include "auto_viz_demo_naive_down.h"
#include "auto_viz_demo_naive_up.h"

std::string infile, outfile, schedule_type;
float scale_factor = 1.0f;
int benchmark_iters = 10;

void show_usage_and_exit() {
    fprintf(stderr,
            "Usage:\n"
            "\t./resample [-f scalefactor] "
            "[-s naive|lessnaive|complex] "
            "in.png out.png\n");
    exit(1);
}

void parse_commandline(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-f" && i + 1 < argc) {
            scale_factor = atof(argv[++i]);
        } else if (arg == "-s" && i + 1 < argc) {
            schedule_type = argv[++i];
        } else if (infile.empty()) {
            infile = arg;
        } else if (outfile.empty()) {
            outfile = arg;
        } else {
            fprintf(stderr, "Unexpected command line option '%s'.\n", arg.c_str());
            show_usage_and_exit();
        }
    }

    if (infile.empty() || outfile.empty() || schedule_type.empty()) {
        show_usage_and_exit();
    }
}

int main(int argc, char **argv) {
    parse_commandline(argc, argv);

    Halide::Runtime::Buffer<float, 3> in = Halide::Tools::load_and_convert_image(infile);
    int out_width = in.width() * scale_factor;
    int out_height = in.height() * scale_factor;
    Halide::Runtime::Buffer<float, 3> out(out_width, out_height, 3);

    decltype(&auto_viz_demo_naive_up) variants[2][3] =
        {
            {&auto_viz_demo_naive_up,
             &auto_viz_demo_lessnaive_up,
             &auto_viz_demo_complex_up},
            {&auto_viz_demo_naive_down,
             &auto_viz_demo_lessnaive_down,
             &auto_viz_demo_complex_up},
        };

    int schedule_idx = 0;
    if (schedule_type == "naive") {
        schedule_idx = 0;
    } else if (schedule_type == "lessnaive") {
        schedule_idx = 1;
    } else if (schedule_type == "complex") {
        schedule_idx = 2;
    } else {
        fprintf(stderr, "Unknown schedule type: %s\n", schedule_type.c_str());
        show_usage_and_exit();
    }

    int upsample_idx = scale_factor > 1.0f ? 0 : 1;

    auto fn = variants[upsample_idx][schedule_idx];

    fn(in, scale_factor, out);

    Halide::Tools::convert_and_save_image(out, outfile);

    return 0;
}
