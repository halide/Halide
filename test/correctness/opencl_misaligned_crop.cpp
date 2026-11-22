#include "Halide.h"
#include <cstdio>
#include <type_traits>

using namespace Halide;

// OpenCL kernels must be able to read and write crops and slices of device
// buffers that start at any element, not just at multiples of
// CL_DEVICE_MEM_BASE_ADDR_ALIGN (typically 128 to 4096 bits), which is what
// clCreateSubBuffer requires.

namespace {

int errors = 0;

template<typename T>
T input_value(int x, int y, int c) {
    return (T)(x * 3 + y * 7 + c * 11);
}

// The value of out(x, y) = in(x, y) + in(x + 1, y) * 2 - y, wrapping like
// Halide does for integers.
template<typename T>
T output_value(T a, T b, int y) {
    if constexpr (std::is_floating_point_v<T>) {
        return a + b * 2 - (T)y;
    } else {
        return (T)((int64_t)a + (int64_t)b * 2 - y);
    }
}

constexpr int sentinel = 99;

template<typename T>
Buffer<T> make_device_buffer(int w, int h, int c, bool input) {
    Buffer<T> buf(w, h, c);
    buf.for_each_element([&](int x, int y, int ch) {
        buf(x, y, ch) = input ? input_value<T>(x, y, ch) : (T)sentinel;
    });
    buf.set_host_dirty();
    buf.copy_to_device(get_jit_target_from_environment());
    return buf;
}

// An alias of a crop that starts at coordinate 0 in every dimension, sharing
// the crop's device allocation (which the crop keeps owning). set_min() would
// discard the device allocation instead.
template<typename T>
Buffer<T> with_zero_mins(const Buffer<T> &crop) {
    halide_buffer_t raw = *crop.raw_buffer();
    Buffer<T> alias(raw);
    for (int d = 0; d < alias.dimensions(); d++) {
        alias.raw_buffer()->dim[d].min = 0;
    }
    return alias;
}

// Checks that out_parent holds out_fn inside the region of out_crop (whose
// coordinates are in out_crop's own coordinate system, translated by (dx, dy)
// from the parent's), and the sentinel everywhere else.
template<typename T, typename Fn>
void check_output(const char *name, Buffer<T> &out_parent, const Buffer<T> &out_crop,
                  int dx, int dy, int channel, Fn out_fn) {
    out_parent.set_device_dirty();
    out_parent.copy_to_host();
    int bad = 0;
    out_parent.for_each_element([&](int x, int y, int c) {
        int cx = x - dx, cy = y - dy;
        bool inside = c == channel &&
                      cx >= out_crop.dim(0).min() && cx <= out_crop.dim(0).max() &&
                      cy >= out_crop.dim(1).min() && cy <= out_crop.dim(1).max();
        T correct = inside ? out_fn(cx, cy) : (T)sentinel;
        if (out_parent(x, y, c) != correct && bad++ < 5) {
            printf("%s: out(%d, %d, %d) = %f instead of %f\n", name, x, y, c,
                   (double)out_parent(x, y, c), (double)correct);
        }
    });
    if (bad) {
        errors++;
    }
}

// in and out are 2D crops of 3D device buffers. The kernel reads in at x and
// x + 1, and stores out with vectors of 'vec' lanes.
template<typename T>
void run_2d(Buffer<T> in, Buffer<T> out, int vec,
            bool constrain, int stride) {
    ImageParam in_p(type_of<T>(), 2);
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f("f");
    f(x, y) = cast<T>(in_p(x, y) + in_p(x + 1, y) * 2 - cast<T>(y));
    if (constrain) {
        // With the mins at 0 and the strides multiples of the vector width,
        // Halide proves that every vector access is aligned relative to the
        // base of the buffer, although the base itself is not aligned.
        in_p.dim(0).set_min(0).dim(1).set_min(0).dim(1).set_stride(stride);
        f.output_buffer().dim(0).set_min(0).dim(1).set_min(0).dim(1).set_stride(stride);
    }
    if (vec > 1) {
        f.vectorize(x, vec, TailStrategy::GuardWithIf).gpu_tile(x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf);
    } else {
        f.gpu_tile(x, y, xi, yi, 16, 4, TailStrategy::GuardWithIf);
    }
    in_p.set(in);
    f.realize(out, get_jit_target_from_environment());
}

template<typename T>
void test_type(const char *type_name) {
    const Target target = get_jit_target_from_environment();
    // A parent buffer large enough that crops at odd offsets are far from
    // any CL_DEVICE_MEM_BASE_ADDR_ALIGN boundary.
    const int W = 256, H = 40, C = 3;
    char name[256];

    for (int vec : {1, 4, 8}) {
        for (int x0 : {1, 3, 4, 13, 37}) {
            const int y0 = 5, c0 = 1;
            // Crops of a slice, at odd offsets.
            Buffer<T> in_parent = make_device_buffer<T>(W, H, C, true);
            Buffer<T> out_parent = make_device_buffer<T>(W, H, C, false);
            Buffer<T> in = in_parent.sliced(2, c0).cropped(0, x0 - 1, 102).cropped(1, y0, 19);
            Buffer<T> out = out_parent.sliced(2, c0).cropped(0, x0, 99).cropped(1, y0 + 2, 15);
            snprintf(name, sizeof(name), "%s vec=%d x0=%d crop of slice", type_name, vec, x0);
            run_2d<T>(in, out, vec, false, W);
            check_output<T>(name, out_parent, out, 0, 0, c0, [&](int x, int y) {
                return output_value<T>(input_value<T>(x, y, c0), input_value<T>(x + 1, y, c0), y);
            });

            // Crops translated to min 0 with known strides: the indices are
            // provably vector-aligned, but the crops' bases are not.
            in_parent = make_device_buffer<T>(W, H, C, true);
            out_parent = make_device_buffer<T>(W, H, C, false);
            Buffer<T> in_crop = in_parent.sliced(2, 2).cropped(0, x0, 128).cropped(1, y0, 20);
            Buffer<T> out_crop = out_parent.sliced(2, 2).cropped(0, x0 + 2, 64).cropped(1, y0 + 1, 13);
            Buffer<T> in2 = with_zero_mins(in_crop);
            Buffer<T> out2 = with_zero_mins(out_crop);
            snprintf(name, sizeof(name), "%s vec=%d x0=%d aligned indices", type_name, vec, x0);
            run_2d<T>(in2, out2, vec, true, W);
            check_output<T>(name, out_parent, out2, x0 + 2, y0 + 1, 2, [&](int x, int y) {
                return output_value<T>(input_value<T>(x + x0, y + y0, 2), input_value<T>(x + 1 + x0, y + y0, 2), y);
            });
        }
    }

    // A 3D pipeline on a crop of a crop, reading from two crops of the same
    // device allocation, and writing to a crop of a third.
    {
        Buffer<T> in_parent = make_device_buffer<T>(W, H, C, true);
        Buffer<T> out_parent = make_device_buffer<T>(W, H, C, false);
        Buffer<T> a = in_parent.cropped(std::vector<std::pair<int, int>>{{3, 200}, {1, 30}, {0, 3}}).cropped(std::vector<std::pair<int, int>>{{7, 101}, {2, 21}, {1, 2}});
        Buffer<T> b = in_parent.cropped(std::vector<std::pair<int, int>>{{9, 150}, {0, 33}, {0, 3}});
        Buffer<T> out = out_parent.cropped(std::vector<std::pair<int, int>>{{5, 190}, {3, 30}, {0, 3}}).cropped(std::vector<std::pair<int, int>>{{11, 97}, {4, 19}, {1, 2}});
        ImageParam a_p(type_of<T>(), 3), b_p(type_of<T>(), 3);
        Var x("x"), y("y"), c("c"), xi("xi"), yi("yi");
        Func f("f");
        f(x, y, c) = cast<T>(a_p(x, y, c) + b_p(x + 1, y + 1, c) * 2);
        f.vectorize(x, 4, TailStrategy::GuardWithIf).gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::GuardWithIf);
        a_p.set(a);
        b_p.set(b);
        f.realize(out, target);
        out_parent.set_device_dirty();
        out_parent.copy_to_host();
        int bad = 0;
        out_parent.for_each_element([&](int x, int y, int ch) {
            bool inside = x >= 11 && x < 11 + 97 && y >= 4 && y < 4 + 19 && ch >= 1 && ch < 3;
            T correct = inside ? (T)(input_value<T>(x, y, ch) + input_value<T>(x + 1, y + 1, ch) * 2) : (T)sentinel;
            if (out_parent(x, y, ch) != correct && bad++ < 5) {
                printf("%s 3D: out(%d, %d, %d) = %f instead of %f\n", type_name, x, y, ch,
                       (double)out_parent(x, y, ch), (double)correct);
            }
        });
        if (bad) {
            errors++;
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenCL)) {
        printf("[SKIP] OpenCL not enabled.\n");
        return 0;
    }

    test_type<uint8_t>("uint8");
    test_type<uint16_t>("uint16");
    test_type<int32_t>("int32");
    test_type<float>("float");

    if (errors) {
        printf("%d tests failed\n", errors);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
