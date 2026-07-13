// M*//////////////////////////////////////////////////////////////////////////////////////
//
//   IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//   By downloading, copying, installing or using the software you agree to this license.
//   If you do not agree to this license, do not download, install,
//   copy or use the software.
//
//
//                           License Agreement
//                 For Open Source Computer Vision Library
//
//  Copyright (C) 2000, Intel Corporation, all rights reserved.
//  Copyright (C) 2013, OpenCV Foundation, all rights reserved.
//  Third party copyrights are property of their respective owners.
//
//  Redistribution and use in source and binary forms, with or without modification,
//  are permitted provided that the following conditions are met:
//
//    * Redistribution's of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistribution's in binary form must reproduce the above copyright notice,
//      this list of conditions and the following disclaimer in the documentation
//      and/or other materials provided with the distribution.
//
//    * The name of the copyright holders may not be used to endorse or promote products
//      derived from this software without specific prior written permission.
//
//  This software is provided by the copyright holders and contributors "as is" and
//  any express or implied warranties, including, but not limited to, the implied
//  warranties of merchantability and fitness for a particular purpose are disclaimed.
//  In no event shall the Intel Corporation or contributors be liable for any direct,
//  indirect, incidental, special, exemplary, or consequential damages
//  (including, but not limited to, procurement of substitute goods or services;
//  loss of use, data, or profits; or business interruption) however caused
//  and on any theory of liability, whether in contract, strict liability,
//  or tort (including negligence or otherwise) arising in any way out of
//  the use of this software, even if advised of the possibility of such damage.
//
// M*/

