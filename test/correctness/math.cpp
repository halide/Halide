#include "Halide.h"
#include <iostream>
#include <limits>
#include <math.h>
#include <stdio.h>

using namespace Halide;

namespace {

static int num_errors = 0;

template<typename T, typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr>
bool is_equal(T a, T b) {
    if (std::isnan(a) && std::isnan(b)) {
        return true;
    } else {
        return a == b;
    }
}

template<typename T, typename std::enable_if<!std::is_floating_point<T>::value>::type * = nullptr>
bool is_equal(T a, T b) {
    return a == b;
}

template<typename value_t>
bool relatively_equal(value_t a, value_t b, Target target) {
    if (is_equal(a, b)) {
        return true;
    } else if (!std::numeric_limits<value_t>::is_integer) {
        double da = (double)a, db = (double)b;
        double relative_error;

        // This test seems a bit high.
        if (fabs(db - da) < .0001) {
            return true;
        }

        if (fabs(da) > fabs(db)) {
            relative_error = fabs((db - da) / da);
        } else {
            relative_error = fabs((db - da) / db);
        }

        if (relative_error < .00000125) {
            return true;
        }

        // For HLSL, try again with a lower error threshold, as it might be using
        // fast but approximated trigonometric functions:
        if (target.supports_device_api(DeviceAPI::D3D12Compute) ||
            target.supports_device_api(DeviceAPI::OpenGLCompute) ||
            target.supports_device_api(DeviceAPI::WebGPU)) {
            // this threshold value has been empirically determined since there
            // is no clear documentation on the precision of these algorithms
            const double threshold = .001023;
            if (relative_error < threshold) {
                std::cout << "relatively_equal: relaxed threshold for (" << a << ", " << b << ") "
                          << "with relative error " << relative_error
                          << " (shader fast trig)\n";
                return true;
            }
        }

        std::cerr
            << "relatively_equal failed for (" << a << ", " << b
            << ") with relative error " << relative_error << "\n";
    } else {
        std::cerr << "relatively_equal failed for (" << (double)a << ", " << (double)b << ")\n";
    }
    return false;
}

float absd(float a, float b) {
    return a < b ? b - a : a - b;
}
double absd(double a, double b) {
    return a < b ? b - a : a - b;
}
uint8_t absd(int8_t a, int8_t b) {
    return a < b ? b - a : a - b;
}
uint16_t absd(int16_t a, int16_t b) {
    return a < b ? b - a : a - b;
}
uint32_t absd(int32_t a, int32_t b) {
    return a < b ? b - a : a - b;
}
uint8_t absd(uint8_t a, uint8_t b) {
    return a < b ? b - a : a - b;
}
uint16_t absd(uint16_t a, uint16_t b) {
    return a < b ? b - a : a - b;
}
uint32_t absd(uint32_t a, uint32_t b) {
    return a < b ? b - a : a - b;
}

template<typename T>
struct TestArgs {
    Buffer<T> data;

    TestArgs(int steps, T start, T end)
        : data(steps) {
        for (int i = 0; i < steps; i++) {
            data(i) = (T)((double)start + i * ((double)end - start) / steps);
        }
    }

    TestArgs(int steps,
             T start_x, T end_x,
             T start_y, T end_y)
        : data(2, steps) {
        for (int i = 0; i < steps; i++) {
            data(0, i) = (T)((double)start_x + i * ((double)end_x - start_x) / steps);
            data(1, i) = (T)((double)start_y + i * ((double)end_y - start_y) / steps);
        }
    }
};

// Using macros to expand name as both a C function and an Expr fragment.
// It may well be possible to do this without macros, but that is left
// for another day.

// Version for a one argument function.
#define fun_1(type_ret, type, name, c_name)                                                  \
    void test_##type##_##name(Buffer<type> in) {                                             \
        Target target = get_jit_target_from_environment();                                   \
        if (!target.supports_type(type_of<type>())) {                                        \
            return;                                                                          \
        }                                                                                    \
        Func test_##name("test_" #name);                                                     \
        Var x("x"), xi("xi");                                                                \
        test_##name(x) = name(in(x));                                                        \
        if (target.has_gpu_feature()) {                                                      \
            test_##name.gpu_tile(x, xi, 8);                                                  \
        } else if (target.has_feature(Target::HVX)) {                                        \
            test_##name.hexagon();                                                           \
        }                                                                                    \
        Buffer<type_ret> result = test_##name.realize({in.extent(0)}, target);               \
        for (int i = 0; i < in.extent(0); i++) {                                             \
            type_ret c_result = c_name(in(i));                                               \
            if (!relatively_equal(c_result, result(i), target)) {                            \
                fprintf(stderr, "For " #name "(%.20f) == %.20f from C and %.20f from %s.\n", \
                        (double)in(i), (double)c_result, (double)result(i),                  \
                        target.to_string().c_str());                                         \
                num_errors++;                                                                \
            }                                                                                \
        }                                                                                    \
    }

