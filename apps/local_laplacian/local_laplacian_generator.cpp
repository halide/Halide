#include "Halide.h"
#include "halide_trace_config.h"

namespace {

constexpr int maxJ = 20;

class LocalLaplacian : public Halide::Generator<LocalLaplacian> {
public:
    GeneratorParam<int> pyramid_levels{"pyramid_levels", 8, 1, maxJ};

    Input<Buffer<uint16_t, 3>> input{"input"};
    Input<int> levels{"levels"};
    Input<float> alpha{"alpha"};
    Input<float> beta{"beta"};
    Output<Buffer<uint16_t, 3>> output{"output"};

    void generate() {
        /* THE ALGORITHM */
        const int J = pyramid_levels;

        // Make the remapping function as a lookup table.
        Func remap;
        Expr fx = cast<float>(x) / 256.0f;
        remap(x) = alpha * fx * exp(-fx * fx / 2.0f);

        // Set a boundary condition
        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        // Convert to floating point
        Func floating;
        floating(x, y, c) = clamped(x, y, c) / 65535.0f;

        // Get the luminance channel
        Func gray;
        gray(x, y) = 0.299f * floating(x, y, 0) + 0.587f * floating(x, y, 1) + 0.114f * floating(x, y, 2);

        // Make the processed Gaussian pyramid.
        Func gPyramid[maxJ];
        // Do a lookup into a lut with 256 entires per intensity level
        Expr level = k * (1.0f / (levels - 1));
        Expr idx = gray(x, y) * cast<float>(levels - 1) * 256.0f;
        idx = clamp(cast<int>(idx), 0, (levels - 1) * 256);
        gPyramid[0](x, y, k) = beta * (gray(x, y) - level) + level + remap(idx - 256 * k);
        for (int j = 1; j < J; j++) {
            gPyramid[j](x, y, k) = downsample(gPyramid[j - 1])(x, y, k);
        }

        // Get its laplacian pyramid
        Func lPyramid[maxJ];
        lPyramid[J - 1](x, y, k) = gPyramid[J - 1](x, y, k);
        for (int j = J - 2; j >= 0; j--) {
            lPyramid[j](x, y, k) = gPyramid[j](x, y, k) - upsample(gPyramid[j + 1])(x, y, k);
        }

        // Make the Gaussian pyramid of the input
        Func inGPyramid[maxJ];
        inGPyramid[0](x, y) = gray(x, y);
        for (int j = 1; j < J; j++) {
            inGPyramid[j](x, y) = downsample(inGPyramid[j - 1])(x, y);
        }

        // Make the laplacian pyramid of the output
        Func outLPyramid[maxJ];
        for (int j = 0; j < J; j++) {
            // Split input pyramid value into integer and floating parts
            Expr level = inGPyramid[j](x, y) * cast<float>(levels - 1);
            Expr li = clamp(cast<int>(level), 0, levels - 2);
            Expr lf = level - cast<float>(li);
            // Linearly interpolate between the nearest processed pyramid levels
            outLPyramid[j](x, y) = (1.0f - lf) * lPyramid[j](x, y, li) + lf * lPyramid[j](x, y, li + 1);
        }

        // Make the Gaussian pyramid of the output
        Func outGPyramid[maxJ];
        outGPyramid[J - 1](x, y) = outLPyramid[J - 1](x, y);
        for (int j = J - 2; j >= 0; j--) {
            outGPyramid[j](x, y) = upsample(outGPyramid[j + 1])(x, y) + outLPyramid[j](x, y);
        }

        // Reintroduce color (Connelly: use eps to avoid scaling up noise w/ apollo3.png input)
        Func color;
        float eps = 0.01f;
        color(x, y, c) = outGPyramid[0](x, y) * (floating(x, y, c) + eps) / (gray(x, y) + eps);

        // Convert back to 16-bit
        output(x, y, c) = cast<uint16_t>(clamp(color(x, y, c), 0.0f, 1.0f) * 65535.0f);

        /* ESTIMATES */
        // (This can be useful in conjunction with RunGen and benchmarks as well
        // as auto-schedule, so we do it in all cases.)
        input.set_estimates({{0, 1536}, {0, 2560}, {0, 3}});
        // Provide estimates on the parameters
        levels.set_estimate(8);
        alpha.set_estimate(1);
        beta.set_estimate(1);
        // Provide estimates on the pipeline output
        output.set_estimates({{0, 1536}, {0, 2560}, {0, 3}});

        /* THE SCHEDULE */
        if (auto_schedule) {
            // Nothing.
        } else if (get_target().has_gpu_feature()) {
            // GPU schedule.
            // 3.19ms on an RTX 2060.
            remap.compute_root();
            Var xi, yi;
            output.compute_root().gpu_tile(x, y, xi, yi, 16, 8);
            for (int j = 0; j < J; j++) {
                int blockw = 16, blockh = 8;
                if (j > 3) {
                    blockw = 2;
                    blockh = 2;
                }
                if (j > 0) {
                    inGPyramid[j].compute_root().gpu_tile(x, y, xi, yi, blockw, blockh);
                    gPyramid[j].compute_root().reorder(k, x, y).gpu_tile(x, y, xi, yi, blockw, blockh);
                }
                outGPyramid[j].compute_root().gpu_tile(x, y, xi, yi, blockw, blockh);
            }
        } else {
            // CPU schedule.

            // 21.4ms on an Intel i9-9960X using 32 threads at 3.7
            // GHz, using the target x86-64-avx2.

            // This app is dominated by data-dependent loads from
            // memory, so we're better off leaving the AVX-512 units
            // off in exchange for a higher clock, and we benefit from
            // hyperthreading.

            remap.compute_root();
            Var yo;
            output.reorder(c, x, y).split(y, yo, y, 64).parallel(yo).vectorize(x, 8);
            gray.compute_root().parallel(y, 32).vectorize(x, 8);
            for (int j = 1; j < 5; j++) {
                inGPyramid[j]
                    .compute_root()
                    .parallel(y, 32)
                    .vectorize(x, 8);
                gPyramid[j]
                    .compute_root()
                    .reorder_storage(x, k, y)
                    .reorder(k, y)
                    .parallel(y, 8)
                    .vectorize(x, 8);
                outGPyramid[j]
                    .store_at(output, yo)
                    .compute_at(output, y)
                    .fold_storage(y, 4)
                    .vectorize(x, 8);
            }
            outGPyramid[0].compute_at(output, y).vectorize(x, 8);
            for (int j = 5; j < J; j++) {
                inGPyramid[j].compute_root();
                gPyramid[j].compute_root().parallel(k);
                outGPyramid[j].compute_root();
            }
        }

        /* Optional tags to specify layout for HalideTraceViz */
        {
            Halide::Trace::FuncConfig cfg;
            cfg.color_dim = 2;
            cfg.max = 65535;
            cfg.pos.x = 30;
            cfg.pos.y = 100;
            input.add_trace_tag(cfg.to_trace_tag());

            cfg.pos.x = 1700;
            output.add_trace_tag(cfg.to_trace_tag());
        }

        {
            Halide::Trace::FuncConfig cfg;
            cfg.store_cost = 5;
            cfg.pos.x = 370;
            cfg.pos.y = 100;
            cfg.labels = {{"input pyramid", {-90, -68}}};
            gray.add_trace_tag(cfg.to_trace_tag());
        }

        for (int i = 0; i < pyramid_levels; ++i) {
            int y = 100;
            for (int j = 0; j < i; ++j) {
                y += 500 >> j;
            }
            {
                int x = 370;
                int store_cost = 1 << (i + 1);
                Halide::Trace::FuncConfig cfg;
                cfg.pos = {x, y};
                cfg.store_cost = store_cost;
                inGPyramid[i].add_trace_tag(cfg.to_trace_tag());
            }
            {
                int x = 720;
                int store_cost = 1 << i;
                Halide::Trace::FuncConfig cfg;
                cfg.strides = {{1, 0}, {0, 1}, {200, 0}};
                cfg.pos = {x, y};
                cfg.store_cost = store_cost;
                if (i == 1) {
                    cfg.labels = {{"differently curved intermediate pyramids"}};
                }
                gPyramid[i].add_trace_tag(cfg.to_trace_tag());
            }
            {
                int x = 1500;
                int store_cost = (1 << i) * 10;
                Halide::Trace::FuncConfig cfg;
                cfg.pos = {x, y};
                cfg.store_cost = store_cost;
                if (i == 0) {
                    cfg.labels = {{"output pyramids"}};
                    cfg.pos = {x, 100};
                }
                outGPyramid[i].add_trace_tag(cfg.to_trace_tag());
            }
        }
    }

private:
    Var x, y, c, k;

    // Downsample with a 1 3 3 1 filter
    Func downsample(Func f) {
        using Halide::_;
        Func downx, downy;
        downx(x, y, _) = (f(2 * x - 1, y, _) + 3.0f * (f(2 * x, y, _) + f(2 * x + 1, y, _)) + f(2 * x + 2, y, _)) / 8.0f;
        downy(x, y, _) = (downx(x, 2 * y - 1, _) + 3.0f * (downx(x, 2 * y, _) + downx(x, 2 * y + 1, _)) + downx(x, 2 * y + 2, _)) / 8.0f;
        return downy;
    }

    // Upsample using bilinear interpolation
    Func upsample(Func f) {
        using Halide::_;
        Func upx, upy;
        upx(x, y, _) = lerp(f((x + 1) / 2, y, _), f((x - 1) / 2, y, _), ((x % 2) * 2 + 1) / 4.0f);
        upy(x, y, _) = lerp(upx(x, (y + 1) / 2, _), upx(x, (y - 1) / 2, _), ((y % 2) * 2 + 1) / 4.0f);
        return upy;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(LocalLaplacian, local_laplacian)
