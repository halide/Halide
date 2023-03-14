#include <iostream>
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

    // Check copy_to_interleaved()
    assert(a.stride(0) == 1);
    auto a_interleaved = a.copy_to_interleaved();
    assert(a_interleaved.stride(0) == a_interleaved.channels());
    assert(a_interleaved.stride(2) == 1);
    check_equal(a, a_interleaved);

    // Check copy_to_planar()
    auto a_planar = a_interleaved.copy_to_planar();
    assert(a_planar.stride(0) == 1);
    check_equal(a, a_planar);
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
                                        {0, 3, 1 * 100},
                                        {0, 80, 1 * 100 * 3}};
        Buffer<float> a(nullptr, 3, shape_a);
        a.allocate();

        halide_dimension_t shape_b[] = {{0, 120, 1},
                                        {0, 80, 1 * 120},
                                        {0, 3, 1 * 120 * 80}};
        Buffer<float> b(nullptr, 3, shape_b);
        b.allocate();

        test_copy(a, b);
    }

    {
        // Check copying a buffer, using the vector<halide_dimension_t> ctors
        Buffer<float> a(nullptr, {{0, 100, 1},
                                  {0, 3, 1 * 100},
                                  {0, 80, 1 * 100 * 3}});
        a.allocate();

        Buffer<float> b(nullptr, {{0, 120, 1},
                                  {0, 80, 1 * 120},
                                  {0, 3, 1 * 120 * 80}});
        b.allocate();

        test_copy(a, b);
    }

    {
        // Check make a Buffer from a Buffer of a different type
        Buffer<float> a(100, 80);
        Buffer<const float> b(a);  // statically safe
        Buffer<const void> c(b);   // statically safe
        Buffer<const float> d(c);  // does runtime check of actual type.
        Buffer<void> e(a);         // statically safe
        Buffer<float> f(e);        // runtime checks

        static_assert(a.has_static_halide_type);
        static_assert(b.has_static_halide_type);
        static_assert(!c.has_static_halide_type);
        static_assert(d.has_static_halide_type);
        static_assert(!e.has_static_halide_type);
        static_assert(f.has_static_halide_type);

        static_assert(a.static_halide_type() == halide_type_of<float>());
        static_assert(b.static_halide_type() == halide_type_of<float>());
        static_assert(d.static_halide_type() == halide_type_of<float>());
        static_assert(f.static_halide_type() == halide_type_of<float>());
    }

    {
        // Check Buffers with static dimensionality
        Buffer<float, 2> a(100, 80);
        Buffer<float, 2> b(a);        // statically safe
        Buffer<float> c(a);           // checks at runtime (and succeeds)
        Buffer<float, AnyDims> d(a);  // same as previous, just explicit syntax
        Buffer<float, 2> e(d);        // checks at runtime (and succeeds because d.dims = 2)
        // Buffer<float, 3> f(a);     // won't compile: static_assert failure
        // Buffer<float, 3> g(c);     // fails at runtime: c.dims = 2

        static_assert(a.has_static_dimensions);
        static_assert(b.has_static_dimensions);
        static_assert(!c.has_static_dimensions);
        static_assert(!d.has_static_dimensions);
        static_assert(e.has_static_dimensions);

        static_assert(a.static_dimensions() == 2);
        static_assert(b.static_dimensions() == 2);
        static_assert(e.static_dimensions() == 2);

        Buffer<float> s1 = a.sliced(0);
        assert(s1.dimensions() == 1);
        assert(s1.dim(0).extent() == 80);

        Buffer<float, 1> s2 = a.sliced(1);
        assert(s2.dimensions() == 1);
        assert(s2.dim(0).extent() == 100);

        Buffer<float, 0> s3 = s2.sliced(0);
        static_assert(a.has_static_dimensions && s3.static_dimensions() == 0);
        assert(s3.dimensions() == 0);

        // auto s3a = s3.sliced(0);              // won't compile: can't call sliced() on a zero-dim buffer
        // Buffer<float, 2> s3b = a.sliced(0);   // won't compile: return type has incompatible dimensionality
        // a.slice(0);                           // won't compile: can't call slice() on static-dimensioned buffer

        Buffer<float> s4 = a.sliced(0);  // assign to dynamic-dimensioned result
        static_assert(!s4.has_static_dimensions);
        assert(s4.dimensions() == 1);

        s4.slice(0);  // ok to call on dynamic-dimensioned
        assert(s4.dimensions() == 0);

        Buffer<float, 0> e0 = Buffer<float, 0>::make_scalar();

        auto e1 = e0.embedded(0);
        static_assert(e1.has_static_dimensions && e1.static_dimensions() == 1);
        assert(e1.dimensions() == 1);

        // Buffer<float, 0> e2 = a.embedded(0);  // won't compile: return type has incompatible dimensionality
        // e1.embed(0);                          // won't compile: can't call embed() on static-dimensioned buffer

        Buffer<float> e3 = e0.embedded(0);  // assign to dynamic-dimensioned result
        static_assert(!e3.has_static_dimensions);
        assert(e3.dimensions() == 1);

        e3.embed(0);  // ok to call on dynamic-dimensioned
        assert(e3.dimensions() == 2);
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
            a = 2 * b;
        },
                         b);

        if (counter != W * H * C) {
            printf("for_each_value didn't hit every element\n");
            return 1;
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
        int data[4] = {42, 42, 42, 42};

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
        const Buffer<const int> a_const = a;
        const Buffer<int> b = zero.copy();
        const Buffer<const int> b_const = b;
        Buffer<int> c = zero.copy();
        int counter;

        counter = 0;
        a.for_each_value([&](const int &a_value) { counter += 1; });
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a.for_each_value([&](int a_value) { counter += 1; });
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a.for_each_value([&](int a_value, int b_value) { counter += 1; }, b);
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a.for_each_value([&](int a_value, const int &b_value) { counter += 1; }, b);
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a_const.for_each_value([&](const int &a_value) { counter += 1; });
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a_const.for_each_value([&](int a_value) { counter += 1; });
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a_const.for_each_value([&](int a_value, int b_value) { counter += 1; }, b_const);
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a_const.for_each_value([&](int a_value, const int &b_value) { counter += 1; }, b_const);
        assert(counter == 5 * 4 * 3);

        counter = 0;
        a.for_each_value([&](int a_value, const int &b_value, int &c_value_ref) {
            counter += 1;
            c_value_ref = 1;
        },
                         b, c);
        assert(counter == 5 * 4 * 3);
        assert(a.all_equal(0));
        assert(b.all_equal(0));
        assert(c.all_equal(1));

        counter = 0;
        c.for_each_value([&](int &c_value_ref, const int &b_value, int a_value) {
            counter += 1;
            c_value_ref = 2;
        },
                         b, a);
        assert(counter == 5 * 4 * 3);
        assert(a.all_equal(0));
        assert(b.all_equal(0));
        assert(c.all_equal(2));

        counter = 0;
        a_const.for_each_value([&](int a_value, const int &b_value, int &c_value_ref) {
            counter += 1;
            c_value_ref = 1;
        },
                               b_const, c);
        assert(counter == 5 * 4 * 3);
        assert(a.all_equal(0));
        assert(b.all_equal(0));
        assert(c.all_equal(1));

        counter = 0;
        c.for_each_value([&](int &c_value_ref, const int &b_value, int a_value) {
            counter += 1;
            c_value_ref = 2;
        },
                         b_const, a_const);
        assert(counter == 5 * 4 * 3);
        assert(a.all_equal(0));
        assert(b.all_equal(0));
        assert(c.all_equal(2));

        // Won't compile: a_const is const T, can't specify a nonconst ref for value
        // a_const.for_each_value([&](int &a_value) { });

        // Won't compile: b_const is const, can't specify a nonconst ref for value
        // a.for_each_value([&](int a_value, int &b_value) { }, b_const);

        // Won't compile: a is const, can't specify a nonconst ref for value
        // c.for_each_value([&](int c_value, int &a_value, int &b_value) { }, a_const, b);

        // Won't compile: a and b are const, can't specify a nonconst ref for value
        // c.for_each_value([&](int c_value, int a_value, int &b_value) { }, a_const, b_const);
    }

    {
        // Check initializing const buffers via return ref from fill(), etc
        const int W = 5, H = 4;

        const Buffer<const int> a = Buffer<int>(W, H).fill(1);
        assert(a.all_equal(1));

        const Buffer<const int> b = Buffer<int>(W, H).for_each_value([](int &value) { value = 2; });
        assert(b.all_equal(2));

        // for_each_element()'s callback doesn't get the Buffer itself, so we need a named temp here
        auto c0 = Buffer<int>(W, H);
        const Buffer<const int> c = c0.for_each_element([&](int x, int y) { c0(x, y) = 3; });
        assert(c.all_equal(3));

        const Buffer<const int> d = Buffer<int>(W, H).fill([](int x, int y) -> int { return 4; });
        assert(d.all_equal(4));
    }

    {
        constexpr int W = 7, H = 5, C = 3, Z = 2;

        // test reorder() and the related ctors
        auto a = Buffer<uint8_t>({W, H, C}, {2, 0, 1});
        assert(a.dim(0).extent() == W);
        assert(a.dim(1).extent() == H);
        assert(a.dim(2).extent() == C);
        assert(a.dim(2).stride() == 1);
        assert(a.dim(0).stride() == C);
        assert(a.dim(1).stride() == W * C);

        auto b = Buffer<uint8_t>({W, H, C, Z}, {2, 3, 0, 1});
        assert(b.dim(0).extent() == W);
        assert(b.dim(1).extent() == H);
        assert(b.dim(2).extent() == C);
        assert(b.dim(3).extent() == Z);
        assert(b.dim(2).stride() == 1);
        assert(b.dim(3).stride() == C);
        assert(b.dim(0).stride() == C * Z);
        assert(b.dim(1).stride() == W * C * Z);

        auto b2 = Buffer<uint8_t>(C, Z, W, H);
        assert(b.dim(0).extent() == b2.dim(2).extent());
        assert(b.dim(1).extent() == b2.dim(3).extent());
        assert(b.dim(2).extent() == b2.dim(0).extent());
        assert(b.dim(3).extent() == b2.dim(1).extent());
        assert(b.dim(0).stride() == b2.dim(2).stride());
        assert(b.dim(1).stride() == b2.dim(3).stride());
        assert(b.dim(2).stride() == b2.dim(0).stride());
        assert(b.dim(3).stride() == b2.dim(1).stride());

        b2.transpose({2, 3, 0, 1});
        assert(b.dim(0).extent() == b2.dim(0).extent());
        assert(b.dim(1).extent() == b2.dim(1).extent());
        assert(b.dim(2).extent() == b2.dim(2).extent());
        assert(b.dim(3).extent() == b2.dim(3).extent());
        assert(b.dim(0).stride() == b2.dim(0).stride());
        assert(b.dim(1).stride() == b2.dim(1).stride());
        assert(b.dim(2).stride() == b2.dim(2).stride());
        assert(b.dim(3).stride() == b2.dim(3).stride());
    }

    printf("Success!\n");
    return 0;
}
