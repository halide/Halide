#!/usr/bin/python3

# Halide tutorial lesson 12.

# This lesson demonstrates how to use Halide to run code on a GPU.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_12_using_the_gpu
# in a shell with the current directory at python_bindings/

import halide as hl

import halide.imageio
import os.path
import struct

# Include a clock to do performance testing.
from datetime import datetime

# Define some Vars to use.
x, y, c, i, xo, yo, xi, yi = hl.Var("x"), hl.Var("y"), hl.Var("c"), hl.Var(
    "i"), hl.Var("xo"), hl.Var("yo"), hl.Var("xi"), hl.Var("yi")

# We're going to want to schedule a pipeline in several ways, so we
# define the pipeline in a class so that we can recreate it several
# times with different schedules.


class MyPipeline:

    def __init__(self, input):

        assert input.type() == hl.UInt(8)

        self.lut = hl.Func("lut")
        self.padded = hl.Func("padded")
        self.padded16 = hl.Func("padded16")
        self.sharpen = hl.Func("sharpen")
        self.curved = hl.Func("curved")
        self.input = input

        # For this lesson, we'll use a two-stage pipeline that sharpens
        # and then applies a look-up-table (LUT).

        # First we'll define the LUT. It will be a gamma curve.
        gamma = hl.f32(1.2)
        self.lut[i] = hl.u8(hl.clamp(hl.pow(i / 255.0, gamma) * 255.0, 0, 255))

        # Augment the input with a boundary condition.
        self.padded[x, y, c] = input[hl.clamp(x, 0, input.width() - 1),
                                     hl.clamp(y, 0, input.height() - 1), c]

        # Cast it to 16-bit to do the math.
        self.padded16[x, y, c] = hl.u16(self.padded[x, y, c])

        # Next we sharpen it with a five-tap filter.
        self.sharpen[x, y, c] = (self.padded16[x, y, c] * 2 -
                                 (self.padded16[x - 1, y, c] +
                                  self.padded16[x, y - 1, c] +
                                  self.padded16[x + 1, y, c] +
                                  self.padded16[x, y + 1, c]) / 4)

        # Then apply the LUT.
        self.curved[x, y, c] = self.lut[self.sharpen[x, y, c]]

    # Now we define methods that give our pipeline several different
    # schedules.
    def schedule_for_cpu(self):
        # Compute the look-up-table ahead of time.
        self.lut.compute_root()

        # Compute color channels innermost. Promise that there will
        # be three of them and unroll across them.
        self.curved.reorder(c, x, y) \
            .bound(c, 0, 3) \
            .unroll(c)

        # Look-up-tables don't vectorize well, so just parallelize
        # curved in slices of 16 scanlines.
        self.curved.split(y, yo, yi, 16) \
            .parallel(yo)

        # Compute sharpen as needed per scanline of curved, reusing
        # previous values computed within the same strip of 16
        # scanlines.
        self.sharpen.compute_at(self.curved, yi)

        # Vectorize the sharpen. It's 16-bit so we'll vectorize it 8-wide.
        self.sharpen.vectorize(x, 8)

        # Compute the padded input at the same granularity as the
        # sharpen. We'll leave the cast to 16-bit inlined into
        # sharpen.
        self.padded.store_at(self.curved, yo) \
            .compute_at(self.curved, yi)

        # Also vectorize the padding. It's 8-bit, so we'll vectorize
        # 16-wide.
        self.padded.vectorize(x, 16)

        # JIT-compile the pipeline for the CPU.
        target = hl.get_host_target()
        self.curved.compile_jit(target)

        return

    # Now a schedule that uses CUDA or OpenCL.
    def schedule_for_gpu(self):
        target = find_gpu_target()
        if not target.has_gpu_feature():
            return False

        # If you want to see all of the OpenCL, Metal, CUDA or D3D 12 API
        # calls done by the pipeline, you can also enable the Debug flag.
        # This is helpful for figuring out which stages are slow, or when
        # CPU -> GPU copies happen. It hurts performance though, so we'll
        # leave it commented out.
        # target.set_feature(hl.TargetFeature.Debug)

        # We make the decision about whether to use the GPU for each
        # hl.Func independently. If you have one hl.Func computed on the
        # CPU, and the next computed on the GPU, Halide will do the
        # copy-to-gpu under the hood. For this pipeline, there's no
        # reason to use the CPU for any of the stages. Halide will
        # copy the input image to the GPU the first time we run the
        # pipeline, and leave it there to reuse on subsequent runs.

        # As before, we'll compute the LUT once at the start of the
        # pipeline.
        self.lut.compute_root()

        # Let's compute the look-up-table using the GPU in 16-wide
        # one-dimensional thread blocks. First we split the index
        # into blocks of size 16:
        block, thread = hl.Var("block"), hl.Var("thread")
        self.lut.split(i, block, thread, 16)
        # Then we tell cuda that our Vars 'block' and 'thread'
        # correspond to CUDA's notions of blocks and threads, or
        # OpenCL's notions of thread groups and threads.
        self.lut.gpu_blocks(block) \
                .gpu_threads(thread)

        # This is a very common scheduling pattern on the GPU, so
        # there's a shorthand for it:

        # lut.gpu_tile(i, block, thread, 16);

        # hl.Func.gpu_tile behaves the same as hl.Func.tile, except that
        # it also specifies that the tile coordinates correspond to
        # GPU blocks, and the coordinates within each tile correspond
        # to GPU threads.

        # Compute color channels innermost. Promise that there will
        # be three of them and unroll across them.
        self.curved.reorder(c, x, y) \
                   .bound(c, 0, 3) \
                   .unroll(c)

        # Compute curved in 2D 8x8 tiles using the GPU.
        self.curved.gpu_tile(x, y, xo, yo, xi, yi, 8, 8)

        # This is equivalent to:
        # curved.tile(x, y, xo, yo, xi, yi, 8, 8)
        #       .gpu_blocks(xo, yo)
        #       .gpu_threads(xi, yi)

        # We'll leave sharpen as inlined into curved.

        # Compute the padded input as needed per GPU block, storing
        # the intermediate result in shared memory. In the schedule
        # above xo corresponds to GPU blocks.
        self.padded.compute_at(self.curved, xo)

        # Use the GPU threads for the x and y coordinates of the
        # padded input.
        self.padded.gpu_threads(x, y)

        # JIT-compile the pipeline for the GPU. CUDA, OpenCL, or
        # Metal are not enabled by default. We have to construct a
        # Target object, enable one of them, and then pass that
        # target object to compile_jit. Otherwise your CPU will very
        # slowly pretend it's a GPU, and use one thread per output
        # pixel.

        print("Target: ", target)
        self.curved.compile_jit(target)
        return True

    def test_performance(self):
        # Test the performance of the scheduled MyPipeline.

        output = hl.Buffer(hl.UInt(8),
                           [self.input.width(), self.input.height(), self.input.channels()])

        # Run the filter once to initialize any GPU runtime state.
        self.curved.realize(output)

        # Now take the best of 3 runs for timing.
        best_time = float("inf")
        for i in range(3):

            t1 = datetime.now()

            # Run the filter 100 times.
            for j in range(100):
                self.curved.realize(output)

            # Force any GPU code to finish by copying the buffer back to the
            # CPU.
            output.copy_to_host()

            t2 = datetime.now()

            elapsed = (t2 - t1).total_seconds()
            if elapsed < best_time:
                best_time = elapsed
        # end of "best of three times"

        print("%1.4f milliseconds" % (best_time * 1000))

    def test_correctness(self, reference_output):

        assert reference_output.type() == hl.UInt(8)
        output = self.curved.realize([self.input.width(),
                                      self.input.height(),
                                      self.input.channels()])
        assert output.type() == hl.UInt(8)

        # Check against the reference output.
        for cc in range(self.input.channels()):
            for yy in range(self.input.height()):
                for xx in range(self.input.width()):
                    assert output[xx, yy, cc] == reference_output[xx, yy, cc], \
                        "Mismatch between output (%d) and reference output (%d) at %d, %d, %d" % (
                            output[xx, yy, cc], reference_output[xx, yy, cc], xx, yy, cc)

        print("CPU and GPU outputs are consistent.")


