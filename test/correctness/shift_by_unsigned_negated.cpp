#include "Halide.h"

using namespace Halide;

template<typename T>
bool test(Func f, T f_expected, int width) {
    Buffer<uint32_t> actual = f.realize({width});
    for (int i = 0; i < actual.width(); i++) {
        if (actual(i) != f_expected(i)) {
            printf("r(%d) = %d, f_expected(%d) = %d\n",
                   i, actual(i), i, f_expected(i));
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    Buffer<uint32_t> step(31);
    for (int i = 0; i < step.width(); i++) {
        step(i) = -i;
    }

    bool success = true;
    Var x;

    {
        Func f;
        f(x) = Expr(-1U) << -step(x);
        auto f_expected = [&](int x) {
            return -1U << x;
        };
        success &= test(f, f_expected, step.width());
    }
    {
        Func f;
        f(x) = Expr(-1U) >> -step(x);
        auto f_expected = [&](int x) {
            return -1U >> x;
        };
        success &= test(f, f_expected, step.width());
    }

    if (success) printf("Success!\n");
    return success ? 0 : -1;
}
