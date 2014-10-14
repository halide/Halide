#include <stdio.h>
#include <math.h>
#include <Halide.h>
#include <iostream>
#include <limits>

using namespace Halide;

template <typename value_t>
bool relatively_equal(value_t a, value_t b) {
    if (a == b) {
        return true;
    } else if (!std::numeric_limits<value_t>::is_integer) {
        double da = (double)a, db = (double)b;
        double relative_error;

        // This test seems a bit high.
        if (fabs(db - da) < .0001)
          return true;

        if (fabs(da) > fabs(db))
            relative_error = fabs((db - da) / da);
        else
            relative_error = fabs((db - da) / db);

        if (relative_error < .0000005)
          return true;
        std::cerr << "relatively_equal failed for (" << a << ", " << b <<
          ") with relative error " << relative_error << std::endl;
    }
    return false;
}

// Using macros to expand name as both a C function and an Expr fragment.
// It may well be possible to do this without macros, but that is left
// for another day.

// Version for a one argument function.
#define fun_1(type, name, c_name)                                             \
    void test_##type##_##name(buffer_t *in_buf) {                             \
        Target target = get_jit_target_from_environment();                    \
        if (target.has_feature(Target::OpenCL) &&                             \
            !target.has_feature(Target::CLDoubles) &&                         \
            type_of<type>() == type_of<double>()) {                           \
            return;                                                           \
        }                                                                     \
        Func test_##name("test_" #name);                                      \
        Var x("x");                                                           \
        ImageParam input(type_of<type>(), 1);                                 \
        test_##name(x) = name(input(x));                                      \
        Buffer in_buffer(type_of<type>(), in_buf);                            \
        input.set(in_buffer);                                                 \
        if (target.has_gpu_feature()) {                                       \
            test_##name.gpu_tile(x, 8, GPU_Default);                          \
        }                                                                     \
        Image<type> result = test_##name.realize(in_buf->extent[0], target);  \
        for (int i = 0; i < in_buf->extent[0]; i++) {                         \
          type c_result = c_name(reinterpret_cast<type *>(in_buf->host)[i]);  \
          assert(relatively_equal(c_result, result(i)) &&                     \
                 "Failure on function " #name);                               \
        }                                                                     \
    }

// Version for a one argument function
#define fun_2(type, name, c_name)                                                   \
    void test_##type##_##name(buffer_t *in_buf) {                                   \
        Target target = get_jit_target_from_environment();                          \
        if (target.has_feature(Target::OpenCL) &&                                   \
            !target.has_feature(Target::CLDoubles) &&                               \
            type_of<type>() == type_of<double>()) {                                 \
            return;                                                                 \
        }                                                                           \
        Func test_##name("test_" #name);                                            \
        Var x("x");                                                                 \
        ImageParam input(type_of<type>(), 2);                                       \
        test_##name(x) = name(input(0, x), input(1, x));                            \
        Buffer in_buffer(type_of<type>(), in_buf);                                  \
        input.set(in_buffer);                                                       \
        if (target.has_gpu_feature()) {                                             \
          test_##name.gpu_tile(x, 8, GPU_Default);                                  \
        }                                                                           \
        Image<type> result = test_##name.realize(in_buf->extent[1], target);        \
        for (int i = 0; i < in_buf->extent[1]; i++) {                               \
          type c_result = c_name(reinterpret_cast<type *>(in_buf->host)[i * 2],     \
                                 reinterpret_cast<type *>(in_buf->host)[i * 2 + 1]);\
          assert(relatively_equal(c_result, result(i)) &&                           \
                 "Failure on function " #name);                                     \
        }                                                                           \
    }

#define fun_1_all_types(name)    \
  fun_1(float, name, name)       \
  fun_1(double, name, name)

#define fun_2_all_types(name)    \
  fun_2(float, name, name)       \
  fun_2(double, name, name)

fun_1_all_types(sqrt)
fun_1_all_types(sin)
fun_1_all_types(cos)
fun_1_all_types(exp)
fun_1_all_types(log)
fun_1_all_types(floor)
fun_1_all_types(ceil)
fun_1_all_types(trunc)
fun_1_all_types(asin)
fun_1_all_types(acos)
fun_1_all_types(tan)
fun_1_all_types(atan)
fun_1_all_types(sinh)
fun_1_all_types(cosh)
fun_1_all_types(tanh)
#ifndef _MSC_VER
// These functions don't exist in msvc < 2012
fun_1_all_types(asinh)
fun_1_all_types(acosh)
fun_1_all_types(atanh)
fun_1_all_types(round)
#endif

fun_1(float, abs, fabsf)
fun_1(double, abs, fabs)

fun_2_all_types(pow)
fun_2_all_types(atan2)

template <typename T>
struct TestArgs {
    Image<T> data;

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

    operator buffer_t *() { return data.raw_buffer(); }
};

// Note this test is more oriented toward making sure the paths
// through to math functions all work on a given target rather
// than usefully testing the accuracy of mathematical operations.
// As such little effort has been put into the domains tested,
// other than making sure they are valid for each function.

#define call_1(name, steps, start, end)                           \
    {                                                             \
    printf("Testing " #name "\n");                                \
    TestArgs<float> name##_float_args(steps, start, end);         \
    test_float_##name(name##_float_args);                         \
    TestArgs<double> name##_double_args(steps, start, end);       \
    test_double_##name(name##_double_args);                       \
    }

#define call_2(name, steps, start1, end1, start2, end2)                     \
    {                                                                       \
    printf("Testing " #name "\n");                                          \
    TestArgs<float> name##_float_args(steps, start1, end1, start2, end2);   \
    test_float_##name(name##_float_args);                                   \
    TestArgs<double> name##_double_args(steps, start1, end1, start2, end2); \
    test_double_##name(name##_double_args);                                 \
    }

int main(int argc, char **argv) {
    call_1(abs, 256, -1000, 1000);
    call_1(sqrt, 256, 0, 1000000)

    call_1(sin, 256, 5 * -3.1415, 5 * 3.1415)
    call_1(cos, 256, 5 * -3.1415, 5 * 3.1415)
    call_1(tan, 256, 5 * -3.1415, 5 * 3.1415)

    call_1(asin, 256, -1.0, 1.0)
    call_1(acos, 256, -1.0, 1.0)
    call_1(atan, 256, -256, 100)
    call_2(atan2, 256, -20, 20, -2, 2.001)

    call_1(sinh, 256, 5 * -3.1415, 5 * 3.1415)
    call_1(cosh, 256, 0, 1)
    call_1(tanh, 256, 5 * -3.1415, 5 * 3.1415)

#ifndef _MSC_VER
    call_1(asinh, 256, -10.0, 10.0)
    call_1(acosh, 256, 1.0, 10)
    call_1(atanh, 256, -1.0, 1.0)
    call_1(round, 256, -15, 15)
#endif

    call_1(exp, 256, 0, 20)
    call_1(log, 256, 1, 1000000)
    call_1(floor, 256, -25, 25)
    call_1(ceil, 256, -25, 25)
    call_1(trunc, 256, -25, 25)
    call_2(pow, 256, .1, 20, .1, 2)

    printf("Success!\n");
    return 0;
}
