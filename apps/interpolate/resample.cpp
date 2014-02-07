#include "Halide.h"

using namespace Halide;

#include <image_io.h>

#include <iostream>
#include <limits>

#include <sys/time.h>

using std::vector;

double now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    static bool first_call = true;
    static time_t first_sec = 0;
    if (first_call) {
        first_call = false;
        first_sec = tv.tv_sec;
    }
    assert(tv.tv_sec >= first_sec);
    return (tv.tv_sec - first_sec) + (tv.tv_usec / 1000000.0);
}

Expr kernel_linear(Expr x) {
    Expr xx = abs(x);
    return select(xx < 1.0f, xx, 0.0f);
}

Expr kernel_cubic(Expr x) {
    Expr xx = abs(x);
    Expr xx2 = xx * xx;
    Expr xx3 = xx2 * xx;
    float a = -0.5f;

    return select(xx < 1.0f, (a + 2.0f) * xx3 + (a + 3.0f) * xx2 + 1,
                  select (xx < 2.0f, a * xx3 - 5 * a * xx2 + 8 * a * xx - 4.0f * a,
                          0.0f));
}

Expr scaled(Expr x, Expr magnification) {
    return (x + 0.5f) / magnification;
}

enum Interpolation {
    LINEAR, CUBIC
};

int main(int argc, char **argv) {
    std::string infile, outfile;
    Interpolation interpolationType = LINEAR;
    float magnification = 1.0f;
    bool show_usage = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-s" && i+1 < argc) {
            magnification = atof(argv[++i]);
        } else if (arg == "-t" && i+1 < argc) {
            arg = argv[++i];
            if (arg == "linear") {
                interpolationType = LINEAR;
            } else if (arg == "cubic") {
                interpolationType = CUBIC;
            } else {
                fprintf(stderr, "Invalid interpolation type '%s' specified.\n",
                        arg.c_str());
                show_usage = true;
            }
        } else if (infile.empty()) {
            infile = arg;
        } else if (outfile.empty()) {
            outfile = arg;
        } else {
            fprintf(stderr, "Unexpected command line option '%s'.\n", arg.c_str());
        }
    }
    if (infile.empty() || outfile.empty() || show_usage) {
        fprintf(stderr,
                "Usage:\n"
                "\t./resample [-s scalefactor] [-t linear|cubic] in.png out.png\n");
        return 1;
    }

    ImageParam input(Float(32), 3);

    Var x("x"), y("y"), c("c"), k("k");

    Func clamped("clamped");
    clamped(x, y, c) = input(clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1), c);

    // Setup interpolation kernels and helper expressions
    float kernel_size = 0;
    if (interpolationType == LINEAR) {
        kernel_size = 1.0f;
    } else if (interpolationType == CUBIC) {
        kernel_size = 2.0f;
    }
    float kernelScaling = (magnification < 1.0f) ? magnification : 1.0f;
    kernel_size /= kernelScaling;

    Expr scaledx = scaled(x, magnification);
    Expr beginx = cast<int>(scaledx - kernel_size + 0.5f);
    Expr scaledy = scaled(y, magnification);
    Expr beginy = cast<int>(scaledy - kernel_size + 0.5f);

    // Initialize interpolation kernels. Since we allow an arbitrary
    // magnification factor, a different kernel has to be used for each x and
    // y coordinate.
    Func kernel_x, kernel_y;
    Func norm_x, norm_y;
    RDom domx(0, static_cast<int>(2.0f*kernel_size)+1, "domx");
    RDom domy(0, static_cast<int>(2.0f*kernel_size)+1, "domy");
    if (interpolationType == LINEAR) {
        kernel_x(x, k) = kernel_linear((k + beginx - scaledx) * kernelScaling);
        kernel_y(y, k) = kernel_linear((k + beginy - scaledy) * kernelScaling);
    } else if (interpolationType == CUBIC) {
        kernel_x(x, k) = kernel_cubic((k + beginx - scaledx) * kernelScaling);
        kernel_y(y, k) = kernel_cubic((k + beginy - scaledy) * kernelScaling);
    }
    Func norm_kernel_x, norm_kernel_y;
    norm_kernel_x(x, k) = kernel_x(x, k) / sum(kernel_x(x, domx));
    norm_kernel_y(y, k) = kernel_y(y, k) / sum(kernel_y(y, domy));

    // Perform separable upscaling
    Func upsampled_x("upsampled_x");
    Func upsampled_y("upsampled_y");
    upsampled_x(x, y, c) =
        sum(norm_kernel_x(x, domx) * cast<float>(clamped(domx + beginx, y, c)));
    upsampled_y(x, y, c) =
        sum(norm_kernel_y(y, domy) * upsampled_x(x, domy + beginy, c));

    Func final("final");
    final(x, y, c) = clamp(upsampled_y(x, y, c), 0.0f, 1.0f);

    std::cout << "Finished function setup." << std::endl;

    // Scheduling
    norm_kernel_x.compute_root();
    norm_kernel_y.compute_at(final, y);
    upsampled_x.compute_root();

    // final.compute_root();
    Target target = get_jit_target_from_environment();
    final.compile_jit(target);

    Image<float> in_png = load<float>(infile);
    int out_width = in_png.width() * magnification;
    int out_height = in_png.height() * magnification;
    Image<float> out(out_width, out_height, 3);
    input.set(in_png);
    printf("Resampling '%s' from %dx%d to %dx%d using %s interpolation\n",
           infile.c_str(),
           in_png.width(), in_png.height(),
           out_width, out_height,
           (interpolationType == LINEAR) ? "linear" : "cubic");

    double min = std::numeric_limits<double>::infinity();
    const unsigned int iters = 2;

    for (unsigned int x = 0; x < iters; ++x) {
        double before = now();
        final.realize(out);
        double after = now();
        double amt = after - before;

        std::cout << "   " << amt * 1000 << std::endl;
        if (amt < min) min = amt;

    }
    std::cout << " took " << min * 1000 << " msec." << std::endl;

    // vector<Argument> args;
    // args.push_back(input);
    // final.compile_to_assembly("test.s", args, target);

    save(out, outfile);
}
