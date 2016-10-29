#include "Halide.h"

namespace {

constexpr int maxJ = 20;

class LocalLaplacianFixed : public Halide::Generator<LocalLaplacianFixed> {
public:

    GeneratorParam<int>   pyramid_levels{"pyramid_levels", 8, 1, maxJ};

    ImageParam            input{UInt(16), 3, "input"};
    Param<int>            levels{"levels"};
    Param<float>          alpha{"alpha"};
    Param<float>          beta{"beta"};

    Func build() {
        /* THE ALGORITHM */
        const int J = pyramid_levels;

        // Make the remapping function as a lookup table.
        Func remap;
        Expr fx = cast<float>(x) / 256.0f;
        remap(x) = cast<int16_t>(1024.0f * alpha * fx * exp(-fx*fx/2.0f));

        // Set a boundary condition
        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        // Convert to floating point
        Func fixed_8;
        fixed_8(x, y, c) = cast<int16_t>(clamped(x, y, c));

        // Get the luminance channel
        Func gray;
        gray(x, y) = fixed_8(x, y, 0) + 2 * fixed_8(x, y, 1) + fixed_8(x, y, 2);

        // Make the processed Gaussian pyramid.
        Func gPyramid[maxJ];
        // Do a lookup into a lut with 256 entires per intensity level
        Expr level = cast<int16_t>(( 1024 * k) / (levels - 1));
        Expr idx = cast<int>(gray(x, y))*(levels-1)/4;
        Expr beta_fixed = cast<uint8_t>(beta * 255);
        //idx = clamp(cast<int>(idx), 0, (levels-1)*256);
        gPyramid[0](x, y, k) = lerp(level, gray(x, y), beta_fixed) + remap(idx - 256*k);

        for (int j = 1; j < J; j++) {
            gPyramid[j](x, y, k) = downsample(gPyramid[j-1])(x, y, k);
        }

        // Get its laplacian pyramid
        Func lPyramid[maxJ];
        lPyramid[J-1](x, y, k) = gPyramid[J-1](x, y, k);
        for (int j = J-2; j >= 0; j--) {
            lPyramid[j](x, y, k) = gPyramid[j](x, y, k) - upsample(gPyramid[j+1])(x, y, k);
        }

        // Make the Gaussian pyramid of the input
        Func inGPyramid[maxJ];
        inGPyramid[0](x, y) = gray(x, y);
        for (int j = 1; j < J; j++) {
            inGPyramid[j](x, y) = downsample(inGPyramid[j-1])(x, y);
        }

        // Make the laplacian pyramid of the output
        Func outLPyramid[maxJ];
        for (int j = 0; j < J; j++) {
            // inGPyramid is a 10-bit value stored in an int16_t, so this shouldn't overflow if levels <= 32
            Expr level = inGPyramid[j](x, y) * (levels-1);

            // Split it into integer and fractional parts
            Expr li = clamp(level / 1024, 0, levels-2);
            Expr lf = level % 1024;

            // Turn lf into an 8-bit interpolant
            lf = cast<uint8_t>(lf / 4);

            // Linearly interpolate between the nearest processed pyramid levels
            outLPyramid[j](x, y) = lerp(lPyramid[j](x, y, li), lPyramid[j](x, y, li+1), lf);
        }

        // Make the Gaussian pyramid of the output
        Func outGPyramid[maxJ];
        outGPyramid[J-1](x, y) = outLPyramid[J-1](x, y);
        for (int j = J-2; j >= 0; j--) {
            outGPyramid[j](x, y) = upsample(outGPyramid[j+1])(x, y) + outLPyramid[j](x, y);
        }

        Func color;
        color(x, y, c) = fixed_8(x, y, c) + (outGPyramid[0](x, y) - gray(x, y)) / 4;


        // Clamp and cast back to 16-bit
        Func output;
        output(x, y, c) = cast<uint16_t>(clamp(color(x, y, c), 0, 255));

        /* THE SCHEDULE */
        remap.compute_root();

        if (get_target().has_gpu_feature()) {
            // gpu schedule
            output.compute_root().gpu_tile(x, y, 16, 8);
            for (int j = 0; j < J; j++) {
                int blockw = 16, blockh = 8;
                if (j > 3) {
                    blockw = 2;
                    blockh = 2;
                }
                if (j > 0) {
                    inGPyramid[j].compute_root().gpu_tile(x, y, blockw, blockh);
                    gPyramid[j].compute_root().reorder(k, x, y).gpu_tile(x, y, blockw, blockh);
                }
                outGPyramid[j].compute_root().gpu_tile(x, y, blockw, blockh);
            }
        } else if (get_target().features_any_of({Target::HVX_64, Target::HVX_128})) {
            // cpu schedule
            Var yi;
            int vec_lanes_16 = get_target().natural_vector_size(UInt(16));
            int vec_lanes_8  = get_target().natural_vector_size(UInt(8));
            output.parallel(y, 32).vectorize(x, vec_lanes_8);
            gray.compute_root().parallel(y, 32).vectorize(x, vec_lanes_16);
            for (int j = 0; j < 4; j++) {
                if (j > 0) {
                    inGPyramid[j]
                        .compute_root().parallel(y, 32).vectorize(x, vec_lanes_16);
                    gPyramid[j]
                        .compute_root().reorder_storage(x, k, y)
                        .reorder(k, y).parallel(y, 8).vectorize(x, vec_lanes_16);
                }
                outGPyramid[j].compute_root().parallel(y, 32).vectorize(x, vec_lanes_16);
            }
            for (int j = 4; j < J; j++) {
                inGPyramid[j].compute_root().hexagon();
                gPyramid[j].compute_root().hexagon().parallel(k);
                outGPyramid[j].compute_root().hexagon();
            }

        } else {
            // cpu schedule
            Var yo;
            output.reorder(c, x, y).split(y, yo, y, 64).parallel(yo).vectorize(x, 8);
            gray.compute_root().parallel(y, 32).vectorize(x, 8);
            for (int j = 1; j < 5; j++) {
                inGPyramid[j]
                    .compute_root().parallel(y, 32).vectorize(x, 8);
                gPyramid[j]
                    .compute_root().reorder_storage(x, k, y)
                    .reorder(k, y).parallel(y, 8).vectorize(x, 8);
                outGPyramid[j]
                    .store_at(output, yo).compute_at(output, y)
                    .vectorize(x, 8);
            }
            outGPyramid[0]
                .compute_at(output, y).vectorize(x, 8);
            for (int j = 5; j < J; j++) {
                inGPyramid[j].compute_root();
                gPyramid[j].compute_root().parallel(k);
                outGPyramid[j].compute_root();
            }
        }

        return output;
    }
private:
    Var x, y, c, k;

    // Downsample with a 1 3 3 1 filter
    Func downsample(Func f) {
        using Halide::_;
        Func downx, downy;
        downx(x, y, _) = (f(2*x-1, y, _) + 3 * (f(2*x, y, _) + f(2*x+1, y, _)) + f(2*x+2, y, _)) / 8;
        downy(x, y, _) = (downx(x, 2*y-1, _) + 3 * (downx(x, 2*y, _) + downx(x, 2*y+1, _)) + downx(x, 2*y+2, _)) / 8;
        return downy;
    }

    // Upsample using bilinear interpolation
    Func upsample(Func f) {
        using Halide::_;
        Func upx, upy;
        upx(x, y, _) = (f((x/2) - 1 + 2*(x % 2), y, _) + 3 * f(x/2, y, _)) / 4;
        upy(x, y, _) = (upx(x, (y/2) - 1 + 2*(y % 2), _) + 3 * upx(x, y/2, _)) / 4;
        return upy;
    }


};

Halide::RegisterGenerator<LocalLaplacianFixed> register_me{"local_laplacian_fixed"};

}  // namespace
