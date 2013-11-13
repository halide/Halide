#include <stdio.h>
#include <math.h>
#include <Halide.h>
#include <iostream>

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

        if (relative_error < .0000002)
          return true;
        std::cerr << "relatively_equal failed for (" << a << ", " << b <<
          ") with relative error " << relative_error << std::endl;
    }
    return false;
}

// Using a macro to expand name as both a C function and an Expr fragment.
#define fun_1(type, name, c_name)                                           \
    void test_##type##_##name(buffer_t *in_buf) {                           \
        Func test_##name("test_" #name);                                    \
        Var x("x");                                                         \
        ImageParam input;                                                   \
        test_##name(x) = name(input(x));                                    \
        Buffer in_buffer(type_of<type>(), in_buf);                          \
        input.set(in_buffer);                                               \
        Target target = get_target_from_environment();                      \
        if (target.features & Target::CUDA) {                               \
            test_##name.cuda_tile(x, 8);                                    \
        }                                                                   \
        Image<type> result = test_##name.realize(in_buf->extent[0]);        \
        for (int i = 0; i < in_buf->extent[0]; i++) {                       \
          type c_result = name(reinterpret_cast<type *>(in_buf->host)[i]);  \
          assert(relatively_equal(c_result, result(i)) &&                   \
                 "Failure on function " #name);                             \
        }                                                                   \
    }

#define fun_2

#define fun_1_both(name)         \
  fun_1(float, name, name)	 \
  fun_1(double, name, name)

fun_1_both(sqrt)
fun_1_both(sin)
fun_1_both(cos)
fun_1_both(exp)
fun_1_both(log)
fun_1_both(floor)
fun_1_both(ceil)
fun_1_both(round)
fun_1_both(asin)
fun_1_both(acos)
fun_1_both(tan)
fun_1_both(atan)
fun_1_both(sinh)
fun_1_both(asinh)
fun_1_both(cosh)
fun_1_both(acosh)
fun_1_both(tanh)
fun_1_both(atanh)

fun_1(float, abs, fabsf)
fun_1(double, abs, fabs)

#if 0
fun_2_both(pow)
fun_2_both(atan2)
#endif

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
    : data(steps, 2) {
    for (int i = 0; i < steps; i++) {
      data(i, 0) = (T)((double)start_x + i * ((double)end_x - start_x) / steps);
      data(i, 1) = (T)((double)start_y + i * ((double)end_y - start_y) / steps);
    }
  }
};

int main(int argc, char **argv) {
#if 0
sqrt
sin
cos
exp
log
fabs
floor
ceil
round
pow
asin
acos
tan
atan
atan2
sinh
asinh
cosh
acosh
tanh
atanh
#endif  

    printf("Success!\n");
    return 0;
}
