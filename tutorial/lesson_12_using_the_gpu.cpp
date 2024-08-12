// Halide tutorial lesson 12: Using the GPU

// This lesson demonstrates how to use Halide to run code on a GPU using OpenCL.

// On linux, you can compile and run it like so:
// g++ lesson_12*.cpp -g -std=c++17 -I <path/to/Halide.h> -I <path/to/tools/halide_image_io.h> -L <path/to/libHalide.so> -lHalide `libpng-config --cflags --ldflags` -ljpeg -lpthread -ldl -o lesson_12
// LD_LIBRARY_PATH=<path/to/libHalide.so> ./lesson_12

// On os x:
// g++ lesson_12*.cpp -g -std=c++17 -I <path/to/Halide.h> -I <path/to/tools/halide_image_io.h> -L <path/to/libHalide.so> -lHalide `libpng-config --cflags --ldflags` -ljpeg -o lesson_12
// DYLD_LIBRARY_PATH=<path/to/libHalide.dylib> ./lesson_12

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_12_using_the_gpu
// in a shell with the current directory at the top of the halide
// source tree.

#include <stdio.h>

#include "Halide.h"

// Include a clock to do performance testing.
#include "clock.h"

// Include some support code for loading pngs.
#include "halide_image_io.h"

using namespace Halide;
using namespace Halide::Tools;

Target find_gpu_target();

// Define some Vars to use.
Var x, y, c, i, ii, xo, yo, xi, yi;

// We're going to want to schedule a pipeline in several ways, so we
// define the pipeline in a class so that we can recreate it several
// times with different schedules.
class MyPipeline {
public:
    Func lut, padded, padded16, sharpen, curved;
    Buffer<uint8_t> input;

    MyPipeline(Buffer<uint8_t> in)
        : input(in) {
        // For this lesson, we'll use a two-stage pipeline that sharpens
        // and then applies a look-up-table (LUT).

        // First we'll define the LUT. It will be a gamma curve.

        lut(i) = cast<uint8_t>(clamp(pow(i / 255.0f, 1.2f) * 255.0f, 0, 255));

        // Augment the input with a boundary condition.
        padded(x, y, c) = input(clamp(x, 0, input.width() - 1),
                                clamp(y, 0, input.height() - 1), c);

        // Cast it to 16-bit to do the math.
        padded16(x, y, c) = cast<uint16_t>(padded(x, y, c));

        // Next we sharpen it with a five-tap filter.
        sharpen(x, y, c) = (padded16(x, y, c) * 2 -
                            (padded16(x - 1, y, c) +
                             padded16(x, y - 1, c) +
                             padded16(x + 1, y, c) +
                             padded16(x, y + 1, c)) /
                                4);

        // Then apply the LUT.
        curved(x, y, c) = lut(sharpen(x, y, c));
    }

    // Now we define methods that give our pipeline several different
    // schedules.
    void schedule_for_cpu() {
        // Compute the look-up-table ahead of time.
        lut.compute_root();

        // Compute color channels innermost. Promise that there will
        // be three of them and unroll across them.
        curved.reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c);

        // Look-up-tables don't vectorize well, so just parallelize
        // curved in slices of 16 scanlines.
        Var yo, yi;
        curved.split(y, yo, yi, 16)
            .parallel(yo);

        // Compute sharpen as needed per scanline of curved.
        sharpen.compute_at(curved, yi);

        // Vectorize the sharpen. It's 16-bit so we'll vectorize it 8-wide.
        sharpen.vectorize(x, 8);

        // Compute the padded input as needed per scanline of curved,
        // reusing previous values computed within the same strip of
        // 16 scanlines.
        padded.store_at(curved, yo)
            .compute_at(curved, yi);

        // Also vectorize the padding. It's 8-bit, so we'll vectorize
        // 16-wide.
        padded.vectorize(x, 16);

