#include "Halide.h"
#ifdef HEXAGON
#include "halide-hexagon-setup.h"
#endif

using namespace Halide;

Var x, y;

// Downsample with a 1 3 3 1 filter. Assumes the type has three bits of overhead available.
Func downsample(Func f) {
    Func downx, downy;

    downx(x, y, _) = (f(2*x-1, y, _) + 3 * (f(2*x, y, _) + f(2*x+1, y, _)) + f(2*x+2, y, _)) / 8;
    downy(x, y, _) = (downx(x, 2*y-1, _) + 3 * (downx(x, 2*y, _) + downx(x, 2*y+1, _)) + downx(x, 2*y+2, _)) / 8;

    return downy;
}

// Upsample using bilinear interpolation. Assumes 2 bits of overhead available
Func upsample(Func f) {
    Func upx, upy;

    upx(x, y, _) = (f((x/2) - 1 + 2*(x % 2), y, _) + 3 * f(x/2, y, _)) / 4;
    upy(x, y, _) = (upx(x, (y/2) - 1 + 2*(y % 2), _) + 3 * upx(x, y/2, _)) / 4;

    return upy;

}

int main(int argc, char **argv) {
#ifdef HEXAGON
    Target target;// = get_target_from_environment();
    setupHexagonTarget(target, LOG2VLEN==7 ? Target::HVX_128 : Target::HVX_64);
    commonPerfSetup(target);
#else
    Target target = get_target_from_environment();
#endif
    
    /* THE ALGORITHM */

    // Number of pyramid levels
    int J = 8;
    if (argc > 1) J = atoi(argv[1]);
    const int maxJ = 20;

    // number of intensity levels
    Param<int> levels;
    // Parameters controlling the filter
    Param<float> alpha, beta;
    // Takes an 8-bit input
    ImageParam input(UInt(8), 3);

    // loop variables
    Var c, k;

    // Make the remapping function as a lookup table. Returns a 10-bit signed fixed-point value stored in an int16_t
    Func remap;
    Expr fx = cast<float>(x) / 256.0f;
    remap(x) = cast<int16_t>(1024.0f * alpha * fx * exp(-fx*fx/2.0f));

    // Set a boundary condition
    Func clamped = BoundaryConditions::repeat_edge(input);

    // Convert to int16, with 8 bits occupied
    Func fixed_8;
    fixed_8(x, y, c) = cast<int16_t>(clamped(x, y, c));

    // Get the luminance channel. Now a 10-bit unsigned value stored in an int16
    Func gray;
    gray(x, y) = fixed_8(x, y, 0) + 2 * fixed_8(x, y, 1) + fixed_8(x, y, 2);

    // Make the processed Gaussian pyramid.
    Func gPyramid[maxJ];
    // Do a lookup into a lut with 256 entires per intensity level
    Expr level = cast<int16_t>((1024 * k) / (levels - 1));
    Expr idx = (cast<int>(gray(x, y))*(levels-1))/4;
    Expr beta_fixed = cast<uint8_t>(beta * 255);
    // gray was between 0 and 1023, so idx is between 0 and 256*(levels-1)
    //idx = clamp(idx, 0, (levels-1)*256);
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

    // Reintroduce color
    Func color;
    color(x, y, c) = fixed_8(x, y, c) + (outGPyramid[0](x, y) - gray(x, y)) / 4;

    // Clamp and cast back to 8-bit
    Func output;
    output(x, y, c) = cast<uint8_t>(clamp(color(x, y, c), 0, 255));

    /* THE SCHEDULE */
    remap.compute_root();

    if (target.has_gpu_feature()) {
        // gpu schedule
        output.compute_root().gpu_tile(x, y, 16, 8, DeviceAPI::Default_GPU);
        for (int j = 0; j < J; j++) {
            int blockw = 16, blockh = 8;
            if (j > 3) {
                blockw = 2;
                blockh = 2;
            }
            if (j > 0) {
                inGPyramid[j].compute_root().gpu_tile(x, y, blockw, blockh, DeviceAPI::Default_GPU);
                gPyramid[j].compute_root().reorder(k, x, y).gpu_tile(x, y, blockw, blockh, DeviceAPI::Default_GPU);
            }
            outGPyramid[j].compute_root().gpu_tile(x, y, blockw, blockh, DeviceAPI::Default_GPU);
        }
    } else {
#ifndef SCHEDULE
#error "Schedule not defined."
#endif
#if (SCHEDULE == 0)
        // cpu schedule
        Var yi;
        output.parallel(y, 32).vectorize(x, 8);
        gray.compute_root().parallel(y, 32).vectorize(x, 8);
        for (int j = 0; j < 4; j++) {
            if (j > 0) {
                inGPyramid[j]
                    .compute_root().parallel(y, 32).vectorize(x, 8);
                gPyramid[j]
                    .compute_root().reorder_storage(x, k, y)
                    .reorder(k, y).parallel(y, 8).vectorize(x, 8);
            }
            outGPyramid[j].compute_root().parallel(y, 32).vectorize(x, 8);
        }
        for (int j = 4; j < J; j++) {
            inGPyramid[j].compute_root();
            gPyramid[j].compute_root().parallel(k);
            outGPyramid[j].compute_root();
        }
#elif (SCHEDULE == 1)
        // Scalar schedule
        gray.compute_root();
        for (int j = 0; j < J; j++) {
            if (j > 0) {
                inGPyramid[j].compute_root();
                gPyramid[j].compute_root();
            }
            outGPyramid[j].compute_root();
        }
#elif (SCHEDULE == 2)
        // Non-vector parallel schedule
        output.parallel(y, 32);
        gray.compute_root().parallel(y, 32);
        for (int j = 0; j < 4; j++) {
            if (j > 0) {
                inGPyramid[j]
                    .compute_root().parallel(y, 32);
                gPyramid[j]
                    .compute_root().reorder_storage(x, k, y)
                    .reorder(k, y).parallel(y, 8);
            }
            outGPyramid[j].compute_root().parallel(y, 32);
        }
        for (int j = 4; j < J; j++) {
            inGPyramid[j].compute_root();
            gPyramid[j].compute_root().parallel(k);
            outGPyramid[j].compute_root();
        }
#elif (SCHEDULE == 3)
        // HVX vector schedule
        Var yi;
        int vec_lanes_16 = target.natural_vector_size<uint16_t>();
        int vec_lanes_8  = target.natural_vector_size<uint8_t>();
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
            inGPyramid[j].compute_root();
            gPyramid[j].compute_root().parallel(k);
            outGPyramid[j].compute_root();
        }
#else
#error "Schedule out of range."
#endif
    }

//  output.compile_to_bitcode("ll.bc",  {levels, alpha, beta, input}, target);

    output.compile_to_file("local_laplacian", {levels, alpha, beta, input}, target);

    return 0;
}
