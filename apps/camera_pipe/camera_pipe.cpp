#include <FImage.h>
#include <stdint.h>

using namespace FImage;

Var x, y, c;

Func hot_pixel_suppression(Func input) {
    Expr max = Max(Max(input(x-2, y), input(x+2, y)),
                   Max(input(x, y-2), input(x, y+2)));
    Expr min = Min(Min(input(x-2, y), input(x+2, y)),
                   Min(input(x, y-2), input(x, y+2)));
    
    Func denoised("denoised");
    denoised(x, y) = Clamp(input(x, y), min, max);
    
    return denoised;
}

Expr abs(Expr e) {
    return Select(e < Cast(e.type(), 0), -e, e);
}

Func interleave_x(Func a, Func b) {
    Func out;
    out(x, y) = Select((x%2)==0, a(x/2, y), b(x/2, y));
    return out;
}

Func interleave_y(Func a, Func b) {
    Func out;
    out(x, y) = Select((y%2)==0, a(x, y/2), b(x, y/2));
    return out;
}

Func demosaic(Func raw) {
    // These are the values we already know from the input
    // x_y = the value of channel x at a site in the input of channel y
    // gb refers to green sites in the blue rows
    // gr refers to green sites in the red rows
    Func r_r("r_r"), g_gr("g_gr"), g_gb("g_gb"), b_b("b_b");    
    g_gr(x, y) = raw(2*x, 2*y);
    r_r(x, y)  = raw(2*x+1, 2*y);
    b_b(x, y)  = raw(2*x, 2*y+1);
    g_gb(x, y) = raw(2*x+1, 2*y+1);

    // These are the ones we need to interpolate
    Func b_r("b_r"), g_r("g_r");
    Func b_gr("b_gr"), r_gr("r_gr");
    Func b_gb("b_gb"), r_gb("r_gb");
    Func r_b("r_b"), g_b("g_b");

    // First calculate green at the red and blue sites

    // Try interpolating vertically and horizontally. Also compute
    // differences vertically and horizontally. Use interpolation in
    // whichever direction had the smallest difference.
    Expr gv_r  =    (g_gb(x, y-1) + g_gb(x, y))/2;
    Expr gvd_r = abs(g_gb(x, y-1) - g_gb(x, y));
    Expr gh_r  =    (g_gr(x+1, y) + g_gr(x, y))/2;
    Expr ghd_r = abs(g_gr(x+1, y) - g_gr(x, y));

    g_r(x, y)  = Select(ghd_r < gvd_r, gh_r, gv_r);

    Expr gv_b  =    (g_gr(x, y+1) + g_gr(x, y))/2;
    Expr gvd_b = abs(g_gr(x, y+1) - g_gr(x, y));
    Expr gh_b  =    (g_gb(x-1, y) + g_gb(x, y))/2;
    Expr ghd_b = abs(g_gb(x-1, y) - g_gb(x, y));

    g_b(x, y)  = Select(ghd_b < gvd_b, gh_b, gv_b);

    // Next interpolate red at gr by first interpolating, then
    // correcting using the error green would have had if we had
    // interpolated it in the same way (i.e. add the second derivative
    // of the green channel at the same place).
    Expr correction;
    correction = g_gr(x, y) - (g_r(x, y) + g_r(x-1, y))/2;
    r_gr(x, y) = correction + (r_r(x-1, y) + r_r(x, y))/2;
    
    // Do the same for other reds and blues at green sites
    correction = g_gr(x, y) - (g_b(x, y) + g_b(x, y-1))/2;
    b_gr(x, y) = correction + (b_b(x, y) + b_b(x, y-1))/2;

    correction = g_gb(x, y) - (g_r(x, y) + g_r(x, y+1))/2;
    r_gb(x, y) = correction + (r_r(x, y) + r_r(x, y+1))/2;

    correction = g_gb(x, y) - (g_b(x, y) + g_b(x+1, y))/2;
    b_gb(x, y) = correction + (b_b(x, y) + b_b(x+1, y))/2;
            
    // Now interpolate diagonally to get red at blue and blue at
    // red. Hold onto your hats; this gets really fancy. We do the
    // same thing as for interpolating green where we try both
    // directions (in this case the positive and negative diagonals),
    // and use the one with the lowest absolute difference. But we
    // also use the same trick as interpolating red and blue at green
    // sites - we correct our interpolations using the second
    // derivative of green at the same sites.

    correction = g_b(x, y)  - (g_r(x, y) + g_r(x-1, y+1))/2;
    Expr rp_b  = correction + (r_r(x, y) + r_r(x-1, y+1))/2;
    Expr rpd_b = abs(r_r(x, y) - r_r(x-1, y+1));

    correction = g_b(x, y)  - (g_r(x-1, y) + g_r(x, y+1))/2;
    Expr rn_b  = correction + (r_r(x-1, y) + r_r(x, y+1))/2;
    Expr rnd_b = abs(r_r(x-1, y) - r_r(x, y+1));

    r_b(x, y)  = Select(rpd_b < rnd_b, rp_b, rn_b);

    // Same thing for blue at red
    correction = g_r(x, y)  - (g_b(x, y) + g_b(x+1, y-1))/2;
    Expr bp_r  = correction + (b_b(x, y) + b_b(x+1, y-1))/2;
    Expr bpd_r = abs(b_b(x, y) - b_b(x+1, y-1));

    correction = g_r(x, y)  - (g_b(x+1, y) + g_b(x, y-1))/2;
    Expr bn_r  = correction + (b_b(x+1, y) + b_b(x, y-1))/2;
    Expr bnd_r = abs(b_b(x+1, y) - b_b(x, y-1));

    b_r(x, y)  =  Select(bpd_r < bnd_r, bp_r, bn_r);    

    // Interleave the resulting channels
    Func r = interleave_y(interleave_x(r_gr, r_r),
                          interleave_x(r_b, r_gb));
    Func g = interleave_y(interleave_x(g_gr, g_r),
                          interleave_x(g_b, g_gb));
    Func b = interleave_y(interleave_x(b_gr, b_r),                          
                          interleave_x(b_b, b_gb));

    Func output("dem");
    output(x, y) = (r(x, y), g(x, y), b(x, y));

    return output;
}


