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

Expr scaled(Expr x, Expr magnification) {
    return cast<float>(x + 0.5f) / magnification;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage:\n\t./interpolate in.png out.png\n" << std::endl;
        return 1;
    }

    ImageParam input(Float(32), 3);

    Var x("x"), y("y"), c("c");

    Func clamped("clamped");
    clamped(x, y, c) = input(clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1), c);

    float magnification = 1.3f;
    int kernel_size = 2;
    typedef float AccT;


    Expr scaledx = scaled(x, magnification);
    Expr beginx = cast<int>(scaledx - kernel_size/2.0f + 0.5f);
    Expr endx = cast<int>(scaledx + kernel_size/2.0f);
    Func upsampled_x("upsampled_x");

    RDom domx(0, kernel_size+1, "domx");
    RDom domy(0, kernel_size+1, "domy");
    upsampled_x(x, y, c) =
        sum(kernel_linear(domx + beginx - scaledx) * cast<AccT>(clamped(domx + beginx, y, c)));

    Expr scaledy = scaled(y, magnification);
    Expr beginy = cast<int>(scaledy - kernel_size/2.0f + 0.5f);
    Expr endy = cast<int>(scaledy + kernel_size/2.0f);
    Func upsampled_y("upsampled_y");
    upsampled_y(x, y, c) =
        sum(kernel_linear(domy + beginy - scaledy) * upsampled_x(x, domy + beginy, c));

    Func final("final");
    final(x, y, c) = upsampled_y(x, y, c);

    std::cout << "Finished function setup." << std::endl;
    // upsampled_x.compute_at(final, y);


    // final.compute_root();
    Target target = get_jit_target_from_environment();
    final.compile_jit(target);

    Image<float> in_png = load<float>(argv[1]);
    Image<float> out(in_png.width() * magnification,
                     in_png.height() * magnification, 3);
    input.set(in_png);
    final.realize(out);

    vector<Argument> args;
    args.push_back(input);
    final.compile_to_assembly("test.s", args, target);

    save(out, argv[2]);
}
