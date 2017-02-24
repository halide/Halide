#include "Halide.h"
#include <stdint.h>

namespace {

using namespace Halide;

using Scheduler = std::function<void(Func processed)>;

class CameraPipe : public Halide::Generator<CameraPipe> {
public:
    // Parameterized output type, because LLVM PTX (GPU) backend does not
    // currently allow 8-bit computations
    GeneratorParam<Type> result_type{"result_type", UInt(8)};

    ImageParam input{UInt(16), 2, "input"};
    ImageParam matrix_3200{Float(32), 2, "matrix_3200"};
    ImageParam matrix_7000{Float(32), 2, "matrix_7000"};
    Param<float> color_temp{"color_temp"};
    Param<float> gamma{"gamma"};
    Param<float> contrast{"contrast"};
    Param<int> blackLevel{"blackLevel"};
    Param<int> whiteLevel{"whiteLevel"};

    Func build();

private:
    Expr avg(Expr a, Expr b);
    Func hot_pixel_suppression(Func input);
    Func interleave_x(Func a, Func b);
    Func interleave_y(Func a, Func b);
    Func deinterleave(Func raw);
    std::pair<Func, Scheduler> demosaic(Func deinterleaved);
    Func apply_curve(Func input);
    Func color_correct(Func input);

    Var x, y, c, yi, yo, yii, xi;
};

// Average two positive values rounding up
Expr CameraPipe::avg(Expr a, Expr b) {
    Type wider = a.type().with_bits(a.type().bits() * 2);
    return cast(a.type(), (cast(wider, a) + b + 1)/2);
}

Func CameraPipe::hot_pixel_suppression(Func input) {

    Expr a = max(max(input(x-2, y), input(x+2, y)),
                 max(input(x, y-2), input(x, y+2)));

    Func denoised("denoised");
    denoised(x, y) = clamp(input(x, y), 0, a);

    return denoised;
}

Func CameraPipe::interleave_x(Func a, Func b) {
    Func out;
    out(x, y) = select((x%2)==0, a(x/2, y), b(x/2, y));
    return out;
}

Func CameraPipe::interleave_y(Func a, Func b) {
    Func out;
    out(x, y) = select((y%2)==0, a(x, y/2), b(x, y/2));
    return out;
}

Func CameraPipe::deinterleave(Func raw) {
    // Deinterleave the color channels
    Func deinterleaved("deinterleaved");

    deinterleaved(x, y, c) = select(c == 0, raw(2*x, 2*y),
                                    c == 1, raw(2*x+1, 2*y),
                                    c == 2, raw(2*x, 2*y+1),
                                            raw(2*x+1, 2*y+1));
    return deinterleaved;
}

std::pair<Func, Scheduler> CameraPipe::demosaic(Func deinterleaved) {
    // These are the values we already know from the input
    // x_y = the value of channel x at a site in the input of channel y
    // gb refers to green sites in the blue rows
    // gr refers to green sites in the red rows

    // Give more convenient names to the four channels we know
    Func r_r("r_r"), g_gr("g_gr"), g_gb("g_gb"), b_b("b_b");
    g_gr(x, y) = deinterleaved(x, y, 0);
    r_r(x, y)  = deinterleaved(x, y, 1);
    b_b(x, y)  = deinterleaved(x, y, 2);
    g_gb(x, y) = deinterleaved(x, y, 3);

    // These are the ones we need to interpolate
    Func b_r("b_r"), g_r("g_r"), b_gr("b_gr"), r_gr("r_gr");
    Func b_gb("b_gb"), r_gb("r_gb"), r_b("r_b"), g_b("g_b");

    // First calculate green at the red and blue sites

    // Try interpolating vertically and horizontally. Also compute
    // differences vertically and horizontally. Use interpolation in
    // whichever direction had the smallest difference.
    Expr gv_r  = avg(g_gb(x, y-1), g_gb(x, y));
    Expr gvd_r = absd(g_gb(x, y-1), g_gb(x, y));
    Expr gh_r  = avg(g_gr(x+1, y), g_gr(x, y));
    Expr ghd_r = absd(g_gr(x+1, y), g_gr(x, y));

    g_r(x, y)  = select(ghd_r < gvd_r, gh_r, gv_r);

    Expr gv_b  = avg(g_gr(x, y+1), g_gr(x, y));
    Expr gvd_b = absd(g_gr(x, y+1), g_gr(x, y));
    Expr gh_b  = avg(g_gb(x-1, y), g_gb(x, y));
    Expr ghd_b = absd(g_gb(x-1, y), g_gb(x, y));

    g_b(x, y)  = select(ghd_b < gvd_b, gh_b, gv_b);

    // Next interpolate red at gr by first interpolating, then
    // correcting using the error green would have had if we had
    // interpolated it in the same way (i.e. add the second derivative
    // of the green channel at the same place).
    Expr correction;
    correction = g_gr(x, y) - avg(g_r(x, y), g_r(x-1, y));
    r_gr(x, y) = correction + avg(r_r(x-1, y), r_r(x, y));

    // Do the same for other reds and blues at green sites
    correction = g_gr(x, y) - avg(g_b(x, y), g_b(x, y-1));
    b_gr(x, y) = correction + avg(b_b(x, y), b_b(x, y-1));

    correction = g_gb(x, y) - avg(g_r(x, y), g_r(x, y+1));
    r_gb(x, y) = correction + avg(r_r(x, y), r_r(x, y+1));

    correction = g_gb(x, y) - avg(g_b(x, y), g_b(x+1, y));
    b_gb(x, y) = correction + avg(b_b(x, y), b_b(x+1, y));

    // Now interpolate diagonally to get red at blue and blue at
    // red. Hold onto your hats; this gets really fancy. We do the
    // same thing as for interpolating green where we try both
    // directions (in this case the positive and negative diagonals),
    // and use the one with the lowest absolute difference. But we
    // also use the same trick as interpolating red and blue at green
    // sites - we correct our interpolations using the second
    // derivative of green at the same sites.

    correction = g_b(x, y)  - avg(g_r(x, y), g_r(x-1, y+1));
    Expr rp_b  = correction + avg(r_r(x, y), r_r(x-1, y+1));
    Expr rpd_b = absd(r_r(x, y), r_r(x-1, y+1));

    correction = g_b(x, y)  - avg(g_r(x-1, y), g_r(x, y+1));
    Expr rn_b  = correction + avg(r_r(x-1, y), r_r(x, y+1));
    Expr rnd_b = absd(r_r(x-1, y), r_r(x, y+1));

    r_b(x, y)  = select(rpd_b < rnd_b, rp_b, rn_b);


    // Same thing for blue at red
    correction = g_r(x, y)  - avg(g_b(x, y), g_b(x+1, y-1));
    Expr bp_r  = correction + avg(b_b(x, y), b_b(x+1, y-1));
    Expr bpd_r = absd(b_b(x, y), b_b(x+1, y-1));

    correction = g_r(x, y)  - avg(g_b(x+1, y), g_b(x, y-1));
    Expr bn_r  = correction + avg(b_b(x+1, y), b_b(x, y-1));
    Expr bnd_r = absd(b_b(x+1, y), b_b(x, y-1));

    b_r(x, y)  =  select(bpd_r < bnd_r, bp_r, bn_r);

    // Interleave the resulting channels
    Func r = interleave_y(interleave_x(r_gr, r_r),
                          interleave_x(r_b, r_gb));
    Func g = interleave_y(interleave_x(g_gr, g_r),
                          interleave_x(g_b, g_gb));
    Func b = interleave_y(interleave_x(b_gr, b_r),
                          interleave_x(b_b, b_gb));

    Func output("output");
    output(x, y, c) = select(c == 0, r(x, y),
                                 c == 1, g(x, y),
                                 b(x, y));

    Scheduler scheduler = [=](Func processed) mutable {
        assert(g_r.defined());
        int vec = get_target().natural_vector_size(UInt(16));
        if (get_target().has_feature(Target::HVX_64)) {
            vec = 32;
        } else if (get_target().has_feature(Target::HVX_128)) {
            vec = 64;
        }
        assert(processed.defined());
        g_r.compute_at(processed, yi)
            .store_at(processed, yo)
            .vectorize(x, 2*vec, TailStrategy::RoundUp)
            .fold_storage(y, 2);
        g_b.compute_at(processed, yi)
            .store_at(processed, yo)
            .vectorize(x, 2*vec, TailStrategy::RoundUp)
            .fold_storage(y, 2);
        output.compute_at(processed, x)
            .vectorize(x)
            .unroll(y)
            .reorder(c, x, y)
            .unroll(c);

        if (get_target().features_any_of({Target::HVX_64, Target::HVX_128})) {
            g_r.align_storage(x, vec);
            g_b.align_storage(x, vec);
        }
    };

    return {output, scheduler};
}


Func CameraPipe::color_correct(Func input) {
    // Get a color matrix by linearly interpolating between two
    // calibrated matrices using inverse kelvin.
    Expr kelvin = color_temp;

    Func matrix;
    Expr alpha = (1.0f/kelvin - 1.0f/3200) / (1.0f/7000 - 1.0f/3200);
    Expr val =  (matrix_3200(x, y) * alpha + matrix_7000(x, y) * (1 - alpha));
    matrix(x, y) = cast<int16_t>(val * 256.0f); // Q8.8 fixed point
    matrix.compute_root();

    Func corrected("corrected");
    Expr ir = cast<int32_t>(input(x, y, 0));
    Expr ig = cast<int32_t>(input(x, y, 1));
    Expr ib = cast<int32_t>(input(x, y, 2));

    Expr r = matrix(3, 0) + matrix(0, 0) * ir + matrix(1, 0) * ig + matrix(2, 0) * ib;
    Expr g = matrix(3, 1) + matrix(0, 1) * ir + matrix(1, 1) * ig + matrix(2, 1) * ib;
    Expr b = matrix(3, 2) + matrix(0, 2) * ir + matrix(1, 2) * ig + matrix(2, 2) * ib;

    r = cast<int16_t>(r/256);
    g = cast<int16_t>(g/256);
    b = cast<int16_t>(b/256);
    corrected(x, y, c) = select(c == 0, r,
                                c == 1, g,
                                        b);

    return corrected;
}

Func CameraPipe::apply_curve(Func input) {
    // copied from FCam
    Func curve("curve");

    Expr minRaw = 0 + blackLevel;
    Expr maxRaw = whiteLevel;

    // How much to upsample the LUT by when sampling it.
    int lutResample = 1;
    if (get_target().features_any_of({Target::HVX_64, Target::HVX_128})) {
        // On HVX, LUT lookups are much faster if they are to LUTs not
        // greater than 256 elements, so we reduce the tonemap to 256
        // elements and use linear interpolation to upsample it.
        lutResample = 8;
    }

    minRaw /= lutResample;
    maxRaw /= lutResample;

    Expr invRange = 1.0f/(maxRaw - minRaw);
    Expr b = 2.0f - pow(2.0f, contrast/100.0f);
    Expr a = 2.0f - 2.0f*b;

    // Get a linear luminance in the range 0-1
    Expr xf = clamp(cast<float>(x - minRaw)*invRange, 0.0f, 1.0f);
    // Gamma correct it
    Expr g = pow(xf, 1.0f/gamma);
    // Apply a piecewise quadratic contrast curve
    Expr z = select(g > 0.5f,
                    1.0f - (a*(1.0f-g)*(1.0f-g) + b*(1.0f-g)),
                    a*g*g + b*g);

    // Convert to 8 bit and save
    Expr val = cast(result_type, clamp(z*255.0f+0.5f, 0.0f, 255.0f));
    // makeLUT add guard band outside of (minRaw, maxRaw]:
    curve(x) = select(x <= minRaw, 0, select(x > maxRaw, 255, val));

    curve.compute_root(); // It's a LUT, compute it once ahead of time.

    Func curved("curved");

    if (lutResample == 1) {
        // Use clamp to restrict size of LUT as allocated by compute_root
        curved(x, y, c) = curve(clamp(input(x, y, c), 0, 1023));
    } else {
        // Use linear interpolation to sample the LUT.
        Expr in = input(x, y, c);
        Expr u0 = in/lutResample;
        Expr u = in%lutResample;
        Expr y0 = curve(clamp(u0, 0, 127));
        Expr y1 = curve(clamp(u0 + 1, 0, 127));
        curved(x, y, c) = cast<uint8_t>((cast<uint16_t>(y0)*lutResample + (y1 - y0)*u)/lutResample);
    }

    return curved;
}

Func CameraPipe::build() {
    // shift things inwards to give us enough padding on the
    // boundaries so that we don't need to check bounds. We're going
    // to make a 2560x1920 output image, just like the FCam pipe, so
    // shift by 16, 12. We also convert it to be signed, so we can deal
    // with values that fall below 0 during processing.
    Func shifted;
    shifted(x, y) = cast<int16_t>(input(x+16, y+12));

    Func denoised = hot_pixel_suppression(shifted);
    Func deinterleaved = deinterleave(denoised);
    Func demosaiced; Scheduler demosaiced_scheduler;
    std::tie(demosaiced, demosaiced_scheduler) = demosaic(deinterleaved);
    Func corrected = color_correct(demosaiced);
    Func processed = apply_curve(corrected);

    // Schedule
    Expr out_width = processed.output_buffer().width();
    Expr out_height = processed.output_buffer().height();
    // In HVX 128, we need 2 threads to saturate HVX with work,
    //and in HVX 64 we need 4 threads, and on other devices,
    // we might need many threads.
    Expr strip_size;
    if (get_target().has_feature(Target::HVX_128)) {
        strip_size = processed.output_buffer().dim(1).extent() / 2;
    } else if (get_target().has_feature(Target::HVX_64)) {
        strip_size = processed.output_buffer().dim(1).extent() / 4;
    } else {
        strip_size = 32;
    }
    strip_size = (strip_size / 2) * 2;

    int vec = get_target().natural_vector_size(UInt(16));
    if (get_target().has_feature(Target::HVX_64)) {
        vec = 32;
    } else if (get_target().has_feature(Target::HVX_128)) {
        vec = 64;
    }
    denoised.compute_at(processed, yi).store_at(processed, yo)
        .prefetch(y, 2)
        .fold_storage(y, 8)
        .tile(x, y, x, y, xi, yi, 2*vec, 2)
        .vectorize(xi)
        .unroll(yi);
    deinterleaved.compute_at(processed, yi).store_at(processed, yo)
        .fold_storage(y, 4)
        .reorder(c, x, y)
        .vectorize(x, 2*vec, TailStrategy::RoundUp)
        .unroll(c);
    corrected.compute_at(processed, x)
        .reorder(c, x, y)
        .vectorize(x)
        .unroll(y)
        .unroll(c);
    processed.compute_root()
        .reorder(c, x, y)
        .split(y, yo, yi, strip_size)
        .tile(x, yi, x, yi, xi, yii, 2*vec, 2, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(yii)
        .unroll(c)
        .parallel(yo);

    demosaiced_scheduler(processed);

    if (get_target().features_any_of({Target::HVX_64, Target::HVX_128})) {
        processed.hexagon();
        denoised.align_storage(x, vec);
        deinterleaved.align_storage(x, vec);
        corrected.align_storage(x, vec);
    }

    // We can generate slightly better code if we know the splits divide the extent.
    processed
        .bound(c, 0, 3)
        .bound(x, 0, ((out_width)/(2*vec))*(2*vec))
        .bound(y, 0, (out_height/strip_size)*strip_size);

    return processed;
};


Halide::RegisterGenerator<CameraPipe> register_me{"camera_pipe"};

}  // namespace