// Version for a two argument function
#define fun_2(type_ret, type, name, c_name)                                                         \
    void test_##type##_##name(Buffer<type> in) {                                                    \
        Target target = get_jit_target_from_environment();                                          \
        if (!target.supports_type(type_of<type>())) {                                               \
            return;                                                                                 \
        }                                                                                           \
        Func test_##name("test_" #name);                                                            \
        Var x("x"), xi("xi");                                                                       \
        test_##name(x) = name(in(0, x), in(1, x));                                                  \
        if (target.has_gpu_feature()) {                                                             \
            test_##name.gpu_tile(x, xi, 8);                                                         \
        } else if (target.has_feature(Target::HVX)) {                                               \
            test_##name.hexagon();                                                                  \
        }                                                                                           \
        Buffer<type_ret> result = test_##name.realize({in.height()}, target);                       \
        for (int i = 0; i < in.height(); i++) {                                                     \
            type_ret c_result = c_name(in(0, i), in(1, i));                                         \
            if (!relatively_equal(c_result, result(i), target)) {                                   \
                fprintf(stderr, "For " #name "(%.20f, %.20f) == %.20f from C and %.20f from %s.\n", \
                        (double)in(0, i), (double)in(1, i), (double)c_result, (double)result(i),    \
                        target.to_string().c_str());                                                \
                num_errors++;                                                                       \
            }                                                                                       \
        }                                                                                           \
    }

// clang-format off

#define fun_1_float_types(name)       \
    fun_1(float, float, name, name)   \
    fun_1(double, double, name, name)

#define fun_2_float_types(name)       \
    fun_2(float, float, name, name)   \
    fun_2(double, double, name, name)

fun_1_float_types(sqrt)
fun_1_float_types(sin)
fun_1_float_types(cos)
fun_1_float_types(exp)
fun_1_float_types(log)
fun_1_float_types(floor)
fun_1_float_types(ceil)
fun_1_float_types(trunc)
fun_1_float_types(asin)
fun_1_float_types(acos)
fun_1_float_types(tan)
fun_1_float_types(atan)
fun_1_float_types(sinh)
fun_1_float_types(cosh)
fun_1_float_types(tanh)
fun_1_float_types(asinh)
fun_1_float_types(acosh)
fun_1_float_types(atanh)
fun_1_float_types(round)

fun_2_float_types(pow)
fun_2_float_types(atan2)

fun_1(float, float, abs, fabsf)
fun_1(double, double, abs, fabs)
fun_1(uint8_t, int8_t, abs, abs)
fun_1(uint16_t, int16_t, abs, abs)
fun_1(uint32_t, int32_t, abs, abs)

fun_2_float_types(absd)
fun_2(uint8_t, int8_t, absd, absd)
fun_2(uint16_t, int16_t, absd, absd)
fun_2(uint32_t, int32_t, absd, absd)
fun_2(uint8_t, uint8_t, absd, absd)
fun_2(uint16_t, uint16_t, absd, absd)
fun_2(uint32_t, uint32_t, absd, absd)

// clang-format on

// Note this test is more oriented toward making sure the paths
// through to math functions all work on a given target rather
// than usefully testing the accuracy of mathematical operations.
// As such little effort has been put into the domains tested,
// other than making sure they are valid for each function.

