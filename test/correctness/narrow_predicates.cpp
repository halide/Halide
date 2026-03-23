#include "Halide.h"

using namespace Halide;

Var x;

template<typename T>
void check(const Expr &e) {
    Func g1, g2;
    g1(x) = e;
    g2(x) = e;

    // Introduce some vector predicates to g1
    g1.vectorize(x, 64, TailStrategy::GuardWithIf);

    Buffer<T> b1(1024), b2(1024);
    g1.realize(b1);
    g2.realize(b2);

    for (int i = 0; i < b1.width(); i++) {
        if (b1(i) != b2(i)) {
            printf("b1(%d) = %d instead of %d\n",
                   i, b1(i), b2(i));
            exit(-1);
        }
    }
}

template<typename T>
void check_all() {
    Func f;
    f(x) = cast<T>(x);
    f.compute_root();

    // This will have a predicated instruction in the loop tail:
    check<T>(f(x));

    // These will also have a comparison mask in the loop body:
    check<T>(select(x < 50, f(x), cast<T>(17)));
    check<T>(select(x > 50, f(x), cast<T>(17)));

    // Also test boundary conditions, which introduce all sorts of coordinate
    // comparisons:
    check<T>(BoundaryConditions::repeat_edge(f, {{10, 100}})(x));
    check<T>(BoundaryConditions::repeat_image(f, {{10, 100}})(x));
    check<T>(BoundaryConditions::constant_exterior(f, cast<T>(17), {{10, 100}})(x));
    check<T>(BoundaryConditions::mirror_image(f, {{10, 100}})(x));
    check<T>(BoundaryConditions::mirror_interior(f, {{10, 100}})(x));
}

int main(int argc, char **argv) {
    check_all<uint8_t>();
    check_all<uint16_t>();

    printf("Success!\n");
    return 0;
}