// Halide adaptation of https://github.com/opencv/opencv/blob/4.x/modules/calib3d/src/stereobm.cpp

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class StereoBM : public Generator<StereoBM> {
public:
    // Inputs: two grayscale images of the same scene as seen by a left and right camera
    Input<Buffer<uint8_t, 2>> left_gray{"left_gray"};
    Input<Buffer<uint8_t, 2>> right_gray{"right_gray"};

    GeneratorParam<int> winsize{"winsize", 9};                    // size of block surrounding each pixel to compare; must be odd
    GeneratorParam<int> depth{"depth", 16};                       // maximum number of disparities to consider
    GeneratorParam<int> tilesize{"tilesize", 64};                 // strip size
    GeneratorParam<int> threshold{"threshold", 10};               // reject pixels if sum of prefiltered pixels in block is less than this
    GeneratorParam<int> mindisp{"mindisp", 0};                    // minimum disparity to consider.
    GeneratorParam<int> uniqueness_ratio{"uniqueness_ratio", 0};  // reject if the second-best match is too close to the best match, as a percentage of the best match score.
    GeneratorParam<int> filtercap{"filtercap", 31};               // clamp the prefiltering output to the range [0, filtercap*2].
    Output<Buffer<int16_t, 2>> output{"output"};

    void generate() {
        const Type uint16 = UInt(16);
        const Type int16 = Int(16);
        const Type int32 = Int(32);
        const int native_lanes = get_target().natural_vector_size<int16_t>();

        Var y("y");
        Var x("x");
        Var c("c");
        Var di("di");
        Var xi("xi"), xo("xo");

        Expr W = left_gray.dim(0).extent();
        Expr H = left_gray.dim(1).extent();

        Func proc0("proc0"), proc1("proc1");
        // shift the input to ensure the indices of blur_y correspond to the center pixel in the window.
        proc0(x, y) = cast<int16_t>(BoundaryConditions::mirror_interior(left_gray)(x - winsize / 2, y - winsize / 2));
        proc1(x, y) = cast<int16_t>(BoundaryConditions::mirror_interior(right_gray)(x - winsize / 2, y - winsize / 2));

        Func b0("b0"), b1("b1");

        // prefiltering with sobel filter
        Func xsobel0("xsobel0"), xsobel1("xsobel1");
        Expr e0 = proc0(x + 1, y - 1) - proc0(x - 1, y - 1) + 2 * proc0(x + 1, y) - 2 * proc0(x - 1, y) + proc0(x + 1, y + 1) - proc0(x - 1, y + 1);
        Expr e1 = proc1(x + 1, y - 1) - proc1(x - 1, y - 1) + 2 * proc1(x + 1, y) - 2 * proc1(x - 1, y) + proc1(x + 1, y + 1) - proc1(x - 1, y + 1);
        Expr ix = x - winsize / 2, iy = y - winsize / 2;  // image-space coordinates
        Expr border = ix == 0 || ix == W - 1 || ((H % 2 == 1) && iy == H - 1);
        xsobel0(x, y) = select(border, cast<int16_t>(filtercap), cast<int16_t>(clamp(e0, -1 * filtercap, filtercap) + filtercap));
        xsobel1(x, y) = select(border, cast<int16_t>(filtercap), cast<int16_t>(clamp(e1, -1 * filtercap, filtercap) + filtercap));

        Func diff("diff");
        // for each disparity di, sample right image (depth-1-di)+mindisp pixels left of the left image's pixel
        // image is divided into strips indexed by xo that are processed in parallel
        diff(di, xi, y, xo) = Halide::cast<uint16_t>(cast<uint16_t>(abs((xsobel0(xi + xo * tilesize, y)) - (xsobel1(xi + xo * tilesize + di - depth + 1 - mindisp, y)))));

        // compute the sum of absolute differences (SAD) for each pixel and disparity
        Func vsum(uint16, std::string("vsum"));
        Func zero_blur(uint16, "zero_blur");
        RDom rwin(0, winsize, "rwin");
        zero_blur(di, xi, xo) = sum(cast<uint16_t>(diff(di, xi, rwin, xo)));
        vsum(di, xi, y, xo) = select(y <= 0, zero_blur(di, xi, xo), likely(vsum(di, xi, y - 1, xo) + diff(di, xi, y + winsize - 1, xo) - diff(di, xi, y - 1, xo)));
        Func blur_y(uint16, "blur_y");
        RDom rx(0, tilesize, "rx");
        Func f1("f1");
        f1(di, y, xo) = sum(vsum(di, rwin, y, xo));
        blur_y(di, xi, y, xo) = select(xi <= 0, f1(di, y, xo), likely(blur_y(di, xi - 1, y, xo) + vsum(di, xi + winsize - 1, y, xo) - vsum(di, xi - 1, y, xo)));

        // compute the texture value for each pixel, which is the sum of pixels in the surrounding block
        Func text("text"), zerotext("zerotext"), textf1("textf1");
        Func textsum(uint16, "textsum");      // explicit type required for inductive self-reference
        Func textblury(uint16, "textblury");  // explicit type required for inductive self-reference
        text(xi, y, xo) = cast<uint8_t>(abs(cast<int16_t>(xsobel0(xi + xo * tilesize, y)) - cast<int16_t>(filtercap)));
        zerotext(xi, xo) = sum(cast<uint16_t>(text(xi, rwin, xo)));
        textsum(xi, y, xo) = select(y <= 0, zerotext(xi, xo), likely(textsum(xi, y - 1, xo) + text(xi, y + winsize - 1, xo) - text(xi, y - 1, xo)));
        textf1(y, xo) = sum(textsum(rwin, y, xo));
        textblury(xi, y, xo) = select(xi <= 0, textf1(y, xo), likely(textblury(xi - 1, y, xo) + textsum(xi + winsize - 1, y, xo) - textsum(xi - 1, y, xo)));

        // compute the best and second-best disparity for each pixel
        Func preout("preout");
        RDom rd(0, depth, "rd");
        preout(xi, y, xo) = cast<uint16_t>(65535);
        preout(xi, y, xo) = min(blur_y(rd, xi, y, xo), preout(xi, y, xo));
        Func prearg("prearg");
        prearg(di, xi, y, xo) = select(preout(xi, y, xo) == blur_y(di, xi, y, xo), cast<uint16_t>(di), cast<uint16_t>(65535));
        Func argmin1("argmin1");
        argmin1(xi, y, xo) = cast<uint16_t>(65535);
        argmin1(xi, y, xo) = min(argmin1(xi, y, xo), prearg(rd, xi, y, xo));
        Func second_best("second_best");
        second_best(di, xi, y, xo) = select(abs(di - cast<int16_t>(argmin1(xi, y, xo))) <= 1, cast<uint16_t>(65535), blur_y(di, xi, y, xo));
        Func argmin2("argmin2");
        argmin2(xi, y, xo) = cast<uint16_t>(65535);
        argmin2(xi, y, xo) = min(argmin2(xi, y, xo), second_best(rd, xi, y, xo));
        Func p_clamped("p_clamped");
        p_clamped(xi, y, xo) = clamp(argmin1(xi, y, xo), 1, depth - 2);  // indexes into blur_y

        // subpixel refinement
        Func subpout(int16, "subpout");
        Expr p = cast<int32_t>(blur_y(cast(int32, p_clamped(xi, y, xo)) + 1, xi, y, xo));
        Expr n = cast<int32_t>(blur_y(cast(int32, p_clamped(xi, y, xo)) - 1, xi, y, xo));
        Expr d1 = p + n - 2 * preout(xi, y, xo) + abs(p - n);
        Expr q = (abs(p - n) * 256) / d1;
        Expr quot = select(p >= n, q, -q);
        Expr subpout_expr = cast<int16_t>((cast<int16_t>(depth - p_clamped(xi, y, xo) - 1 + mindisp) * 256 + (select(d1 == 0, 0, quot) + 15)) >> 4);
        subpout(xi, y, xo) = select(argmin1(xi, y, xo) > 0 && argmin1(xi, y, xo) < depth - 1, subpout_expr, cast<int16_t>((depth - argmin1(xi, y, xo) - 1 + mindisp) * 16));

        // edge case handling
        Expr filtered = cast<int16_t>((mindisp - 1) * 16);
        Expr reject = textblury(xi, y, xo) < threshold;  // reject if block texture is too uniform
        if (int(uniqueness_ratio) > 0) {                 // reject if second-best disparity is too close to best disparity
            reject = reject || (cast<int32_t>(argmin2(xi, y, xo)) <= cast<int32_t>(preout(xi, y, xo)) + (cast<int32_t>(preout(xi, y, xo)) * cast<int32_t>(uniqueness_ratio)) / 100);
        }
        Func splitoutput("splitoutput");
        splitoutput(xi, y, xo) = select(reject, filtered, subpout(xi, y, xo));

        // disparities are only valid where the SAD window and the full disparity search both fit inside the image.
        Expr sw2 = winsize / 2;
        Expr in_valid_roi = x >= (static_cast<int>(mindisp) + static_cast<int>(depth) - 1) + sw2 && x < W - static_cast<int>(mindisp) - sw2 &&
                            y >= sw2 && y < H - sw2;
        output(x, y) = select(in_valid_roi, splitoutput(x % tilesize, y, x / tilesize), filtered);

        proc0.compute_root().vectorize(x, native_lanes * 4);
        proc1.compute_root().vectorize(x, native_lanes * 4);
        xsobel0.compute_root().vectorize(x, native_lanes * 4);
        xsobel1.compute_root().vectorize(x, native_lanes * 4);

        preout.compute_at(splitoutput, xi).update().atomic(false).vectorize(rd, depth);
        argmin1.compute_at(splitoutput, xi).update().atomic(false).vectorize(rd, depth);
        argmin2.compute_at(splitoutput, xi).update().atomic(false).vectorize(rd, depth);

        subpout.compute_at(splitoutput, xi).vectorize(xi, native_lanes);
        splitoutput.compute_root().reorder_storage(xi, xo, y).parallel(xo).vectorize(xi, native_lanes);

        blur_y.bound(di, 0, depth);
        vsum.bound(di, 0, depth);

        blur_y.compute_at(splitoutput, y).store_at(splitoutput, y).vectorize(di, depth).fold_storage(xi, 1);
        vsum.compute_at(splitoutput, y).store_at(splitoutput, xo).vectorize(di, depth).fold_storage(y, 1);

        f1.compute_at(splitoutput, y).vectorize(di, depth);
        zero_blur.compute_at(splitoutput, xo).vectorize(di, depth);

        zerotext.compute_at(splitoutput, xo).vectorize(xi, native_lanes);
        textsum.compute_at(splitoutput, y).store_at(splitoutput, xo).vectorize(xi).fold_storage(y, 1);
        textf1.compute_at(splitoutput, y);
        textblury.compute_at(splitoutput, y).store_at(splitoutput, y).fold_storage(xi, 1);
        textblury.compute_with(blur_y, xi);

        // Constrain the output origin to 0 so the base-case boundary terms
        // (min(output.min.1, 0), select(output.min.1 <= y, ...)) fold to
        // constants, letting the inductive fold analysis simplify the y
        // footprint of textsum/vsum to a clean single-element step.
        output.dim(0).set_min(0);
        output.dim(1).set_min(0);

        output.vectorize(x, native_lanes);
    }
};

HALIDE_REGISTER_GENERATOR(StereoBM, stereobm)
