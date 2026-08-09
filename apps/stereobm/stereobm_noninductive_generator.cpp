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
// Uses "Schedule 4" from https://commit.csail.mit.edu/papers/2016/min-zhang-meng-thesis.pdf

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class StereoBMSchedule4 : public Generator<StereoBMSchedule4> {
public:
    Input<Buffer<uint8_t, 2>> left_gray{"left_gray"};
    Input<Buffer<uint8_t, 2>> right_gray{"right_gray"};

    GeneratorParam<int> winsize{"winsize", 9};
    GeneratorParam<int> depth{"depth", 16};
    GeneratorParam<int> tilesize{"tilesize", 64};         // x tile
    GeneratorParam<int> ytilesize{"ytilesize", 64};       // y tile
    GeneratorParam<int> vector_width{"vector_width", 0};  // disparity vector width; 0 => natural_vector_size
    GeneratorParam<int> threshold{"threshold", 10};
    GeneratorParam<int> mindisp{"mindisp", 0};
    GeneratorParam<int> uniqueness_ratio{"uniqueness_ratio", 0};
    GeneratorParam<int> filtercap{"filtercap", 31};
    Output<Buffer<int16_t, 2>> output{"output"};

    void generate() {
        const Type uint16 = UInt(16);
        const Type int16 = Int(16);
        const Type int32 = Int(32);
        const int native_lanes = get_target().natural_vector_size<int16_t>();

        const int T = tilesize;
        const int N = ytilesize;
        int VW = int(vector_width) > 0 ? int(vector_width) : native_lanes;
        if (int(depth) % VW != 0) VW = depth;  // require VW | depth; fall back to a single vector
        const int DO = int(depth) / VW;

        Var x("x"), y("y");
        Var di("di"), d_o("d_o");
        Var xi("xi"), xo("xo"), yi("yi"), yo("yo");

        Expr W = left_gray.dim(0).extent();
        Expr H = left_gray.dim(1).extent();

        Func proc0("proc0"), proc1("proc1");
        proc0(x, y) = cast<int16_t>(BoundaryConditions::mirror_interior(left_gray)(x - winsize / 2, y - winsize / 2));
        proc1(x, y) = cast<int16_t>(BoundaryConditions::mirror_interior(right_gray)(x - winsize / 2, y - winsize / 2));

        Func xsobel0("xsobel0"), xsobel1("xsobel1");
        Expr e0 = proc0(x + 1, y - 1) - proc0(x - 1, y - 1) + 2 * proc0(x + 1, y) - 2 * proc0(x - 1, y) + proc0(x + 1, y + 1) - proc0(x - 1, y + 1);
        Expr e1 = proc1(x + 1, y - 1) - proc1(x - 1, y - 1) + 2 * proc1(x + 1, y) - 2 * proc1(x - 1, y) + proc1(x + 1, y + 1) - proc1(x - 1, y + 1);
        Expr ix = x - winsize / 2, iy = y - winsize / 2;
        Expr border = ix == 0 || ix == W - 1 || ((H % 2 == 1) && iy == H - 1);
        xsobel0(x, y) = select(border, cast<int16_t>(filtercap), cast<int16_t>(clamp(e0, -1 * filtercap, filtercap) + filtercap));
        xsobel1(x, y) = select(border, cast<int16_t>(filtercap), cast<int16_t>(clamp(e1, -1 * filtercap, filtercap) + filtercap));

        Expr ax = xi + xo * T;
        Expr ay = yi + yo * N;

        Func diff("diff");
        Var d("d");
        diff(d, xi, yi, xo, yo) = cast<uint16_t>(abs(xsobel0(ax, ay) - xsobel1(ax + d - depth + 1 - mindisp, ay)));

        RDom rwx(0, winsize, "rwx");  // horizontal window (cSAD base col)
        RDom rwy(0, winsize, "rwy");  // vertical window (vsum base row)
        RDom ryi(1, N - 1, "ryi");    // vertical scan over tile rows
        RDom rxi(1, T - 1, "rxi");    // horizontal scan over tile cols

        Expr d_full = d_o * VW + di;

        Func vsum(uint16, "vsum");
        vsum(di, xi, yi, xo, yo, d_o) = undef<uint16_t>();
        vsum(di, xi, 0, xo, yo, d_o) = sum(diff(d_full, xi, rwy, xo, yo));
        vsum(di, xi, ryi, xo, yo, d_o) = cast<uint16_t>(vsum(di, xi, ryi - 1, xo, yo, d_o) +
                                                        diff(d_full, xi, ryi + winsize - 1, xo, yo) -
                                                        diff(d_full, xi, ryi - 1, xo, yo));

        Func cSAD(uint16, "cSAD");
        cSAD(di, xi, yi, xo, yo, d_o) = undef<uint16_t>();
        cSAD(di, 0, yi, xo, yo, d_o) = sum(vsum(di, rwx, yi, xo, yo, d_o));
        cSAD(di, rxi, yi, xo, yo, d_o) = cast<uint16_t>(cSAD(di, rxi - 1, yi, xo, yo, d_o) +
                                                        vsum(di, rxi + winsize - 1, yi, xo, yo, d_o) -
                                                        vsum(di, rxi - 1, yi, xo, yo, d_o));

        auto sad = [&](Expr d_expr, Expr X, Expr Y) {
            return cSAD(d_expr % VW, X, Y, xo, yo, d_expr / VW);
        };

        Func text("text");
        Func textcol(uint16, "textcol");
        Func textSAD(uint16, "textSAD");
        text(xi, yi, xo, yo) = cast<uint8_t>(abs(cast<int16_t>(xsobel0(ax, ay)) - cast<int16_t>(filtercap)));
        textcol(xi, yi, xo, yo) = sum(cast<uint16_t>(text(xi + rwx, yi, xo, yo)));
        textSAD(xi, yi, xo, yo) = undef<uint16_t>();
        textSAD(xi, 0, xo, yo) = sum(textcol(xi, rwy, xo, yo));
        textSAD(xi, ryi, xo, yo) = cast<uint16_t>(textSAD(xi, ryi - 1, xo, yo) +
                                                  textcol(xi, ryi + winsize - 1, xo, yo) -
                                                  textcol(xi, ryi - 1, xo, yo));

        RDom rd(0, VW, 0, DO, "rd");
        Expr rd_full = rd[1] * VW + rd[0];

        Func preout("preout");
        preout(xi, yi, xo, yo) = cast<uint16_t>(65535);
        preout(xi, yi, xo, yo) = min(cSAD(rd[0], xi, yi, xo, yo, rd[1]), preout(xi, yi, xo, yo));

        Func prearg("prearg");
        prearg(di, xi, yi, xo, yo, d_o) = select(preout(xi, yi, xo, yo) == cSAD(di, xi, yi, xo, yo, d_o), cast<uint16_t>(d_full), cast<uint16_t>(65535));
        Func argmin1("argmin1");
        argmin1(xi, yi, xo, yo) = cast<uint16_t>(65535);
        argmin1(xi, yi, xo, yo) = min(argmin1(xi, yi, xo, yo), prearg(rd[0], xi, yi, xo, yo, rd[1]));

        Func second_best("second_best");
        second_best(di, xi, yi, xo, yo, d_o) = select(abs(cast<int16_t>(d_full) - cast<int16_t>(argmin1(xi, yi, xo, yo))) <= 1, cast<uint16_t>(65535), cSAD(di, xi, yi, xo, yo, d_o));
        Func argmin2("argmin2");
        argmin2(xi, yi, xo, yo) = cast<uint16_t>(65535);
        argmin2(xi, yi, xo, yo) = min(argmin2(xi, yi, xo, yo), second_best(rd[0], xi, yi, xo, yo, rd[1]));

        Func p_clamped("p_clamped");
        p_clamped(xi, yi, xo, yo) = clamp(argmin1(xi, yi, xo, yo), 1, depth - 2);

        // subpixel refinement
        Func subpout(int16, "subpout");
        Expr pc = cast<int32_t>(p_clamped(xi, yi, xo, yo));
        Expr p = cast<int32_t>(sad(pc + 1, xi, yi));
        Expr n = cast<int32_t>(sad(pc - 1, xi, yi));
        Expr d1 = p + n - 2 * preout(xi, yi, xo, yo) + abs(p - n);
        Expr q = (abs(p - n) * 256) / d1;
        Expr quot = select(p >= n, q, -q);
        Expr subpout_expr = cast<int16_t>((cast<int16_t>(depth - p_clamped(xi, yi, xo, yo) - 1 + mindisp) * 256 + (select(d1 == 0, 0, quot) + 15)) >> 4);
        subpout(xi, yi, xo, yo) = select(argmin1(xi, yi, xo, yo) > 0 && argmin1(xi, yi, xo, yo) < depth - 1, subpout_expr, cast<int16_t>((depth - argmin1(xi, yi, xo, yo) - 1 + mindisp) * 16));

        // edge case handling
        Expr filtered = cast<int16_t>((mindisp - 1) * 16);
        Expr reject = textSAD(xi, yi, xo, yo) < threshold;
        if (int(uniqueness_ratio) > 0) {
            reject = reject || (cast<int32_t>(argmin2(xi, yi, xo, yo)) <= cast<int32_t>(preout(xi, yi, xo, yo)) + (cast<int32_t>(preout(xi, yi, xo, yo)) * cast<int32_t>(uniqueness_ratio)) / 100);
        }
        Func disp_left("disp_left");
        disp_left(xi, yi, xo, yo) = select(reject, filtered, subpout(xi, yi, xo, yo));

        Expr sw2 = winsize / 2;
        Expr in_valid_roi = x >= (static_cast<int>(mindisp) + static_cast<int>(depth) - 1) + sw2 && x < W - static_cast<int>(mindisp) - sw2 &&
                            y >= sw2 && y < H - sw2;
        output(x, y) = select(in_valid_roi, disp_left(x % T, y % N, x / T, y / N), filtered);

        proc0.compute_root().vectorize(x, native_lanes * 4);
        proc1.compute_root().vectorize(x, native_lanes * 4);
        xsobel0.compute_root().vectorize(x, native_lanes * 4);
        xsobel1.compute_root().vectorize(x, native_lanes * 4);

        disp_left.compute_root().reorder_storage(xi, yi, xo, yo);
        disp_left.reorder(xi, yi, xo, yo)
            .parallel(yo)
            .parallel(xo)
            .vectorize(xi, native_lanes);

        preout.compute_at(disp_left, xi).update().atomic(false).reorder(rd[0], xi, yi, rd[1]).vectorize(rd[0], VW);
        argmin1.compute_at(disp_left, xi).update().atomic(false).reorder(rd[0], xi, yi, rd[1]).vectorize(rd[0], VW);
        argmin2.compute_at(disp_left, xi).update().atomic(false).reorder(rd[0], xi, yi, rd[1]).vectorize(rd[0], VW);
        subpout.compute_at(disp_left, xi).vectorize(xi, native_lanes);

        cSAD.compute_at(disp_left, xo).reorder_storage(di, xi, yi, xo, yo, d_o);
        cSAD.reorder(di, xi, yi, d_o).vectorize(di, VW);
        cSAD.update(0).reorder(di, yi, d_o).vectorize(di, VW);
        cSAD.update(1).reorder(di, rxi, yi, d_o).vectorize(di, VW);

        vsum.compute_at(disp_left, xo).reorder_storage(di, xi, yi, xo, yo, d_o);
        vsum.reorder(di, xi, yi, d_o).vectorize(di, VW);
        vsum.update(0).reorder(di, xi, d_o).vectorize(di, VW);
        vsum.update(1).reorder(di, xi, ryi, d_o).vectorize(di, VW);

        textSAD.compute_at(disp_left, xo).vectorize(xi, native_lanes);
        textSAD.update(0).vectorize(xi, native_lanes);
        textSAD.update(1).vectorize(xi, native_lanes);
        textcol.compute_at(disp_left, xo).vectorize(xi, native_lanes);

        output.dim(0).set_min(0);
        output.dim(1).set_min(0);
        output.vectorize(x, native_lanes);
    }
};

HALIDE_REGISTER_GENERATOR(StereoBMSchedule4, stereobm)
