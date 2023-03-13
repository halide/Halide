#include "Halide.h"

using namespace Halide;

template<typename T>
void test() {

    {
        // Test div_round_to_zero
        Func f;
        Var x, y;

        Expr d = cast<T>(y - 128);
        Expr n = cast<T>(x - 128);
        d = select(d == 0 || (d == -1 && n == d.type().min()),
                   cast<T>(1),
                   d);
        f(x, y) = div_round_to_zero(n, d);

        f.vectorize(x, 8);

        Buffer<T> result = f.realize({256, 256});

        for (int d = -128; d < 128; d++) {
            if (d == 0) {
                continue;
            }
            for (int n = -128; n < 128; n++) {
                if (d == -1 && n == std::numeric_limits<T>::min()) {
                    continue;
                }
                int correct = d == 0 ? n : (T)(n / d);
                int r = result(n + 128, d + 128);
                if (r != correct) {
                    printf("result(%d, %d) = %d instead of %d\n", n, d, r, correct);
                    exit(1);
                }
            }
        }
    }

    {
        // Test the fast version
        Func f;
        Var x, y;

        f(x, y) = fast_integer_divide_round_to_zero(cast<T>(x - 128), cast<uint8_t>(y + 1));

        f.vectorize(x, 8);

        Buffer<T> result_fast = f.realize({256, 255});

        for (int d = 1; d < 256; d++) {
            for (int n = -128; n < 128; n++) {
                int correct = (T)(n / d);
                int r = result_fast(n + 128, d - 1);
                if (r != correct) {
                    printf("result_fast(%d, %d) = %d instead of %d\n", n, d, r, correct);
                    exit(1);
                }
            }
        }
    }

    {
        // Try some constant denominators
        for (int d : {-128, -54, -3, -1, 1, 2, 25, 32, 127}) {
            if (d == 0) {
                continue;
            }

            Func f;
            Var x;

            f(x) = div_round_to_zero(cast<T>(x - 128), cast<T>(d));

            f.vectorize(x, 8);

            Buffer<T> result_const = f.realize({256});

            for (int n = -128; n < 128; n++) {
                int correct = (T)(n / d);
                int r = result_const(n + 128);
                if (r != correct) {
                    printf("result_const(%d, %d) = %d instead of %d\n", n, d, r, correct);
                    exit(1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    test<int8_t>();
    test<int16_t>();
    test<int32_t>();
    printf("Success!\n");
    return 0;
}
