#include "Halide.h"

using namespace Halide;

template<typename T>
void check_equal_shape(const Buffer<T> &a, const Buffer<T> &b) {
    if (a.dimensions() != b.dimensions()) abort();
    for (int i = 0; i < a.dimensions(); i++) {
        if (a.dim(i).min() != b.dim(i).min() ||
            a.dim(i).extent() != b.dim(i).extent()) {
            abort();
        }
    }
}

template<typename T>
void check_equal(const Buffer<T> &a, const Buffer<T> &b) {
    check_equal_shape(a, b);
    a.for_each_element([&](const int *pos) {
        if (a(pos) != b(pos)) abort();
    });
}



int main(int argc, char **argv) {
    {
        // Check copying a buffer
        Buffer<float> a(100, 3, 80), b(120, 80, 3);

        // Mess with the memory layout to make it more interesting
        a.transpose(1, 2);

        a.fill(1.0f);
        b.for_each_element([&](int x, int y, int c) {
            b(x, y, c) = x + 2.5f * y + 9.5f * c;
        });

        check_equal(a, a.copy());

        // Check copying from one subregion to another (with different memory layout)
        Buffer<float> a_window = a.cropped(0, 20, 20).cropped(1, 50, 10);
        Buffer<float> b_window = b.cropped(0, 20, 20).cropped(1, 50, 10);
        a_window.copy_from(b);
        check_equal(a_window, b_window);

        // You don't actually have to crop a.
        a.fill(1.0f);
        a.copy_from(b_window);
        check_equal(a_window, b_window);
    }

    {
        // Check make a Buffer from a Buffer of a different type
        Buffer<float, 2> a(100, 80);
        Buffer<const float, 3> b(a); // statically safe
        Buffer<const void, 4> c(b);  // statically safe
        Buffer<const float, 3> d(c); // does runtime checks of actual dimensionality and type.
        Buffer<void, 3> e(a);        // statically safe
        Buffer<float, 2> f(e);       // runtime checks
    }

    {
        // Check moving a buffer around
        Buffer<float> a(100, 80, 3);
        Buffer<float> b(a);
        b.set_min(123, 456, 2);
        b.translate({-123, -456, -2});
        check_equal(a, b);
    }

    return 0;
}
