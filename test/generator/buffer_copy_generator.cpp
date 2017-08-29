#include "Halide.h"

using namespace Halide;


class BufferCopy : public Halide::Generator<BufferCopy> {
public:
    Input<Func> input {"input", Float(32), 2 };

    Output<Func> output {"output", Float(32), 2 };

    Func dev_1, host_1, dev_2;
    Var x, y;

    Expr check_eq(Expr a, Expr b, const char *name) {
        return require(a == b, a, "!=", b, "@", name, "(", x, ",", y, ")");
    }

    void generate() {
        // We're going to have a four stage pipeline where we do work
        // on-device, on the host, on the device again, and finally on
        // the host again. In between each stage we'll schedule an
        // explicit copy in one direction or the other.

        dev_1(x, y) = input(x, y) + 1;
        host_1(x, y) = dev_1(x, y) + 1;
        dev_2(x, y) = host_1(x, y) + 1;
        output(x, y) = dev_2(x, y) + 1;
    }

    void schedule() {
        if (get_target().has_gpu_feature()) {
            Var tx, ty, xi, yi;

            // Set up a complicated nested tiling so that the two of the
            // buffer_copy stages are pulling a subset and two are pulling an entire buffer.
            output.compute_root()
                .tile(x, y, tx, ty, x, y, 64, 64);

            // dev_1 does computed over 64x64 kernel launches
            dev_1.compute_at(output, tx)
                .gpu_tile(x, y, xi, yi, 8, 8);

            // dev_2 does 32x32 kernel launches
            dev_2.compute_at(output, tx)
                .tile(x, y, tx, ty, x, y, 32, 32)
                .gpu_tile(x, y, xi, yi, 8, 8);

            // host_1 is computed per 32x32 tile of dev_2
            host_1.compute_at(dev_2, tx);

            // pulls a 64x64 subset of the input into a region of a GPU buffer to be consumed by dev_1
            input.in(dev_1).copy_to_device().compute_at(output, tx).store_root();

            // pulls a 32x32 subset from dev to host to be consumed by host_1
            dev_1.in(host_1).copy_to_host().compute_at(dev_2, tx);

            // pulls an entire 32x32 buffer back to the device to be consumed by dev_2
            host_1.in(dev_2).copy_to_device().compute_at(dev_2, tx);

            // pulls an entire 64x64 buffer back to the host to be consumed by the output
            dev_2.in(output).copy_to_host().compute_at(output, tx);
        }
    }
};

HALIDE_REGISTER_GENERATOR(BufferCopy, buffer_copy);
