#!/usr/bin/python3

# Halide tutorial lesson 12.

# This lesson demonstrates how to use Halide to run code on a GPU.

# This lesson can be built by invoking the command:
#    make tutorial_lesson_12_using_the_gpu
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_12*.cpp -g -std=c++11 -I ../include -L ../bin -lHalide `libpng-config --cflags --ldflags` -lpthread -ldl -o lesson_12
# LD_LIBRARY_PATH=../bin ./lesson_12

# On os x:
# g++ lesson_12*.cpp -g -std=c++11 -I ../include -L ../bin -lHalide `libpng-config --cflags --ldflags` -o lesson_12
# DYLD_LIBRARY_PATH=../bin ./lesson_12

#include "Halide.h"
#include <stdio.h>
#using namespace Halide
import halide as hl

# Include some support code for loading pngs.
#include "image_io.h"
from scipy.misc import imread
import os.path

# Include a clock to do performance testing.
#include "clock.h"
from datetime import datetime


# Define some Vars to use.
x, y, c, i, ii, xi, yi = hl.Var("x"), hl.Var("y"), hl.Var("c"), hl.Var("i"), hl.Var("ii"), hl.Var("xi"), hl.Var("yi")

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
        self.lut[i] = hl.cast(hl.UInt(8), hl.clamp(pow(i / 255.0, 1.2) * 255.0, 0, 255))

        # Augment the input with a boundary condition.
        self.padded[x, y, c] = input[hl.clamp(x, 0, input.width()-1),
                                hl.clamp(y, 0, input.height()-1), c]

        # Cast it to 16-bit to do the math.
        self.padded16[x, y, c] = hl.cast(hl.UInt(16), self.padded[x, y, c])

        # Next we sharpen it with a five-tap filter.
        self.sharpen[x, y, c] = (self.padded16[x, y, c] * 2-
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
        yo, yi = hl.Var("yo"), hl.Var("yi")
        self.curved.split(y, yo, yi, 16) \
              .parallel(yo)

        # Compute sharpen as needed per scanline of curved, reusing
        # previous values computed within the same strip of 16
        # scanlines.
        self.sharpen.store_at(self.curved, yo) \
                    .compute_at(self.curved, yi)

        # Vectorize the sharpen. It's 16-bit so we'll vectorize it 8-wide.
        self.sharpen.vectorize(x, 8)

        # Compute the padded input at the same granularity as the
        # sharpen. We'll leave the hl.cast to 16-bit inlined into
        # sharpen.
        self.padded.store_at(self.curved, yo) \
            .compute_at(self.curved, yi)

        # Also vectorize the padding. It's 8-bit, so we'll vectorize
        # 16-wide.
        self.padded.vectorize(x, 16)

        # JIT-compile the pipeline for the CPU.
        self.curved.compile_jit()

        return


    # Now a schedule that uses CUDA or OpenCL.
    def schedule_for_gpu(self):
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

        # lut.gpu_tile(i, ii, 16)

        # hl.Func::gpu_tile method is similar to hl.Func::tile, except that
        # it also specifies that the tile coordinates correspond to
        # GPU blocks, and the coordinates within each tile correspond
        # to GPU threads.

        # Compute color channels innermost. Promise that there will
        # be three of them and unroll across them.
        self.curved.reorder(c, x, y) \
                   .bound(c, 0, 3) \
                   .unroll(c)

        # Compute curved in 2D 8x8 tiles using the GPU.
        self.curved.gpu_tile(x, y, xi, yi, 8, 8)

        # This is equivalent to:
        # curved.tile(x, y, xo, yo, xi, yi, 8, 8)
        #       .gpu_blocks(xo, yo)
        #       .gpu_threads(xi, yi)

        # We'll leave sharpen as inlined into curved.

        # Compute the padded input as needed per GPU block, storing the
        # intermediate result in shared memory. hl.Var::gpu_blocks, and
        # hl.Var::gpu_threads exist to help you schedule producers within
        # GPU threads and blocks.
        self.padded.compute_at(self.curved, x)

        # Use the GPU threads for the x and y coordinates of the
        # padded input.
        self.padded.gpu_threads(x, y)

        # JIT-compile the pipeline for the GPU. CUDA or OpenCL are
        # not enabled by default. We have to construct a hl.Target
        # object, enable one of them, and then pass that target
        # object to compile_jit. Otherwise your CPU will very slowly
        # pretend it's a GPU, and use one thread per output pixel.

        # Start with a target suitable for the machine you're running
        # this on.
        target = hl.get_host_target()

        # Then enable OpenCL or CUDA.
        #use_opencl = False
        use_opencl = True
        if use_opencl:
            # We'll enable OpenCL here, because it tends to give better
            # performance than CUDA, even with NVidia's drivers, because
            # NVidia's open source LLVM backend doesn't seem to do all
            # the same optimizations their proprietary compiler does.
            target.set_feature(hl.TargetFeature.OpenCL)
            print("(Using OpenCL)")
        else:
            # Uncomment the next line and comment out the line above to
            # try CUDA instead.
            target.set_feature(hl.TargetFeature.CUDA)
            print("(Using CUDA)")

        # If you want to see all of the OpenCL or CUDA API calls done
        # by the pipeline, you can also enable the Debug
        # flag. This is helpful for figuring out which stages are
        # slow, or when CPU -> GPU copies happen. It hurts
        # performance though, so we'll leave it commented out.
        # target.set_feature(hl.TargetFeature.Debug)

        self.curved.compile_jit(target)

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

            # Force any GPU code to finish by copying the buffer back to the CPU.
            output.copy_to_host()

            t2 = datetime.now()

            elapsed = (t2 - t1).total_seconds()
            if elapsed < best_time:
                best_time = elapsed
        # end of "best of three times"

        print("%1.4f milliseconds" % (best_time * 1000))

    def test_correctness(self, reference_output):

        assert reference_output.type() == hl.UInt(8)
        output = self.curved.realize(self.input.width(),
                                     self.input.height(),
                                     self.input.channels())
        assert output.type() == hl.UInt(8)

        # Check against the reference output.
        for c in range(self.input.channels()):
            for y in range(self.input.height()):
                for x in range(self.input.width()):
                    if output[x, y, c] != reference_output[x, y, c]:
                        print(
                            "Mismatch between output (%d) and "
                            "reference output (%d) at %d, %d, %d" % (
                                output[x, y, c],
                                reference_output[x, y, c],
                                x, y, c))
                        return

        print("CPU and GPU outputs are consistent.")


def main():
    # Load an input image.
    image_path = os.path.join(os.path.dirname(__file__), "../../tutorial/images/rgb.png")
    input = hl.Buffer(imread(image_path))

    # Allocated an image that will store the correct output
    reference_output = hl.Buffer(hl.UInt(8), [input.width(), input.height(), input.channels()])

    print("Testing performance on CPU:")
    p1 = MyPipeline(input)
    p1.schedule_for_cpu()
    p1.test_performance()
    p1.curved.realize(reference_output)

    if have_opencl():
        print("Testing performance on GPU:")
        p2 = MyPipeline(input)
        p2.schedule_for_gpu()
        p2.test_performance()
        p2.test_correctness(reference_output)
    else:
        print("Not testing performance on GPU, "
              "because I can't find the opencl library")

    return 0



def have_opencl():
    """
    A helper function to check if OpenCL seems to exist on this machine.
    :return: bool
    """

    import ctypes
    import platform

    try:
        if platform.system() == "Windows":
            ret = ctypes.windll.LoadLibrary("OpenCL.dll") != None
        elif platform.system() == "Darwin": # apple
            ret =  ctypes.cdll.LoadLibrary("/System/Library/Frameworks/OpenCL.framework/Versions/Current/OpenCL") != None
        elif platform.system() == "Linux":
            ret = ctypes.cdll.LoadLibrary("libOpenCL.so") != None
        else:
            raise Exception("Cannot check for opencl presence "
                            "on unknown system '%s'" % platform.system())
    except OSError:
            ret = False

    return ret


if __name__ == "__main__":
    main()
