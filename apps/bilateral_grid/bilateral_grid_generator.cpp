#include "Halide.h"
#include "halide_trace_config.h"

namespace {

class BilateralGrid : public Halide::Generator<BilateralGrid> {
public:
    GeneratorParam<int> s_sigma{"s_sigma", 8};

    Input<Buffer<float, 2>> input{"input"};
    Input<float> r_sigma{"r_sigma"};
    Output<Buffer<float, 2>> bilateral_grid{"bilateral_grid"};

    void generate() {
        Var x("x"), y("y"), z("z"), c("c");

        // Add a boundary condition
        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        // Construct the bilateral grid
        RDom r(0, s_sigma, 0, s_sigma);
        Expr val = clamped(x * s_sigma + r.x - s_sigma / 2, y * s_sigma + r.y - s_sigma / 2);
        val = clamp(val, 0.0f, 1.0f);

        Expr zi = cast<int>(val * (1.0f / r_sigma) + 0.5f);

        Func histogram("histogram");
        histogram(x, y, z, c) = 0.0f;
        histogram(x, y, zi, c) += mux(c, {val, 1.0f});

        // Blur the grid using a five-tap filter
        Func blurx("blurx"), blury("blury"), blurz("blurz");
        blurz(x, y, z, c) = (histogram(x, y, z - 2, c) +
                             histogram(x, y, z - 1, c) * 4 +
                             histogram(x, y, z, c) * 6 +
                             histogram(x, y, z + 1, c) * 4 +
                             histogram(x, y, z + 2, c));
        blurx(x, y, z, c) = (blurz(x - 2, y, z, c) +
                             blurz(x - 1, y, z, c) * 4 +
                             blurz(x, y, z, c) * 6 +
                             blurz(x + 1, y, z, c) * 4 +
                             blurz(x + 2, y, z, c));
        blury(x, y, z, c) = (blurx(x, y - 2, z, c) +
                             blurx(x, y - 1, z, c) * 4 +
                             blurx(x, y, z, c) * 6 +
                             blurx(x, y + 1, z, c) * 4 +
                             blurx(x, y + 2, z, c));

        // Take trilinear samples to compute the output
        val = clamp(input(x, y), 0.0f, 1.0f);
        Expr zv = val * (1.0f / r_sigma);
        zi = cast<int>(zv);
        Expr zf = zv - zi;
        Expr xf = cast<float>(x % s_sigma) / s_sigma;
        Expr yf = cast<float>(y % s_sigma) / s_sigma;
        Expr xi = x / s_sigma;
        Expr yi = y / s_sigma;
        Func interpolated("interpolated");
        interpolated(x, y, c) =
            lerp(lerp(lerp(blury(xi, yi, zi, c), blury(xi + 1, yi, zi, c), xf),
                      lerp(blury(xi, yi + 1, zi, c), blury(xi + 1, yi + 1, zi, c), xf), yf),
                 lerp(lerp(blury(xi, yi, zi + 1, c), blury(xi + 1, yi, zi + 1, c), xf),
                      lerp(blury(xi, yi + 1, zi + 1, c), blury(xi + 1, yi + 1, zi + 1, c), xf), yf),
                 zf);

        // Normalize
        bilateral_grid(x, y) = interpolated(x, y, 0) / interpolated(x, y, 1);

        /* ESTIMATES */
        // (This can be useful in conjunction with RunGen and benchmarks as well
        // as auto-schedule, so we do it in all cases.)
        // Provide estimates on the input image
        input.set_estimates({{0, 1536}, {0, 2560}});
        // Provide estimates on the parameters
        r_sigma.set_estimate(0.1f);
        // TODO: Compute estimates from the parameter values
        histogram.set_estimate(z, -2, 16);
        blurz.set_estimate(z, 0, 12);
        blurx.set_estimate(z, 0, 12);
        blury.set_estimate(z, 0, 12);
        bilateral_grid.set_estimates({{0, 1536}, {0, 2560}});

        if (auto_schedule) {
            // nothing
        } else if (get_target().has_gpu_feature()) {
            // 0.50ms on an RTX 2060

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

            // Schedule the remaining blurs and the sampling at the end similarly.
            blurx
                .compute_root()
                .reorder(c, x, y, z)
                .reorder_storage(c, x, y, z)
                .vectorize(c)
                .unroll(y, 2, TailStrategy::RoundUp)
                .gpu_tile(x, y, z, xi, yi, zi, 32, 8, 1, TailStrategy::RoundUp);
            blury
                .compute_root()
                .reorder(c, x, y, z)
                .reorder_storage(c, x, y, z)
                .vectorize(c)
                .unroll(y, 2, TailStrategy::RoundUp)
                .gpu_tile(x, y, z, xi, yi, zi, 32, 8, 1, TailStrategy::RoundUp);
            bilateral_grid.compute_root().gpu_tile(x, y, xi, yi, 32, 8);
            interpolated.compute_at(bilateral_grid, xi).vectorize(c);
        } else {
            // CPU schedule.

            // 3.98ms on an Intel i9-9960X using 32 threads at 3.7 GHz
            // using target x86-64-avx2. This is a little less
            // SIMD-friendly than some of the other apps, so we
            // benefit from hyperthreading, and don't benefit from
            // AVX-512, which on my machine reduces the clock to 3.0
            // GHz.

            blurz.compute_root()
                .reorder(c, z, x, y)
                .parallel(y)
                .vectorize(x, 8)
                .unroll(c);
            histogram.compute_at(blurz, y);
            histogram.update()
                .reorder(c, r.x, r.y, x, y)
                .unroll(c);
            blurx.compute_root()
                .reorder(c, x, y, z)
                .parallel(z)
                .vectorize(x, 8)
                .unroll(c);
            blury.compute_root()
                .reorder(c, x, y, z)
                .parallel(z)
                .vectorize(x, 8)
                .unroll(c);
            bilateral_grid.compute_root()
                .parallel(y)
                .vectorize(x, 8);
        }

        /* Optional tags to specify layout for HalideTraceViz */
        {
            Halide::Trace::FuncConfig cfg;
            cfg.pos.x = 100;
            cfg.pos.y = 300;
            input.add_trace_tag(cfg.to_trace_tag());

            cfg.pos.x = 1564;
            bilateral_grid.add_trace_tag(cfg.to_trace_tag());
        }
        {
            Halide::Trace::FuncConfig cfg;
            cfg.strides = {{1, 0}, {0, 1}, {40, 0}};
            cfg.zoom = 3;

            cfg.max = 32;
            cfg.pos.x = 550;
            cfg.pos.y = 100;
            histogram.add_trace_tag(cfg.to_trace_tag());

            cfg.max = 512;
            cfg.pos.y = 300;
            blurz.add_trace_tag(cfg.to_trace_tag());

            cfg.max = 8192;
            cfg.pos.y = 500;
            blurx.add_trace_tag(cfg.to_trace_tag());

            cfg.max = 131072;
            cfg.pos.y = 700;
            blury.add_trace_tag(cfg.to_trace_tag());
        }
        {
            // GlobalConfig applies to the entire visualization pipeline;
            // you can set this tag on any Func that is realized, but only
            // the last one seen will be used. (Since the tags are emitted in
            // an arbitrary order, emitting only one such tag is the best practice).
            // Note also that since the global settings are often context-dependent
            // (eg the output size and timestep may vary depending on the
            // input data), it's often more useful to specify these on the
            // command line.
            Halide::Trace::GlobalConfig global_cfg;
            global_cfg.timestep = 1000;

            bilateral_grid.add_trace_tag(global_cfg.to_trace_tag());
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(BilateralGrid, bilateral_grid)
