#include "Halide.h"

namespace {

class BilateralGrid : public Halide::Generator<BilateralGrid> {
public:
    GeneratorParam<int>   s_sigma{"s_sigma", 8};

    ImageParam            input{Float(32), 2, "input"};
    Param<float>          r_sigma{"r_sigma"};

    Func build() {
        Var x("x"), y("y"), z("z"), c("c");

        // Add a boundary condition
        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        // Construct the bilateral grid
        RDom r(0, s_sigma, 0, s_sigma);
        Expr val = clamped(x * s_sigma + r.x - s_sigma/2, y * s_sigma + r.y - s_sigma/2);
        val = clamp(val, 0.0f, 1.0f);

        Expr zi = cast<int>(val * (1.0f/r_sigma) + 0.5f);

        Func histogram("histogram");
        histogram(x, y, z, c) = 0.0f;
        histogram(x, y, zi, c) += select(c == 0, val, 1.0f);

        // Blur the grid using a five-tap filter
        Func blurx("blurx"), blury("blury"), blurz("blurz");
        blurz(x, y, z, c) = (histogram(x, y, z-2, c) +
                             histogram(x, y, z-1, c)*4 +
                             histogram(x, y, z  , c)*6 +
                             histogram(x, y, z+1, c)*4 +
                             histogram(x, y, z+2, c));
        blurx(x, y, z, c) = (blurz(x-2, y, z, c) +
                             blurz(x-1, y, z, c)*4 +
                             blurz(x  , y, z, c)*6 +
                             blurz(x+1, y, z, c)*4 +
                             blurz(x+2, y, z, c));
        blury(x, y, z, c) = (blurx(x, y-2, z, c) +
                             blurx(x, y-1, z, c)*4 +
                             blurx(x, y  , z, c)*6 +
                             blurx(x, y+1, z, c)*4 +
                             blurx(x, y+2, z, c));

        // Take trilinear samples to compute the output
        val = clamp(input(x, y), 0.0f, 1.0f);
        Expr zv = val * (1.0f/r_sigma);
        zi = cast<int>(zv);
        Expr zf = zv - zi;
        Expr xf = cast<float>(x % s_sigma) / s_sigma;
        Expr yf = cast<float>(y % s_sigma) / s_sigma;
        Expr xi = x/s_sigma;
        Expr yi = y/s_sigma;
        Func interpolated("interpolated");
        interpolated(x, y, c) =
            lerp(lerp(lerp(blury(xi, yi, zi, c), blury(xi+1, yi, zi, c), xf),
                      lerp(blury(xi, yi+1, zi, c), blury(xi+1, yi+1, zi, c), xf), yf),
                 lerp(lerp(blury(xi, yi, zi+1, c), blury(xi+1, yi, zi+1, c), xf),
                      lerp(blury(xi, yi+1, zi+1, c), blury(xi+1, yi+1, zi+1, c), xf), yf), zf);

        // Normalize
        Func bilateral_grid("bilateral_grid");
        bilateral_grid(x, y) = interpolated(x, y, 0)/interpolated(x, y, 1);

        if (get_target().has_gpu_feature()) {
            Var xi("xi"), yi("yi"), zi("zi");

            // Schedule blurz in 8x8 tiles. This is a tile in
            // grid-space, which means it represents something like
            // 64x64 pixels in the input (if s_sigma is 8).
            blurz.compute_root().reorder(c, z, x, y).gpu_tile(x, y, xi, yi, 8, 8);

            // Schedule histogram to happen per-tile of blurz, with
            // intermediate results in shared memory. This means histogram
            // and blurz makes a three-stage kernel:
            // 1) Zero out the 8x8 set of histograms
            // 2) Compute those histogram by iterating over lots of the input image
            // 3) Blur the set of histograms in z
            histogram.reorder(c, z, x, y).compute_at(blurz, x).gpu_threads(x, y);
            histogram.update().reorder(c, r.x, r.y, x, y).gpu_threads(x, y).unroll(c);

            // An alternative schedule for histogram that doesn't use shared memory:
            // histogram.compute_root().reorder(c, z, x, y).gpu_tile(x, y, xi, yi, 8, 8);
            // histogram.update().reorder(c, r.x, r.y, x, y).gpu_tile(x, y, xi, yi, 8, 8).unroll(c);

            // Schedule the remaining blurs and the sampling at the end similarly.
            blurx.compute_root().gpu_tile(x, y, z, xi, yi, zi, 8, 8, 1);
            blury.compute_root().gpu_tile(x, y, z, xi, yi, zi, 8, 8, 1);
            bilateral_grid.compute_root().gpu_tile(x, y, xi, yi, s_sigma, s_sigma);
        } else {
            // The CPU schedule.
            blurz.compute_root().reorder(c, z, x, y).parallel(y).vectorize(x, 8).unroll(c);
            histogram.compute_at(blurz, y);
            histogram.update().reorder(c, r.x, r.y, x, y).unroll(c);
            blurx.compute_root().reorder(c, x, y, z).parallel(z).vectorize(x, 8).unroll(c);
            blury.compute_root().reorder(c, x, y, z).parallel(z).vectorize(x, 8).unroll(c);
            bilateral_grid.compute_root().parallel(y).vectorize(x, 8);
        }

        return bilateral_grid;
    }
};

Halide::RegisterGenerator<BilateralGrid> register_me{"bilateral_grid"};

}  // namespace
