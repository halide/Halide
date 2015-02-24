#include "Halide.h"
#include <stdio.h>
#include <stdint.h>
#include "clock.h"
#include "time.h"

using namespace Halide;

template<typename T>
bool test(int w) {
    Func f, g, h;
    Var x, y;

    size_t bits = sizeof(T)*8;
    bool is_signed = (T)(-1) < (T)(0);

    printf("Testing %sint%d_t x %d\n",
           is_signed ? "" : "u",
           (int)bits, w);

    int min_val = 2, num_vals = 254;

    if (bits <= 8 && is_signed) {
        // There are two types of integer division that cause runtime crashes:
        // 1) Division by zero
        // 2) Division of the smallest negative number by -1 (because
        // the result overflows)
        // In either case, let's avoid overflows to dodge such errors.
        num_vals = 126;
    }

    Image<T> input(w, num_vals);

    for (int y = 0; y < num_vals; y++) {
        for (int x = 0; x < input.width(); x++) {
            uint32_t bits = rand() ^ (rand() << 16);
            input(x, y) = (T)bits;
        }
    }

    f(x, y) = input(x, y)/cast<T>(y + min_val);

    // Reference good version
    g(x, y) = input(x, y)/cast<T>(y + min_val);

    // Version that uses fast_integer_divide
    h(x, y) = Halide::fast_integer_divide(input(x, y), cast<uint8_t>(y + min_val));

    // Try dividing by all the known constants using vectors
    f.bound(y, 0, num_vals).bound(x, 0, input.width()).unroll(y);
    h.bound(x, 0, input.width());
    if (w > 1) {
        f.vectorize(x);
        h.vectorize(x);
    }

    f.compile_jit();
    g.compile_jit();
    h.compile_jit();

    double t1 = current_time();
    Image<T> correct = g.realize(input.width(), num_vals);
    for (int i = 0; i < 30; i++) g.realize(correct);
    double t2 = current_time();
    Image<T> fast = f.realize(input.width(), num_vals);
    for (int i = 0; i < 30; i++) f.realize(fast);
    double t3 = current_time();
    Image<T> fast_dynamic = h.realize(input.width(), num_vals);
    for (int i = 0; i < 30; i++) h.realize(fast_dynamic);
    double t4 = current_time();
    printf("compile-time-constant divisor path is %1.3f x faster \n", (t2-t1)/(t3-t2));
    printf("fast_integer_divide path is           %1.3f x faster \n", (t4-t3)/(t3-t2));

    for (int y = 0; y < num_vals; y++) {
        for (int x = 0; x < input.width(); x++) {
            if (fast(x, y) != correct(x, y)) {
                printf("fast(%d, %d) = %lld instead of %lld (%lld/%d)\n",
                       x, y,
                       (long long int)fast(x, y),
                       (long long int)correct(x, y),
                       (long long int)input(x, y),
                       (T)(y + min_val));
                return false;
            }
        }
    }

    return true;

}

int main(int argc, char **argv) {

    srand(time(NULL));

    bool success = true;
    // Scalar
    success = success && test<int32_t>(1);
    success = success && test<int16_t>(1);
    success = success && test<int8_t>(1);
    success = success && test<uint32_t>(1);
    success = success && test<uint16_t>(1);
    success = success && test<uint8_t>(1);
    // Vector
    success = success && test<int32_t>(4);
    success = success && test<int16_t>(8);
    success = success && test<int8_t>(16);
    success = success && test<uint32_t>(4);
    success = success && test<uint16_t>(8);
    success = success && test<uint8_t>(16);

    //success = test<uint16_t>(8);

    if (success) {
        printf("Success!\n");
        return 0;
    } else {
        return -1;
    }
}
