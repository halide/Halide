/*
    Adapted (with permission) from https://github.com/timothybrooks/hdr-plus
*/

#include "util.h"

#include "Halide.h"
#include <vector>
#include <algorithm>

using namespace Halide;
using namespace Halide::ConciseCasts;

/*
 * box_down2 -- averages 2x2 regions of an image to downsample linearly.
 */
Func box_down2(Func input, std::string name, bool skip_schedule) {

    Func output(name);

    Var x, y, n;
    RDom r(0, 2, 0, 2);

    // output with box filter and stride 2

    output(x, y, n) = u16(sum(u32(input(2*x + r.x, 2*y + r.y, n))) / 4);

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        output.compute_root().parallel(y).vectorize(x, 16);
    }

    return output;
}

/*
 * gauss_down4 -- applies a 3x3 integer gauss kernel and downsamples an image by 4 in
 * one step.
 */
Func gauss_down4(Func input, std::string name, bool skip_schedule) {

    Func output(name);
    Buffer<uint32_t> k(5, 5, "gauss_down4_kernel");
    k.translate({-2, -2});

    Var x, y, n;
    RDom r(-2, 5, -2, 5);

    // gaussian kernel

    k.fill(0);
    k(-2,-2) = 2; k(-1,-2) =  4; k(0,-2) =  5; k(1,-2) =  4; k(2,-2) = 2;
    k(-2,-1) = 4; k(-1,-1) =  9; k(0,-1) = 12; k(1,-1) =  9; k(2,-1) = 4;
    k(-2, 0) = 5; k(-1, 0) = 12; k(0, 0) = 15; k(1, 0) = 12; k(2, 0) = 5;
    k(-2, 1) = 4; k(-1, 1) =  9; k(0, 1) = 12; k(1, 1) =  9; k(2, 1) = 4;
    k(-2, 2) = 2; k(-1, 2) =  4; k(0, 2) =  5; k(1, 2) =  4; k(2, 2) = 2;

    // output with applied kernel and stride 4

    output(x, y, n) = u16(sum(u32(input(4*x + r.x, 4*y + r.y, n) * k(r.x, r.y))) / 159);

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        output.compute_root().parallel(y).vectorize(x, 16);
    }

    return output;
}

/*
 * gauss_7x7 -- Applies a 7x7 gauss kernel with a std deviation of 4/3. Requires its input to handle boundaries.
 */
Func gauss(Func input, Buffer<float> k, RDom r, std::string name, bool skip_schedule) {

    Func blur_x(name + "_x");
    Func output(name);

    Var x, y, c;

    Expr val;

    if (input.dimensions() == 2) {

        blur_x(x, y) = sum(input(x + r, y) * k(r));

        val = sum(blur_x(x, y + r) * k(r));

        if (input.output_types()[0] == UInt(16)) val = u16(val);

        output(x, y) = val;
    }
    else {

        blur_x(x, y, c) = sum(input(x + r, y, c) * k(r));

        val = sum(blur_x(x, y + r, c) * k(r));

        if (input.output_types()[0] == UInt(16)) val = u16(val);

        output (x, y, c) = val;
    }

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        Var xi, yi;
        blur_x.compute_at(output, x).vectorize(x, 16);
        output.compute_root().tile(x, y, xi, yi, 256, 128).vectorize(xi, 16).parallel(y);
    }

    return output;
}

Func gauss_7x7(Func input, std::string name, bool skip_schedule) {

    // gaussian kernel

    Buffer<float> k(7, "gauss_7x7_kernel");
    k.translate({-3});

    Var x;
    RDom r(-3, 7);

    k.fill(0.f);
    k(-3) = 0.026267f; k(-2) = 0.100742f; k(-1) = 0.225511f; k(0) = 0.29496f;
    k( 3) = 0.026267f; k( 2) = 0.100742f; k( 1) = 0.225511f;

    return gauss(input, k, r, name, skip_schedule);

}

Func gauss_15x15(Func input, std::string name, bool skip_schedule) {

    // gaussian kernel

    Buffer<float> k(15, "gauss_15x15");
    k.translate({-7});

    Var x;
    RDom r(-7, 15);

    k.fill(0.f);
    k(-7) = 0.004961f; k(-6) = 0.012246f; k(-5) = 0.026304f; k(-4) = 0.049165f; k(-3) = 0.079968f; k(-2) = 0.113193f; k(-1) = 0.139431f; k(0) = 0.149464f;
    k( 7) = 0.004961f; k( 6) = 0.012246f; k( 5) = 0.026304f; k( 4) = 0.049165f; k( 3) = 0.079968f; k( 2) = 0.113193f; k( 1) = 0.139431f;

    return gauss(input, k, r, name, skip_schedule);
}

/*
 * diff -- Computes difference between two integer functions
 */
