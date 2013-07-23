#include <Halide.h>
#include <stdio.h>
#include <stdint.h>
#include "clock.h"

using namespace Halide;

template<typename T>
bool test(int w) {
    Func f, g;
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
    
    // Try dividing by all the known constants using vectors
    f.bound(y, 0, num_vals).bound(x, 0, input.width()).unroll(y);
    if (w > 1) f.vectorize(x);

    f.compile_jit();
    g.compile_jit();

    double t1 = currentTime();
    Image<T> correct = g.realize(input.width(), num_vals);
    for (int i = 0; i < 10; i++) g.realize(correct);
    double t2 = currentTime();
    Image<T> fast = f.realize(input.width(), num_vals);
    for (int i = 0; i < 10; i++) f.realize(correct);
    double t3 = currentTime();
    printf("Fast division path is %1.3f x faster \n", (t2-t1)/(t3-t2));

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

    if (success) {
        printf("Success!\n");
        return 0;
    } else {
        return -1;
    }
}