Func color_correct(Func input, UniformImage matrix_3200, UniformImage matrix_7000, Uniform<float> kelvin) {
    // Get a color matrix by linearly interpolating between two
    // calibrated matrices using inverse kelvin.

    Func matrix;
    Expr alpha = (1.0f/kelvin - 1.0f/3200) / (1.0f/7000 - 1.0f/3200);
    matrix(x, y) = (matrix_3200(x, y) * alpha + 
                    matrix_7000(x, y) * (1 - alpha));

    Func corrected;
    RVar j(0, 3);
    corrected(x, y, c) = Cast<int16_t>(Sum(matrix(j, c)*Cast<float>(input(x, y, j))) + matrix(3, c));
    return corrected;
}


Func apply_curve(Func input, Uniform<float> gamma, Uniform<float> contrast) {
    // copied from FCam
    Func curve("curve");

    Expr xf = Cast<float>(x)/1024.0f;    
    Expr g = pow(xf, 1.0f/gamma);
    Expr b = 2.0f - pow(2.0f, contrast/100.0f);
    Expr a = 2.0f - 2.0f*b; 
    Expr z = Select(g > 0.5f,
                    1.0f - (a*(1.0f-g)*(1.0f-g) + b*(1.0f-g)),
                    a*g*g + b*g);
    curve(x) = Cast<uint8_t>(Clamp(z*256.0f, 0.0f, 255.0f));

    Func curved("curved");
    // This is after the color transform, so the input could be
    // negative or very large. Clamp it back to 10 bits before applying the curve.
    curved(x, y, c) = curve(Clamp(Cast<int32_t>(input(x, y, c)), 0, 1023));
    return curved;
}

/*
Func rgb_to_yuv422(Func rgb) {
    Func Y;
    Func U;
    Func V;
    Func UV;
    Func YUV;    
}
*/

Func process(Func raw, 
             UniformImage matrix_3200, UniformImage matrix_7000, Uniform<float> color_temp, 
             Uniform<float> gamma, Uniform<float> contrast) {
    Func im = raw;
    im = hot_pixel_suppression(im);
    im = demosaic(im);
    im = color_correct(im, matrix_3200, matrix_7000, color_temp);
    im = apply_curve(im, gamma, contrast);

    /*
    Func check;
    Var x, y, c;
    check(x, y, c) = Debug(im(x, y, c), "after curve ", x, y, c, im(x, y, c));
    im = check;
    */

    //im = rgb_to_yuv422(im);
    return im;
}

int main(int argc, char **argv) {
    UniformImage input(UInt(16), 2, "raw");
    UniformImage matrix_3200(Float(32), 2, "m3200"), matrix_7000(Float(32), 2, "m7000");
    Uniform<float> color_temp("color_temp", 3200.0f);
    Uniform<float> gamma("gamma", 1.8f);
    Uniform<float> contrast("contrast", 10.0f);

    // add a boundary condition and treat things as signed ints
    // (demosaic might introduce negative values)
    Func clamped("in");
    clamped(x, y) = Cast<int16_t>(input(Clamp(x, 0, input.width()-1),
                                        Clamp(y, 0, input.height()-1)));

    // Run the pipeline
    Func output = process(clamped, matrix_3200, matrix_7000, color_temp, gamma, contrast);

    // Pick a schedule   

    if (argc > 1) 
        srand(atoi(argv[1]));
    else
        srand(0);

    std::vector<Func> funcs = output.rhs().funcs();
    
    for (size_t i = 0; i < funcs.size(); i++) {
        funcs[i].root();
        if (funcs[i].name() == "curve") continue;
        if (funcs[i].returnType() == UInt(8)) funcs[i].vectorize(x, 16);
        if (funcs[i].returnType() == Int(16)) funcs[i].vectorize(x, 8);
        if (funcs[i].returnType() == Float(32)) funcs[i].vectorize(x, 4);
    }

    /*

        if (rand() % 4 == 0) continue; // 25% chance of inline

        // we should definitely evaluate the curve ahead of time
        if (funcs[i].name() == "curve") {
            funcs[i].root();
            continue;
        }

        // Randomly select root or chunk over xo
        if (rand() % 4 == 0) {
            funcs[i].root();
        } else {
            funcs[i].chunk(xo);
        }

        // Maybe unroll across y by 2
        if (rand() % 2) {
            funcs[i].unroll(y, 2);
        }
        
        // choose between unrolling, vectorizing, or nothing across x
        switch (rand() % 3) {
        case 0:
            funcs[i].unroll(x, 2);
            break;
        case 1:
            if (funcs[i].returnType() == Int(16)) funcs[i].vectorize(x, 8);
            if (funcs[i].returnType() == UInt(8)) funcs[i].vectorize(x, 16);
            break;
        case 2:
            break;
        }
    }
    */
    output.compileToFile("curved");
    
    return 0;
}