#define call_1(type, name, steps, start, end)     \
    do {                                          \
        printf("Testing " #name "(" #type ")\n"); \
        TestArgs<type> args(steps, start, end);   \
        test_##type##_##name(args.data);          \
    } while (0)

#define call_2(type, name, steps, start1, end1, start2, end2)   \
    do {                                                        \
        printf("Testing " #name "(" #type ")\n");               \
        TestArgs<type> args(steps, start1, end1, start2, end2); \
        test_##type##_##name(args.data);                        \
    } while (0)

#define call_1_float_types(name, steps, start, end) \
    do {                                            \
        call_1(float, name, steps, start, end);     \
        call_1(double, name, steps, start, end);    \
    } while (0)

#define call_2_float_types(name, steps, start1, end1, start2, end2) \
    do {                                                            \
        call_2(float, name, steps, start1, end1, start2, end2);     \
        call_2(double, name, steps, start1, end1, start2, end2);    \
    } while (0)

}  // namespace

int main(int argc, char **argv) {
    printf("host is:      %s\n", get_host_target().to_string().c_str());
    printf("HL_JIT_TARGET is: %s\n", get_jit_target_from_environment().to_string().c_str());

    call_1_float_types(abs, 256, -1000, 1000);
    call_1_float_types(sqrt, 256, 0, 1000000);

    call_1_float_types(sin, 256, 5 * -3.1415f, 5 * 3.1415f);
    call_1_float_types(cos, 256, 5 * -3.1415f, 5 * 3.1415f);
    call_1_float_types(tan, 256, 0.49f * -3.1415f, 0.49f * 3.1415f);

    call_1_float_types(asin, 256, -1.0, 1.0);
    call_1_float_types(acos, 256, -1.0, 1.0);
    call_1_float_types(atan, 256, -256, 100);
    call_2_float_types(atan2, 256, -20, 20, -2, 2.001f);

    call_1_float_types(sinh, 256, 5 * -3.1415f, 5 * 3.1415f);
    call_1_float_types(cosh, 256, 0, 1);
    call_1_float_types(tanh, 256, 5 * -3.1415f, 5 * 3.1415f);

    call_1_float_types(asinh, 256, -10.0, 10.0);
    call_1_float_types(acosh, 256, 1.0, 10);
    call_1_float_types(atanh, 256, -1.0, 1.0);
    call_1_float_types(round, 256, -15, 15);

    call_1_float_types(exp, 256, 0, 20);
    call_1_float_types(log, 256, 1, 1000000);
    call_1_float_types(floor, 256, -25, 25);
    call_1_float_types(ceil, 256, -25, 25);
    call_1_float_types(trunc, 256, -25, 25);

    if (get_jit_target_from_environment().has_feature(Target::OpenGLCompute)) {
        // GLSL isn't required to support NaN, so keep things real
        call_2_float_types(pow, 256, 0.0, 10.0, -4.0f, 4.0f);
    } else {
        call_2_float_types(pow, 256, -10.0, 10.0, -4.0f, 4.0f);
    }

    const int8_t int8_min = std::numeric_limits<int8_t>::min();
    const int16_t int16_min = std::numeric_limits<int16_t>::min();
    const int32_t int32_min = std::numeric_limits<int32_t>::min();
    const int8_t int8_max = std::numeric_limits<int8_t>::max();
    const int16_t int16_max = std::numeric_limits<int16_t>::max();
    const int32_t int32_max = std::numeric_limits<int32_t>::max();

    const uint8_t uint8_min = std::numeric_limits<uint8_t>::min();
    const uint16_t uint16_min = std::numeric_limits<uint16_t>::min();
    const uint32_t uint32_min = std::numeric_limits<uint32_t>::min();
    const uint8_t uint8_max = std::numeric_limits<uint8_t>::max();
    const uint16_t uint16_max = std::numeric_limits<uint16_t>::max();
    const uint32_t uint32_max = std::numeric_limits<uint32_t>::max();

    call_1_float_types(abs, 256, -25, 25);
    call_1(int8_t, abs, 255, -int8_max, int8_max);
    call_1(int16_t, abs, 255, -int16_max, int16_max);
    call_1(int32_t, abs, 255, -int32_max, int32_max);

    call_2_float_types(absd, 256, -25, 25, -25, 25);
    call_2(int8_t, absd, 256, int8_min, int8_max, int8_min, int8_max);
    call_2(int16_t, absd, 256, int16_min, int16_max, int16_min, int16_max);
    call_2(int32_t, absd, 256, int32_min, int32_max, int32_min, int32_max);
    call_2(uint8_t, absd, 256, uint8_min, uint8_max, uint8_min, uint8_max);
    call_2(uint16_t, absd, 256, uint16_min, uint16_max, uint16_min, uint16_max);
    call_2(uint32_t, absd, 256, uint32_min, uint32_max, uint32_min, uint32_max);
    // TODO: int64 isn't tested because the testing mechanism relies
    // on integer types being representable with doubles.

    if (num_errors) {
        fprintf(stderr, "Failed with %d total errors\n", num_errors);
        exit(1);
    }

    printf("Success!\n");
    return 0;
}