def main():
    # Load an input image.
    image_path = os.path.join(os.path.dirname(__file__), "../../tutorial/images/rgb.png")
    input = hl.Buffer(halide.imageio.imread(image_path))

    # Allocated an image that will store the correct output
    reference_output = hl.Buffer(hl.UInt(8), [input.width(), input.height(), input.channels()])

    print("Running pipeline on CPU:")
    p1 = MyPipeline(input)
    p1.schedule_for_cpu()
    p1.curved.realize(reference_output)

    print("Running pipeline on GPU:")
    p2 = MyPipeline(input)
    has_gpu_target = p2.schedule_for_gpu()
    if has_gpu_target:
        print("Testing GPU correctness:")
        p2.test_correctness(reference_output)
    else:
        print("No GPU target available on the host")

    print("Testing performance on CPU:")
    p1.test_performance()

    if has_gpu_target:
        print("Testing performance on GPU:")
        p2.test_performance()

    return 0

# A helper function to check if OpenCL, Metal or D3D12 is present on the
# host machine.


def find_gpu_target():
    # Start with a target suitable for the machine you're running this on.
    target = hl.get_host_target()

    features_to_try = []
    if target.os == hl.TargetOS.Windows:
        # Try D3D12 first; if that fails, try OpenCL.
        if struct.calcsize("P") == 8:
            # D3D12Compute support is only available on 64-bit systems at present.
            features_to_try.append(hl.TargetFeature.D3D12Compute)
        features_to_try.append(hl.TargetFeature.OpenCL)
    elif target.os == hl.TargetOS.OSX:
        # OS X doesn't update its OpenCL drivers, so they tend to be broken.
        # CUDA would also be a fine choice on machines with NVidia GPUs.
        features_to_try.append(hl.TargetFeature.Metal)
    else:
        features_to_try.append(hl.TargetFeature.OpenCL)

    # Uncomment the following lines to also try CUDA:
    # features_to_try.append(hl.TargetFeature.CUDA);
    for f in features_to_try:
        new_target = target.with_feature(f)
        if (hl.host_supports_target_device(new_target)):
            return new_target

    print("Requested GPU(s) are not supported. (Do you have the proper hardware and/or driver installed?)")
    return target


if __name__ == "__main__":
    main()
