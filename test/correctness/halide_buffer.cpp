// Don't include Halide.h: it is not necessary for this test.
#include "HalideBuffer.h"

#include <stdio.h>

using namespace Halide::Runtime;

template<typename T1, typename T2>
void check_equal_shape(const Buffer<T1> &a, const Buffer<T2> &b) {
    if (a.dimensions() != b.dimensions()) abort();
    for (int i = 0; i < a.dimensions(); i++) {
        if (a.dim(i).min() != b.dim(i).min() ||
            a.dim(i).extent() != b.dim(i).extent()) {
            abort();
        }
    }
}

template<typename T1, typename T2>
void check_equal(const Buffer<T1> &a, const Buffer<T2> &b) {
    check_equal_shape(a, b);
    a.for_each_element([&](const int *pos) {
        if (a(pos) != b(pos)) {
            printf("Mismatch: %f vs %f at %d %d %d\n",
                   (float)(a(pos)), (float)(b(pos)), pos[0], pos[1], pos[2]);
            abort();
        }
    });
}

void test_copy(Buffer<float> a, Buffer<float> b) {
    // Mess with the memory layout to make it more interesting
    a.transpose(1, 2);

    a.fill(1.0f);

    assert(a.all_equal(1.0f));

    b.fill([&](int x, int y, int c) {
        return x + 100.0f * y + 100000.0f * c;
    });

    b.for_each_element([&](int x, int y, int c) {
        assert(b(x, y, c) == x + 100.0f * y + 100000.0f * c);
    });

    check_equal(a, a.copy());

    // Check copying from one subregion to another (with different memory layout)
    Buffer<float> a_window = a.cropped(0, 20, 20).cropped(1, 50, 10);
    Buffer<const float> b_window = b.cropped(0, 20, 20).cropped(1, 50, 10);
    a_window.copy_from(b);

    check_equal(a_window, b_window);

    // Check copying from const to nonconst
    Buffer<float> a_window_nonconst = b_window.copy();
    check_equal(a_window_nonconst, b_window);

    // Check copying from const to const
    Buffer<const float> a_window_const = b_window.copy();
    check_equal(a_window_const, b_window);

    // You don't actually have to crop a.
    a.fill(1.0f);
    a.copy_from(b_window);
    check_equal(a_window, b_window);

    // The buffers can have dynamic type
    Buffer<void> a_void(a);
    Buffer<const void> b_void_window(b_window);
    a.fill(1.0f);
    a_void.copy_from(b_void_window);
    check_equal(a_window, b_window);
}

