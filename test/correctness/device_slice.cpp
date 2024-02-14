#include "Halide.h"
#include "HalideBuffer.h"

using namespace Halide;

const int kEdges[3] = {128, 64, 32};

Halide::Runtime::Buffer<int32_t> make_gpu_buffer(bool hexagon_rpc) {
    Var x, y, c;
    Func f;
    f(x, y, c) = x + y * 256 + c * 256 * 256;

    if (hexagon_rpc) {
        f.hexagon();
    } else {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 8, 8);
    }

    Buffer<int32_t> result = f.realize({kEdges[0], kEdges[1], kEdges[2]});
    return *result.get();
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    bool hexagon_rpc = (target.arch != Target::Hexagon) &&
                       target.has_feature(Target::HVX);

    if (!hexagon_rpc && !target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    printf("Test in-place slicing.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        const int slice_dim = 1;
        const int slice_pos = 0;
        gpu_buf.slice(slice_dim, slice_pos);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        assert(gpu_buf.dimensions() == 2);
        assert(gpu_buf.extent(0) == kEdges[0]);
        assert(gpu_buf.extent(1) == kEdges[2]);

        gpu_buf.copy_to_host();
        gpu_buf.for_each_element([&](int x, int c) {
            const int y = slice_pos;
            assert(gpu_buf(x, c) == x + y * 256 + c * 256 * 256);
        });
    }

    printf("Test nondestructive slicing.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        const int slice_dim = 0;
        const int slice_pos = 31;
        Halide::Runtime::Buffer<int32_t> sliced = gpu_buf.sliced(slice_dim, slice_pos);
        assert(sliced.raw_buffer()->device_interface != nullptr);

        assert(sliced.dimensions() == 2);
        assert(sliced.extent(0) == kEdges[1]);
        assert(sliced.extent(1) == kEdges[2]);

        sliced.copy_to_host();
        sliced.for_each_element([&](int y, int c) {
            const int x = slice_pos;
            assert(sliced(y, c) == x + y * 256 + c * 256 * 256);
        });

        gpu_buf.copy_to_host();
        gpu_buf.for_each_element([&](int x, int y, int c) {
            assert(gpu_buf(x, y, c) == x + y * 256 + c * 256 * 256);
        });
    }

    printf("Test slice of a slice\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        const int slice_dim = 1;
        const int slice_pos = 0;
        Halide::Runtime::Buffer<int32_t> sliced = gpu_buf.sliced(slice_dim, slice_pos);
        assert(sliced.raw_buffer()->device_interface != nullptr);

        assert(sliced.dimensions() == 2);
        assert(sliced.extent(0) == kEdges[0]);
        assert(sliced.extent(1) == kEdges[2]);

        const int slice_dim2 = 0;
        const int slice_pos2 = 10;
        Halide::Runtime::Buffer<int32_t> sliced2 = sliced.sliced(slice_dim2, slice_pos2);
        assert(sliced2.raw_buffer()->device_interface != nullptr);

        assert(sliced2.dimensions() == 1);
        assert(sliced2.extent(0) == kEdges[2]);

        sliced.copy_to_host();
        sliced.for_each_element([&](int x, int c) {
            const int y = slice_pos;
            assert(sliced(x, c) == x + y * 256 + c * 256 * 256);
        });

        sliced2.copy_to_host();
        sliced2.for_each_element([&](int c) {
            const int x = slice_pos2;
            const int y = slice_pos;
            assert(sliced2(c) == x + y * 256 + c * 256 * 256);
        });

        gpu_buf.copy_to_host();
        gpu_buf.for_each_element([&](int x, int y, int c) {
            assert(gpu_buf(x, y, c) == x + y * 256 + c * 256 * 256);
        });
    }

    printf("Test parent going out of scope before slice.\n");
    {
        Halide::Runtime::Buffer<int32_t> sliced;

        const int slice_dim = 1;
        const int slice_pos = 0;

        {
            Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
            assert(gpu_buf.raw_buffer()->device_interface != nullptr);

            sliced = gpu_buf.sliced(slice_dim, slice_pos);
            assert(sliced.raw_buffer()->device_interface != nullptr);
        }

        assert(sliced.dimensions() == 2);
        assert(sliced.extent(0) == kEdges[0]);
        assert(sliced.extent(1) == kEdges[2]);

        sliced.copy_to_host();
        sliced.for_each_element([&](int x, int c) {
            const int y = slice_pos;
            assert(sliced(x, c) == x + y * 256 + c * 256 * 256);
        });
    }

    printf("Test realizing to/from slice.\n");
    {
        ImageParam in(Int(32), 2);
        Var x, y;
        Func f;
        f(x, y) = in(x, y) + 42;

        Var xi, yi;
        if (hexagon_rpc) {
            f.hexagon();
        } else {
            f.gpu_tile(x, y, xi, yi, 8, 8);
        }

        Halide::Buffer<int32_t> gpu_input = make_gpu_buffer(hexagon_rpc);
        Halide::Buffer<int32_t> gpu_output = make_gpu_buffer(hexagon_rpc);

        const int slice_dim = 1;
        const int slice_pos = 0;

        gpu_input.slice(slice_dim, slice_pos);
        gpu_output.slice(slice_dim, slice_pos);

        in.set(gpu_input);

        f.realize(gpu_output, target);

        gpu_output.copy_to_host();
        gpu_output.copy_to_host();
        gpu_output.for_each_element([&](int x, int c) {
            const int y = slice_pos;
            assert(gpu_output(x, c) == x + y * 256 + c * 256 * 256 + 42);
        });
    }

    printf("Success!\n");

    return 0;
}