Func diff(Func im1, Func im2, std::string name, bool skip_schedule) {

    Func output(name);

    Var x, y, c;

    if (im1.dimensions() == 2) {
        output(x,y) = i32(im1(x,y)) - i32(im2(x,y));
    } else {
        output(x,y,c) = i32(im1(x,y,c)) - i32(im2(x,y,c));
    }

    return output;
}

/*
 * gamma_correct -- Takes a single or multi-channel linear image and applies gamma correction
 * as described here: http://www.color.org/sRGB.xalter. See formulas 1.2a and 1.2b
 */
Func gamma_correct(Func input, bool skip_schedule) {

    Func output("gamma_correct_output");

    Var x, y, c;

    // constants for gamma correction

    int cutoff = 200;                   // ceil(0.00304 * UINT16_MAX)
    float gamma_toe = 12.92;
    float gamma_pow = 0.416667;         // 1 / 2.4
    float gamma_fac = 680.552897;       // 1.055 * UINT16_MAX ^ (1 - gamma_pow);
    float gamma_con = -3604.425;        // -0.055 * UINT16_MAX

    if (input.dimensions() == 2) {
        output(x, y) = u16(select(input(x, y) < cutoff,
                            gamma_toe * input(x, y),
                            gamma_fac * pow(input(x, y), gamma_pow) + gamma_con));
    }
    else {
        output(x, y, c) = u16(select(input(x, y, c) < cutoff,
                            gamma_toe * input(x, y, c),
                            gamma_fac * pow(input(x, y, c), gamma_pow) + gamma_con));
    }

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        output.compute_root().parallel(y).vectorize(x, 16);
    }

    return output;
}

/*
 * gamma_inverse -- Takes a single or multi-channel image and undoes gamma correction to
 * return in to linear RGB space.
 */
Func gamma_inverse(Func input, bool skip_schedule) {

    Func output("gamma_inverse_output");

    Var x, y, c;

    // constants for inverse gamma correction

    int cutoff = 2575;                   // ceil(1/0.00304 * UINT16_MAX)
    float gamma_toe = 0.0774;            // 1 / 12.92
    float gamma_pow = 2.4;
    float gamma_fac = 57632.49226;       // 1 / 1.055 ^ gamma_pow * U_INT16_MAX;
    float gamma_con = 0.055;

    if (input.dimensions() == 2) {
        output(x, y) = u16(select(input(x, y) < cutoff,
                            gamma_toe * input(x, y),
                            pow(f32(input(x, y)) / 65535.f + gamma_con, gamma_pow) * gamma_fac));
    }
    else {
        output(x, y, c) = u16(select(input(x, y, c) < cutoff,
                            gamma_toe * input(x, y),
                            pow(f32(input(x, y, c)) / 65535.f + gamma_con, gamma_pow) * gamma_fac));
    }

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        output.compute_root().parallel(y).vectorize(x, 16);
    }

    return output;
}

/*
 * rgb_to_yuv -- converts a linear rgb image to a linear yuv image. Note that the output
 * is in float32
 */
Func rgb_to_yuv(Func input, bool skip_schedule) {

    Func output("rgb_to_yuv_output");

    Var x, y, c;

    Expr r = input(x, y, 0);
    Expr g = input(x, y, 1);
    Expr b = input(x, y, 2);

    output(x, y, c) = f32(0);

    output(x, y, 0) =  0.298900f * r + 0.587000f * g + 0.114000f * b;           // Y
    output(x, y, 1) = -0.168935f * r - 0.331655f * g + 0.500590f * b;           // U
    output(x, y, 2) =  0.499813f * r - 0.418531f * g - 0.081282f * b;           // V

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        output.compute_root().parallel(y).vectorize(x, 16);
        output.update(0).parallel(y).vectorize(x, 16);
        output.update(1).parallel(y).vectorize(x, 16);
        output.update(2).parallel(y).vectorize(x, 16);
    }

    return output;
}

/*
 * yuv_to_rgb -- Converts a linear yuv image to a linear rgb image.
 */
Func yuv_to_rgb(Func input, bool skip_schedule) {

    Func output("yuv_to_rgb_output");

    Var x, y, c;

    Expr Y = input(x, y, 0);
    Expr U = input(x, y, 1);
    Expr V = input(x, y, 2);

    output(x, y, c) = u16(0);

    output(x, y, 0) = u16_sat(Y + 1.403f * V            );          // r
    output(x, y, 1) = u16_sat(Y -  .344f * U - .714f * V);          // g
    output(x, y, 2) = u16_sat(Y + 1.770f * U            );          // b

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        output.compute_root().parallel(y).vectorize(x, 16);
        output.update(0).parallel(y).vectorize(x, 16);
        output.update(1).parallel(y).vectorize(x, 16);
        output.update(2).parallel(y).vectorize(x, 16);
    }

    return output;
}