int main(int argc, char **argv) {
    {
        // Check copying a buffer
        Buffer<float> a(100, 3, 80), b(120, 80, 3);
        test_copy(a, b);
    }


    {
        // Check copying a buffer, using the halide_dimension_t pointer ctors
        halide_dimension_t shape_a[] = {{0, 100, 1},
                                        {0, 3, 1*100},
                                        {0, 80, 1*100*3}};
        Buffer<float> a(nullptr, 3, shape_a);
        a.allocate();

        halide_dimension_t shape_b[] = {{0, 120, 1},
                                        {0, 80, 1*120},
                                        {0, 3, 1*120*80}};
        Buffer<float> b(nullptr, 3, shape_b);
        b.allocate();

        test_copy(a, b);
    }

    {
        // Check copying a buffer, using the vector<halide_dimension_t> ctors
        Buffer<float> a(nullptr, {{0, 100, 1},
                                  {0, 3, 1*100},
                                  {0, 80, 1*100*3}});
        a.allocate();

        Buffer<float> b(nullptr, {{0, 120, 1},
                                  {0, 80, 1*120},
                                  {0, 3, 1*120*80}});
        b.allocate();

        test_copy(a, b);
    }

    {
        // Check make a Buffer from a Buffer of a different type
        Buffer<float, 2> a(100, 80);
        Buffer<const float, 3> b(a); // statically safe
        Buffer<const void, 4> c(b);  // statically safe
        Buffer<const float, 3> d(c); // does runtime check of actual type.
        Buffer<void, 3> e(a);        // statically safe
        Buffer<float, 2> f(e);       // runtime checks
    }

    {
        // Check moving a buffer around
        Buffer<float> a(100, 80, 3);
        a.for_each_element([&](int x, int y, int c) {
            a(x, y, c) = x + 100.0f * y + 100000.0f * c;
        });
        Buffer<float> b(a);
        b.set_min(123, 456, 2);
        b.translate({-123, -456, -2});
        check_equal(a, b);
    }

    {
        // Check lifting a function over scalars to a function over entire buffers.
        const int W = 5, H = 4, C = 3;
        Buffer<float> a(W, H, C);
        Buffer<float> b = Buffer<float>::make_interleaved(W, H, C);
        int counter = 0;
        b.for_each_value([&](float &b) {
            counter += 1;
            b = counter;
        });
        a.for_each_value([&](float &a, float b) {
            a = 2*b;
        }, b);

        if (counter != W * H * C) {
            printf("for_each_value didn't hit every element\n");
            return -1;
        }

        a.for_each_element([&](int x, int y, int c) {
            // The original for_each_value iterated over b, which is
            // interleaved, so we expect the counter to count up c
            // fastest.
            float correct_b = 1 + c + C * (x + W * y);
            float correct_a = correct_b * 2;
            if (b(x, y, c) != correct_b) {
                printf("b(%d, %d, %d) = %f instead of %f\n",
                       x, y, c, b(x, y, c), correct_b);
                abort();
            }
            if (a(x, y, c) != correct_a) {
                printf("a(%d, %d, %d) = %f instead of %f\n",
                       x, y, c, a(x, y, c), correct_a);
                abort();
            }
        });
    }

    {
        // Check that copy() works to/from Buffer<void>
        Buffer<int> a(2, 2);
        a.fill(42);

        Buffer<> b = a.copy();
        assert(b.as<int>().all_equal(42));

        Buffer<int> c = b.copy();
        assert(c.all_equal(42));

        // This will fail at runtime, as c and d do not have identical types
        // Buffer<uint8_t> d = c.copy();
        // assert(d.all_equal(42));
    }

    {
        int data[4] = { 42, 42, 42, 42 };

        // Check that copy() works with const
        Buffer<const int> a(data, 2, 2);

        Buffer<const int> b = a.copy();
        assert(b.all_equal(42));
    }

    {
        // Check the fields get zero-initialized with the default constructor.
        uint8_t buf[sizeof(Halide::Runtime::Buffer<float>)];
        memset(&buf, 1, sizeof(buf));
        new (&buf) Halide::Runtime::Buffer<float>();
        // The dim and type fields should be non-zero, but the other
        // fields should all be zero. We'll just check the ones after
        // the halide_buffer_t.
        for (size_t i = sizeof(halide_buffer_t); i < sizeof(buf); i++) {
            assert(!buf[i]);
        }
    }

    {
        // check reset()
        Buffer<float> a(100, 3, 80);

        assert(a.dimensions() == 3);
        assert(a.number_of_elements() == 100 * 3 * 80);
        assert(a.type() == halide_type_of<float>());

        a.reset();
        assert(a.dimensions() == 0);
        assert(a.number_of_elements() == 1);
        assert(a.type() == halide_type_of<float>());

        Buffer<> b(halide_type_of<float>(), 10, 10);

        assert(b.dimensions() == 2);
        assert(b.number_of_elements() == 10 * 10);
        assert(b.type() == halide_type_of<float>());

        b.reset();
        assert(b.dimensions() == 0);
        assert(b.number_of_elements() == 1);
        assert(b.type() == halide_type_of<uint8_t>());
    }

    {
        // Check for_each_value on a const buffer(s)
        const int W = 5, H = 4, C = 3;
        Buffer<int> zero(W, H, C);
        zero.fill(0);

        const Buffer<int> a = zero.copy();
        const Buffer<int> b = zero.copy();
        const Buffer<int> &b_ref = b;
        Buffer<int> c = zero.copy();

        int counter = 0;
        a.for_each_value([&](const int &a_value) { counter += 1; });
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a.for_each_value([&](int a_value) { counter += 1; });
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a.for_each_value([&](int a_value, int b_value) { counter += 1; }, b);
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a.for_each_value([&](int a_value, const int &b_value) { counter += 1; }, b_ref);
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a.for_each_value([&](int a_value, const int &b_value, int &c_value_ref) {
            counter += 1;
            c_value_ref = 1;
        }, b, c);
        assert(counter == 5 * 4 * 3);
        assert(a.all_equal(0));
        assert(b.all_equal(0));
        assert(c.all_equal(1));

        counter = 0;
        c.for_each_value([&](int &c_value_ref, const int &b_value, int a_value) {
            counter += 1;
            c_value_ref = 2;
        }, b, a);
        assert(counter == 5 * 4 * 3);
        assert(a.all_equal(0));
        assert(b.all_equal(0));
        assert(c.all_equal(2));

        // Won't compile: a is const, can't specify a nonconst ref for value
        // a.for_each_value([&](int &a_value) { });

        // Won't compile: b is const, can't specify a nonconst ref for value
        // a.for_each_value([&](int a_value, int &b_value) { }, b);

        // Won't compile: b_ref is const, can't specify a nonconst ref for value
        // a.for_each_value([&](int a_value, int &b_value) { }, b_ref);

        // Won't compile: a and b are const, can't specify a nonconst ref for value
        // c.for_each_value([&](int c_value, int &a_value, int &b_value) { });
    }

    printf("Success!\n");
    return 0;
}