        // JIT-compile the pipeline for the CPU.
        Target target = get_host_target();
        curved.compile_jit(target);
    }

    // Now a schedule that uses CUDA or OpenCL.
    bool schedule_for_gpu() {
        Target target = find_gpu_target();
        if (!target.has_gpu_feature()) {
            return false;
        }

        // If you want to see all of the OpenCL, Metal, CUDA or D3D 12 API
        // calls done by the pipeline, you can also enable the Debug flag.
        // This is helpful for figuring out which stages are slow, or when
        // CPU -> GPU copies happen. It hurts performance though, so we'll
        // leave it commented out.
        //target.set_feature(Target::Debug);

        // We make the decision about whether to use the GPU for each
        // Func independently. If you have one Func computed on the
        // CPU, and the next computed on the GPU, Halide will do the
        // copy-to-gpu under the hood. For this pipeline, there's no
        // reason to use the CPU for any of the stages. Halide will
        // copy the input image to the GPU the first time we run the
        // pipeline, and leave it there to reuse on subsequent runs.

        // As before, we'll compute the LUT once at the start of the
        // pipeline.
        lut.compute_root();

        // Let's compute the look-up-table using the GPU in 16-wide
        // one-dimensional thread blocks. First we split the index
        // into blocks of size 16:
        Var block, thread;
        lut.split(i, block, thread, 16);
        // Then we tell cuda that our Vars 'block' and 'thread'
        // correspond to CUDA's notions of blocks and threads, or
        // OpenCL's notions of thread groups and threads.
        lut.gpu_blocks(block)
            .gpu_threads(thread);

        // This is a very common scheduling pattern on the GPU, so
        // there's a shorthand for it:

        // lut.gpu_tile(i, block, thread, 16);

        // Func::gpu_tile behaves the same as Func::tile, except that
        // it also specifies that the tile coordinates correspond to
        // GPU blocks, and the coordinates within each tile correspond
        // to GPU threads.

        // Compute color channels innermost. Promise that there will
        // be three of them and unroll across them.
        curved.reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c);

        // Compute curved in 2D 8x8 tiles using the GPU.
        curved.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);

        // This is equivalent to:
        // curved.tile(x, y, xo, yo, xi, yi, 8, 8)
        //       .gpu_blocks(xo, yo)
        //       .gpu_threads(xi, yi);

        // We'll leave sharpen as inlined into curved.

        // Compute the padded input as needed per GPU block, storing
        // the intermediate result in shared memory. In the schedule
        // above xo corresponds to GPU blocks.
        padded.compute_at(curved, xo);

        // Use the GPU threads for the x and y coordinates of the
        // padded input.
        padded.gpu_threads(x, y);

        // JIT-compile the pipeline for the GPU. CUDA, OpenCL, or
        // Metal are not enabled by default. We have to construct a
        // Target object, enable one of them, and then pass that
        // target object to compile_jit. Otherwise your CPU will very
        // slowly pretend it's a GPU, and use one thread per output
        // pixel.
        printf("Target: %s\n", target.to_string().c_str());
        curved.compile_jit(target);

        return true;
    }

    void test_performance() {
        // Test the performance of the scheduled MyPipeline.

        Buffer<uint8_t> output(input.width(), input.height(), input.channels());

        // Run the filter once to initialize any GPU runtime state.
        curved.realize(output);

        // Now take the best of 3 runs for timing.
        double best_time = 0.0;
        for (int i = 0; i < 3; i++) {

            double t1 = current_time();

            // Run the filter 100 times.
            for (int j = 0; j < 100; j++) {
                curved.realize(output);
            }

            // Force any GPU code to finish by copying the buffer back to the CPU.
            output.copy_to_host();

            double t2 = current_time();

            double elapsed = (t2 - t1) / 100;
            if (i == 0 || elapsed < best_time) {
                best_time = elapsed;
            }
        }

        printf("%1.4f milliseconds\n", best_time);
    }

    void test_correctness(Buffer<uint8_t> reference_output) {
        Buffer<uint8_t> output =
            curved.realize({input.width(), input.height(), input.channels()});

        // Check against the reference output.
        for (int c = 0; c < input.channels(); c++) {
            for (int y = 0; y < input.height(); y++) {
                for (int x = 0; x < input.width(); x++) {
                    if (output(x, y, c) != reference_output(x, y, c)) {
                        printf("Mismatch between output (%d) and "
                               "reference output (%d) at %d, %d, %d\n",
                               output(x, y, c),
                               reference_output(x, y, c),
                               x, y, c);
                        exit(1);
                    }
                }
            }
        }
    }
};

int main(int argc, char **argv) {
    // Load an input image.
    Buffer<uint8_t> input = load_image("images/rgb.png");

    // Allocated an image that will store the correct output
    Buffer<uint8_t> reference_output(input.width(), input.height(), input.channels());

    printf("Running pipeline on CPU:\n");
    MyPipeline p1(input);
    p1.schedule_for_cpu();
    p1.curved.realize(reference_output);

    printf("Running pipeline on GPU:\n");
    MyPipeline p2(input);
    bool has_gpu_target = p2.schedule_for_gpu();
    if (has_gpu_target) {
        printf("Testing GPU correctness:\n");
        p2.test_correctness(reference_output);
    } else {
        printf("No GPU target available on the host\n");
    }

    printf("Testing performance on CPU:\n");
    p1.test_performance();

    if (has_gpu_target) {
        printf("Testing performance on GPU:\n");
        p2.test_performance();
    }

    return 0;
}

// A helper function to check if OpenCL, Metal or D3D12 is present on the host machine.

Target find_gpu_target() {
    // Start with a target suitable for the machine you're running this on.
    Target target = get_host_target();

    std::vector<Target::Feature> features_to_try;
    if (target.os == Target::Windows) {
        // Try D3D12 first; if that fails, try OpenCL.
        if (sizeof(void*) == 8) {
            // D3D12Compute support is only available on 64-bit systems at present.
            features_to_try.push_back(Target::D3D12Compute);
        }
        features_to_try.push_back(Target::OpenCL);
    } else if (target.os == Target::OSX) {
        // OS X doesn't update its OpenCL drivers, so they tend to be broken.
        // CUDA would also be a fine choice on machines with NVidia GPUs.
        features_to_try.push_back(Target::Metal);
    } else {
        features_to_try.push_back(Target::OpenCL);
    }
    // Uncomment the following lines to also try CUDA:
    // features_to_try.push_back(Target::CUDA);

    for (Target::Feature f : features_to_try) {
        Target new_target = target.with_feature(f);
        if (host_supports_target_device(new_target)) {
            return new_target;
        }
    }

    printf("Requested GPU(s) are not supported. (Do you have the proper hardware and/or driver installed?)\n");
    return target;
}
