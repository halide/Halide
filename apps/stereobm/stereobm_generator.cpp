#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

Var x, y, c;

Func stereobm_func(Func input0, Func input1, int winsize, int depth, int tilesize, int ftzero, int threshold, int mindisp, int uniqueness_ratio, int filtercap, Target target) {
    const Type int8 = Int(8);
    const Type uint16 = UInt(16);
    const Type int32 = Int(32);
    const int native_lanes = target.natural_vector_size<int16_t>();

    Var x("x");
    Var y("y");
    Var c("c");
    Var d("d");
    Var di("di");
    Var xi("xi"), yi("yi"), xo("xo"), yo("yo");

    Func proc0("proc0"), proc1("proc1");
    proc0(y, x, c) = BoundaryConditions::constant_exterior(input0, 0)(y, x, c);
    proc1(y, x, c) = BoundaryConditions::constant_exterior(input1, 0)(y, x, c);

    Func b0("b0"), b1("b1");
    // convert to grayscale
    b0(y, x) = cast<int16_t>(cast<uint8_t>((cast<uint32_t>(proc0(y, x, 0)) * 1868 + cast<uint32_t>(proc0(y, x, 1)) * 9617 + cast<uint32_t>(proc0(y, x, 2)) * 4899 + 8192) >> 14));
    b1(y, x) = cast<int16_t>(cast<uint8_t>((cast<uint32_t>(proc1(y, x, 0)) * 1868 + cast<uint32_t>(proc1(y, x, 1)) * 9617 + cast<uint32_t>(proc1(y, x, 2)) * 4899 + 8192) >> 14));

    // prefiltering with sobel filter
    Func xsobel0("xsobel0"), xsobel1("xsobel1");
    Expr e0 = b0(y + 1, x - 1) - b0(y - 1, x - 1) + 2 * b0(y + 1, x) - 2 * b0(y - 1, x) + b0(y + 1, x + 1) - b0(y - 1, x + 1);
    Expr e1 = b1(y + 1, x - 1) - b1(y - 1, x - 1) + 2 * b1(y + 1, x) - 2 * b1(y - 1, x) + b1(y + 1, x + 1) - b1(y - 1, x + 1);
    xsobel0(y, x) = cast<int16_t>(clamp(e0, -filtercap, filtercap) + filtercap);
    xsobel1(y, x) = cast<int16_t>(clamp(e1, -filtercap, filtercap) + filtercap);

    Func diff("diff");
    diff(di, yi, x, yo) = Halide::cast<uint16_t>(cast<uint16_t>(abs((xsobel0(yi + yo * tilesize, x)) - (xsobel1(yi + yo * tilesize + di - depth + 1, x)))));

    Func vsum(uint16, std::string("vsum"));
    Func zero_blur(uint16, "zero_blur");
    RDom rx0(0, winsize);
    zero_blur(di, yi, yo) = sum(cast<uint16_t>(diff(di, yi, rx0, yo)));
    vsum(di, yi, x, yo) = select(x <= 0, zero_blur(di, yi, yo), likely(vsum(di, yi, max(x - 1, 0), yo) + diff(di, yi, x + winsize - 1, yo) - diff(di, yi, max(x - 1, 0), yo)));  /// should be x-1
    Func blur_y(uint16, 1, "blur_y");
    RDom ry(0, tilesize, "ry");
    RDom ry1(1, depth - 1, "ry1");
    Func f1("f1");
    f1(di, x, yo) = sum(vsum(di, rx0, x, yo));
    blur_y(di, yi, x, yo) = select(yi <= 0, f1(di, x, yo), likely(blur_y(di, max(yi - 1, 0), x, yo) + vsum(di, yi + winsize - 1, x, yo) - vsum(di, max(yi - 1, 0), x, yo)));

    Func text("text"), zerotext("zerotext"), textsum("textsum"), textf1("textf1"), textblury("textblury");
    text(yi, x, yo) = cast<uint8_t>(abs(cast<int8_t>(b0(yi + yo * tilesize, x)) - cast<int8_t>(ftzero)));
    zerotext(yi, yo) = sum(cast<uint16_t>(text(yi, rx0, yo)));
    textsum(yi, x, yo) = select(x <= 0, zerotext(yi, yo), textsum(yi, x - 1, yo) + text(yi, x + winsize - 1, yo) - text(yi, x - 1, yo));
    textf1(x, yo) = sum(textsum(rx0, x, yo));
    textblury(yi, x, yo) = undef<uint16_t>();
    textblury(ry, x, yo) = select(ry == 0, textf1(x, yo), textblury(max(ry - 1, 0), x, yo) + textsum(ry + winsize - 1, x, yo) - textsum(max(ry - 1, 0), x, yo));

    Func preout("preout");
    RDom rd(0, depth, "rd");
    preout(yi, x, yo) = cast<uint16_t>(65535);
    preout(yi, x, yo) = min(blur_y(rd, yi, x, yo), preout(yi, x, yo));

    Func prearg("prearg");
    prearg(di, yi, x, yo) = select(preout(yi, x, yo) == blur_y(di, yi, x, yo), cast<uint16_t>(di), cast<uint16_t>(65535));
    Func argmin1("argmin");
    argmin1(yi, x, yo) = cast<uint16_t>(65535);
    argmin1(yi, x, yo) = min(argmin1(yi, x, yo), prearg(rd, yi, x, yo));

    Func second_best("second_best");
    second_best(di, yi, x, yo) = select(abs(di - cast<int16_t>(argmin1(yi, x, yo))) <= 1, cast<uint16_t>(65535), blur_y(di, yi, x, yo));
    Func argmin2("argmin2");
    argmin2(yi, x, yo) = cast<uint16_t>(65535);
    argmin2(yi, x, yo) = min(argmin2(yi, x, yo), second_best(rd, yi, x, yo));

    Func p_clamped("p_clamped");
    p_clamped(yi, x, yo) = unsafe_promise_clamped(argmin1(yi, x, yo), 1, depth - 2);

    Func subpout(int32, "subpout");
    Expr p = cast<int32_t>(blur_y(cast(int32, p_clamped(yi, x, yo)) + 1, yi, x, yo));
    Expr n = cast<int32_t>(blur_y(cast(int32, p_clamped(yi, x, yo)) - 1, yi, x, yo));
    Expr d1 = p + n - 2 * preout(yi, x, yo) + abs(p - n);
    Expr subpout_expr = cast<int32_t>((cast<int16_t>(depth - p_clamped(yi, x, yo) - 1 + mindisp) * 256 + (select(d1 == 0, 0, (p - n) * 256 / d1) + 15)) >> 4);
    subpout(yi, x, yo) = select(argmin1(yi, x, yo) > 0 && argmin1(yi, x, yo) < depth - 1, subpout_expr, cast<int32_t>(depth - argmin1(yi, x, yo) - 1 + mindisp) * 16);

    Func splitoutput("splitoutput"), output("output");
    splitoutput(yi, x, yo) = select((textblury(yi, x, yo) < threshold || cast<int32_t>(argmin2(yi, x, yo)) <= cast<int32_t>(preout(yi, x, yo)) + (cast<int32_t>(preout(yi, x, yo)) * cast<int32_t>(uniqueness_ratio)) / 100), 0, subpout(yi, x, yo));
    output(y, x) = splitoutput(y % tilesize, x, y / tilesize);

    b0.compute_root().vectorize(y, native_lanes * 4);
    b1.compute_root().vectorize(y, native_lanes * 4);
    xsobel0.compute_root().vectorize(y, native_lanes * 4);
    xsobel1.compute_root().vectorize(y, native_lanes * 4);

    preout.compute_at(splitoutput, yi).update().atomic(false).vectorize(rd, depth);
    argmin1.compute_at(splitoutput, yi).update().atomic(false).vectorize(rd, depth);
    argmin2.compute_at(splitoutput, yi).update().atomic(false).vectorize(rd, depth);

    splitoutput.compute_root().reorder_storage(yi, yo, x).parallel(yo);

    blur_y.bound(di, 0, depth);
    vsum.bound(di, 0, depth);

    blur_y.compute_at(splitoutput, x).store_at(splitoutput, x).vectorize(di, depth).fold_storage(yi, 2);
    vsum.compute_at(splitoutput, x).store_at(splitoutput, yo).vectorize(di, depth).fold_storage(x, 2);

    f1.compute_at(splitoutput, x).vectorize(di, depth);
    zero_blur.compute_at(splitoutput, yo).vectorize(di, depth);

    zerotext.compute_at(splitoutput, yo);
    textf1.compute_at(splitoutput, x);
    textsum.compute_at(splitoutput, x).store_at(splitoutput, yo).fold_storage(x, 2);
    textblury.compute_at(splitoutput, x);
    // subpout.compute_at(splitoutput, x);

    output.vectorize(y, native_lanes);

    return output;
}

class StereoBM : public Generator<StereoBM> {
public:
    // Inputs: two images of the same scene as seen by a left and right eye
    Input<Buffer<uint8_t, 3>> left_color{"left_color"};
    Input<Buffer<uint8_t, 3>> right_color{"right_color"};

    GeneratorParam<int> winsize{"winsize", 15};  // Size of box surrounding each to sum diffs over
    GeneratorParam<int> depth{"depth", 64};      // range of disparities to test
    GeneratorParam<int> tilesize{"tilesize", 64};
    GeneratorParam<int> ftzero{"ftzero", 0};
    GeneratorParam<int> threshold{"threshold", 10};
    GeneratorParam<int> mindisp{"mindisp", 0};
    GeneratorParam<int> uniqueness_ratio{"uniqueness_ratio", 15};
    GeneratorParam<int> filtercap{"filtercap", 31};

    Output<Buffer<int32_t, 2>> output{"output"};

    void generate() {
        Func stereo = stereobm_func(left_color, right_color, winsize, depth, tilesize, ftzero, threshold, mindisp, uniqueness_ratio, filtercap, get_target());
        output = stereo;
    }
};

HALIDE_REGISTER_GENERATOR(StereoBM, stereobm)
