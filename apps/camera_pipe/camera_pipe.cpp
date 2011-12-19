#include <FImage.h>
#include <stdint.h>

using namespace FImage;



Image<int16_t> load(const char *filename) {

    FILE *f = fopen(filename, "rb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h;

    fread(&h, sizeof(header_t), 1, f);

    Image<int16_t> im(h.width, h.height);
    
    printf("Fread\n");
    for (size_t y = 0; y < im.height(); y++) {
        for (size_t x = 0; x < im.width(); x++) {
            float val;
            fread(&val, sizeof(float), 1, f);
            im(x, y) = int16_t(val * 1024);
        }
    }
    printf("Done\n");

    fclose(f);
    return im;
}

void save(Image<int16_t> im, const char *filename) {
    FILE *f = fopen(filename, "wb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h {1, im.width(), im.height(), im.channels(), 0};

    
    fwrite(&h, sizeof(header_t), 1, f);

    for (size_t y = 0; y < im.height(); y++) {
        for (size_t x = 0; x < im.width(); x++) {
            for (size_t c = 0; c < im.channels(); c++) {
                float val = (float)(im(x, y, c))/1024.0f;
                fwrite(&val, sizeof(float), 1, f);
            }
        }
    }

    fclose(f);
}

Var x, y, c;

Func hot_pixel_suppression(Func input) {
    Expr max = Max(Max(input(x-2, y), input(x+2, y)),
                   Max(input(x, y-2), input(x, y+2)));
    Expr min = Min(Min(input(x-2, y), input(x+2, y)),
                   Min(input(x, y-2), input(x, y+2)));
    
    Func clamped;
    clamped(x, y) = Clamp(input(x, y), min, max);
    
    return clamped;
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

    b_r(x, y)  = Select(bpd_r < bnd_r, bp_r, bn_r);    

    // Interleave the resulting channels
    Func r = interleave_y(interleave_x(r_gr, r_r),
                          interleave_x(r_b, r_gb));
    Func g = interleave_y(interleave_x(g_gr, g_r),
                          interleave_x(g_b, g_gb));
    Func b = interleave_y(interleave_x(b_gr, b_r),
                          interleave_x(b_b, b_gb));

    Func output("dem");
    output(x, y, c) = Select(c == 0, r(x, y), Select(c == 1, g(x, y), b(x, y)));

    return output;
}

/*
Func color_correct(Func input, Uniform<float> kelvin) {
    // Get a color matrix by linearly interpolating between two
    // calibrated matrices using inverse kelvin.
    Image matrix_3200(3, 4);
    matrix_3200 = {};
    Image matrix_7000(3, 4);
    matrix_7000 = {};

    Func matrix;
    int16_t alpha = (1.0f/kelvin) / (1.0f/3200 - 1.0f/7000) - (1.0f/7000);
    matrix(x, y) = (matrix_3200(x, y) * alpha + 
                    matrix_7000(x, y) * (1 - alpha));

    Func corrected;
    RVar i(0, 3), j(0, 3);    
    corrected(x, y, i) = Cast<int16_t>(matrix(i, j)*Cast<float>(input(x, y, j)) + matrix(i, 3));
    return corrected;
}
*/

Func apply_curve(Func input, Uniform<float> gamma, Uniform<float> contrast) {
    Func gamma_curve;
    gamma_curve(x) = x;

    Func contrast_curve;
    contrast_curve(x) = x;

    Func combined_curve;
    combined_curve(x) = contrast_curve(gamma_curve(x));

    Func output;
    output(x, y) = combined_curve(input(x, y));
    return output;
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

Func process(Func raw, Uniform<float> color_temp, 
             Uniform<float> gamma, Uniform<float> contrast) {
    Func im = raw;
    im = hot_pixel_suppression(im);
    im = demosaic(im);
    //im = color_correct(im, color_temp);
    //im = apply_curve(im, gamma, contrast);
    //im = rgb_to_yuv422(im);
    return im;
}

int main(int argc, char **argv) {
    Image<int16_t> input = load(argv[1]);
    Uniform<float> color_temp = 3200.0f;
    Uniform<float> gamma = 2.2f;
    Uniform<float> contrast = 1.0f;
    Func clamped("in");
    clamped(x, y) = input(Clamp(x, 0, input.width()),
                          Clamp(y, 0, input.height()));

    Func output = process(clamped, color_temp, gamma, contrast);

    std::vector<Func> funcs = output.rhs().funcs();
    for (size_t i = 0; i < funcs.size(); i++) {
        funcs[i].root();
    }


    Image<int16_t> out = output.realize(input.width(), input.height(), 3);

    save(out, argv[2]);

    return 0;
}

