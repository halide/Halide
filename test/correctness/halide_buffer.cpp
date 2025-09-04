#include <gtest/gtest.h>
// Don't include Halide.h: it is not necessary for this test.
#include "HalideBuffer.h"

using namespace Halide::Runtime;

namespace {
void *my_malloced_addr = nullptr;
int my_malloc_count = 0;
void *my_freed_addr = nullptr;
int my_free_count = 0;

void *my_malloc(size_t size) {
    void *ptr = malloc(size);
    my_malloced_addr = ptr;
    my_malloc_count++;
    return ptr;
}

void my_free(void *ptr) {
    my_freed_addr = ptr;
    my_free_count++;
    free(ptr);
}

template<typename T1, typename T2>
void expect_equal_shape(const Buffer<T1> &a, const Buffer<T2> &b) {
    EXPECT_EQ(a.dimensions(), b.dimensions());
    for (int i = 0; i < a.dimensions(); i++) {
        EXPECT_EQ(a.dim(i).min(), b.dim(i).min());
        EXPECT_EQ(a.dim(i).extent(), b.dim(i).extent());
    }
}

template<typename T1, typename T2>
void expect_equal(const Buffer<T1> &a, const Buffer<T2> &b) {
    expect_equal_shape(a, b);
    a.for_each_element([&](const int *pos) {
        EXPECT_EQ(a(pos), b(pos)) << "Mismatch at pos[0]=" << pos[0] << " pos[1]=" << pos[1] << " pos[2]=" << pos[2];
    });
}

void test_copy(Buffer<float> a, Buffer<float> b) {
    // Mess with the memory layout to make it more interesting
    a.transpose(1, 2);

    a.fill(1.0f);

    EXPECT_TRUE(a.all_equal(1.0f));

    b.fill([&](int x, int y, int c) {
        return x + 100.0f * y + 100000.0f * c;
    });

    b.for_each_element([&](int x, int y, int c) {
        EXPECT_EQ(b(x, y, c), x + 100.0f * y + 100000.0f * c);
    });

    expect_equal(a, a.copy());

    // Check copying from one subregion to another (with different memory layout)
    Buffer<float> a_window = a.cropped(0, 20, 20).cropped(1, 50, 10);
    Buffer<const float> b_window = b.cropped(0, 20, 20).cropped(1, 50, 10);
    a_window.copy_from(b);

    expect_equal(a_window, b_window);

    // Check copying from const to nonconst
    Buffer<float> a_window_nonconst = b_window.copy();
    expect_equal(a_window_nonconst, b_window);

    // Check copying from const to const
    Buffer<const float> a_window_const = b_window.copy();
    expect_equal(a_window_const, b_window);

    // You don't actually have to crop a.
    a.fill(1.0f);
    a.copy_from(b_window);
    expect_equal(a_window, b_window);

    // The buffers can have dynamic type
    Buffer<void> a_void(a);
    Buffer<const void> b_void_window(b_window);
    a.fill(1.0f);
    a_void.copy_from(b_void_window);
    expect_equal(a_window, b_window);

    // Check copy_to_interleaved()
    EXPECT_EQ(a.stride(0), 1);
    auto a_interleaved = a.copy_to_interleaved();
    EXPECT_EQ(a_interleaved.stride(0), a_interleaved.channels());
    EXPECT_EQ(a_interleaved.stride(2), 1);
    expect_equal(a, a_interleaved);

    // Check copy_to_planar()
    auto a_planar = a_interleaved.copy_to_planar();
    EXPECT_EQ(a_planar.stride(0), 1);
    expect_equal(a, a_planar);
}
}  // namespace

TEST(HalideBuffer, BasicCopy) {
    // Check copying a buffer
    Buffer<float> a(100, 3, 80), b(120, 80, 3);
    test_copy(a, b);
}

TEST(HalideBuffer, CopyWithHalideDimensionPtr) {
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

TEST(HalideBuffer, CopyWithVectorDimensions) {
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

TEST(HalideBuffer, TypeConversions) {
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

TEST(HalideBuffer, StaticDimensionality) {
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
    EXPECT_EQ(s1.dimensions(), 1);
    EXPECT_EQ(s1.dim(0).extent(), 80);

    Buffer<float, 1> s2 = a.sliced(1);
    EXPECT_EQ(s2.dimensions(), 1);
    EXPECT_EQ(s2.dim(0).extent(), 100);

    Buffer<float, 0> s3 = s2.sliced(0);
    static_assert(a.has_static_dimensions && s3.static_dimensions() == 0);
    EXPECT_EQ(s3.dimensions(), 0);

    // auto s3a = s3.sliced(0);              // won't compile: can't call sliced() on a zero-dim buffer
    // Buffer<float, 2> s3b = a.sliced(0);   // won't compile: return type has incompatible dimensionality
    // a.slice(0);                           // won't compile: can't call slice() on static-dimensioned buffer

    Buffer<float> s4 = a.sliced(0);  // assign to dynamic-dimensioned result
    static_assert(!s4.has_static_dimensions);
    EXPECT_EQ(s4.dimensions(), 1);

    s4.slice(0);  // ok to call on dynamic-dimensioned
    EXPECT_EQ(s4.dimensions(), 0);

    Buffer<float, 0> e0 = Buffer<float, 0>::make_scalar();

    auto e1 = e0.embedded(0);
    static_assert(e1.has_static_dimensions && e1.static_dimensions() == 1);
    EXPECT_EQ(e1.dimensions(), 1);

    // Buffer<float, 0> e2 = a.embedded(0);  // won't compile: return type has incompatible dimensionality
    // e1.embed(0);                          // won't compile: can't call embed() on static-dimensioned buffer

    Buffer<float> e3 = e0.embedded(0);  // assign to dynamic-dimensioned result
    static_assert(!e3.has_static_dimensions);
    EXPECT_EQ(e3.dimensions(), 1);

    e3.embed(0);  // ok to call on dynamic-dimensioned
    EXPECT_EQ(e3.dimensions(), 2);
}

TEST(HalideBuffer, MovingBuffer) {
    // Check moving a buffer around
    Buffer<float> a(100, 80, 3);
    a.for_each_element([&](int x, int y, int c) {
        a(x, y, c) = x + 100.0f * y + 100000.0f * c;
    });
    Buffer<float> b(a);
    b.set_min(123, 456, 2);
    b.translate({-123, -456, -2});
    expect_equal(a, b);
}

TEST(HalideBuffer, AutoConversions) {
    Buffer<float> a(100, 80, 3);
    a.for_each_element([&](int x, int y, int c) {
        a(x, y, c) = x + 100.0f * y + 100000.0f * c;
    });
    Buffer<float> b(a);

    // Check that Buffer<T> will autoconvert to Buffer<const T>&
    const auto check_equal_non_const_ref = [](Buffer<const float> &a, Buffer<const float> &b) {
        expect_equal(a, b);
    };
    check_equal_non_const_ref(a, b);

    // Check that Buffer<T> will autoconvert to Buffer<void>&
    const auto check_equal_non_const_void_ref = [](Buffer<void> &a, Buffer<void> &b) {
        expect_equal(a.as<float>(), b.as<float>());
    };
    check_equal_non_const_void_ref(a, b);

    // Check that Buffer<const T> will autoconvert to Buffer<const void>&
    const auto check_equal_const_void_ref = [](Buffer<const void> &a, Buffer<const void> &b) {
        expect_equal(a.as<const float>(), b.as<const float>());
    };
    Buffer<const float> ac = a;
    Buffer<const float> bc = b;
    check_equal_const_void_ref(ac, bc);
}

TEST(HalideBuffer, ForEachValueLifting) {
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

    EXPECT_EQ(counter, W * H * C) << "for_each_value didn't hit every element";

    a.for_each_element([&](int x, int y, int c) {
        // The original for_each_value iterated over b, which is
        // interleaved, so we expect the counter to count up c
        // fastest.
        float correct_b = 1 + c + C * (x + W * y);
        float correct_a = correct_b * 2;
        EXPECT_EQ(b(x, y, c), correct_b) << "b(" << x << ", " << y << ", " << c << ")";
        EXPECT_EQ(a(x, y, c), correct_a) << "a(" << x << ", " << y << ", " << c << ")";
    });
}

TEST(HalideBuffer, VoidBufferCopy) {
    // Check that copy() works to/from Buffer<void>
    Buffer<int> a(2, 2);
    a.fill(42);

    Buffer<> b = a.copy();
    EXPECT_TRUE(b.as<int>().all_equal(42));

    Buffer<int> c = b.copy();
    EXPECT_TRUE(c.all_equal(42));

    // This will fail at runtime, as c and d do not have identical types
    // Buffer<uint8_t> d = c.copy();
    // assert(d.all_equal(42));
}

TEST(HalideBuffer, ConstBufferCopy) {
    int data[4] = {42, 42, 42, 42};

    // Check that copy() works with const
    Buffer<const int> a(data, 2, 2);

    Buffer<const int> b = a.copy();
    EXPECT_TRUE(b.all_equal(42));
}

TEST(HalideBuffer, DefaultConstructorZeroInit) {
    // Check the fields get zero-initialized with the default constructor.
    uint8_t buf[sizeof(Halide::Runtime::Buffer<float>)];
    memset(&buf, 1, sizeof(buf));
    new (&buf) Halide::Runtime::Buffer<float>();
    // The dim and type fields should be non-zero, but the other
    // fields should all be zero. We'll just check the ones after
    // the halide_buffer_t.
    for (size_t i = sizeof(halide_buffer_t); i < sizeof(buf); i++) {
        EXPECT_EQ(buf[i], 0);
    }
}

TEST(HalideBuffer, Reset) {
    // check reset()
    Buffer<float> a(100, 3, 80);

    EXPECT_EQ(a.dimensions(), 3);
    EXPECT_EQ(a.number_of_elements(), 100 * 3 * 80);
    EXPECT_EQ(a.type(), halide_type_of<float>());

    a.reset();
    EXPECT_EQ(a.dimensions(), 0);
    EXPECT_EQ(a.number_of_elements(), 1);
    EXPECT_EQ(a.type(), halide_type_of<float>());

    Buffer<> b(halide_type_of<float>(), 10, 10);

    EXPECT_EQ(b.dimensions(), 2);
    EXPECT_EQ(b.number_of_elements(), 10 * 10);
    EXPECT_EQ(b.type(), halide_type_of<float>());

    b.reset();
    EXPECT_EQ(b.dimensions(), 0);
    EXPECT_EQ(b.number_of_elements(), 1);
    EXPECT_EQ(b.type(), halide_type_of<uint8_t>());
}

TEST(HalideBuffer, ForEachValueConst) {
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
    EXPECT_EQ(counter, 5 * 4 * 3);

    counter = 0;
    a.for_each_value([&](int a_value) { counter += 1; });
    EXPECT_EQ(counter, 5 * 4 * 3);

    counter = 0;
    a.for_each_value([&](int a_value, int b_value) { counter += 1; }, b);
    EXPECT_EQ(counter, 5 * 4 * 3);

    counter = 0;
    a.for_each_value([&](int a_value, const int &b_value) { counter += 1; }, b);
    EXPECT_EQ(counter, 5 * 4 * 3);

    counter = 0;
    a_const.for_each_value([&](const int &a_value) { counter += 1; });
    EXPECT_EQ(counter, 5 * 4 * 3);

    counter = 0;
    a_const.for_each_value([&](int a_value) { counter += 1; });
    EXPECT_EQ(counter, 5 * 4 * 3);

    counter = 0;
    a_const.for_each_value([&](int a_value, int b_value) { counter += 1; }, b_const);
    EXPECT_EQ(counter, 5 * 4 * 3);

    counter = 0;
    a_const.for_each_value([&](int a_value, const int &b_value) { counter += 1; }, b_const);
    EXPECT_EQ(counter, 5 * 4 * 3);

    counter = 0;
    a.for_each_value(
        [&](int a_value, const int &b_value, int &c_value_ref) {
            counter += 1;
            c_value_ref = 1;
        },
        b, c);
    EXPECT_EQ(counter, 5 * 4 * 3);
    EXPECT_TRUE(a.all_equal(0));
    EXPECT_TRUE(b.all_equal(0));
    EXPECT_TRUE(c.all_equal(1));

    counter = 0;
    c.for_each_value(
        [&](int &c_value_ref, const int &b_value, int a_value) {
            counter += 1;
            c_value_ref = 2;
        },
        b, a);
    EXPECT_EQ(counter, 5 * 4 * 3);
    EXPECT_TRUE(a.all_equal(0));
    EXPECT_TRUE(b.all_equal(0));
    EXPECT_TRUE(c.all_equal(2));

    counter = 0;
    a_const.for_each_value(
        [&](int a_value, const int &b_value, int &c_value_ref) {
            counter += 1;
            c_value_ref = 1;
        },
        b_const, c);
    EXPECT_EQ(counter, 5 * 4 * 3);
    EXPECT_TRUE(a.all_equal(0));
    EXPECT_TRUE(b.all_equal(0));
    EXPECT_TRUE(c.all_equal(1));

    counter = 0;
    c.for_each_value([&](int &c_value_ref, const int &b_value, int a_value) {
        counter += 1;
        c_value_ref = 2;
    },
                     b_const, a_const);
    EXPECT_EQ(counter, 5 * 4 * 3);
    EXPECT_TRUE(a.all_equal(0));
    EXPECT_TRUE(b.all_equal(0));
    EXPECT_TRUE(c.all_equal(2));

    // Won't compile: a_const is const T, can't specify a nonconst ref for value
    // a_const.for_each_value([&](int &a_value) { });

    // Won't compile: b_const is const, can't specify a nonconst ref for value
    // a.for_each_value([&](int a_value, int &b_value) { }, b_const);

    // Won't compile: a is const, can't specify a nonconst ref for value
    // c.for_each_value([&](int c_value, int &a_value, int &b_value) { }, a_const, b);

    // Won't compile: a and b are const, can't specify a nonconst ref for value
    // c.for_each_value([&](int c_value, int a_value, int &b_value) { }, a_const, b_const);
}

TEST(HalideBuffer, ConstBufferFromFilledBuffer) {
    // Check initializing const buffers via return ref from fill(), etc
    const int W = 5, H = 4;

    const Buffer<const int> a = Buffer<int>(W, H).fill(1);
    EXPECT_TRUE(a.all_equal(1));

    const Buffer<const int> b = Buffer<int>(W, H).for_each_value([](int &value) { value = 2; });
    EXPECT_TRUE(b.all_equal(2));

    // for_each_element()'s callback doesn't get the Buffer itself, so we need a named temp here
    auto c0 = Buffer<int>(W, H);
    const Buffer<const int> c = c0.for_each_element([&](int x, int y) { c0(x, y) = 3; });
    EXPECT_TRUE(c.all_equal(3));

    const Buffer<const int> d = Buffer<int>(W, H).fill([](int x, int y) -> int { return 4; });
    EXPECT_TRUE(d.all_equal(4));
}

TEST(HalideBuffer, ReorderDimensions) {
    constexpr int W = 7, H = 5, C = 3, Z = 2;

    // test reorder() and the related ctors
    auto a = Buffer<uint8_t>({W, H, C}, {2, 0, 1});
    EXPECT_EQ(a.dim(0).extent(), W);
    EXPECT_EQ(a.dim(1).extent(), H);
    EXPECT_EQ(a.dim(2).extent(), C);
    EXPECT_EQ(a.dim(2).stride(), 1);
    EXPECT_EQ(a.dim(0).stride(), C);
    EXPECT_EQ(a.dim(1).stride(), W * C);

    auto b = Buffer<uint8_t>({W, H, C, Z}, {2, 3, 0, 1});
    EXPECT_EQ(b.dim(0).extent(), W);
    EXPECT_EQ(b.dim(1).extent(), H);
    EXPECT_EQ(b.dim(2).extent(), C);
    EXPECT_EQ(b.dim(3).extent(), Z);
    EXPECT_EQ(b.dim(2).stride(), 1);
    EXPECT_EQ(b.dim(3).stride(), C);
    EXPECT_EQ(b.dim(0).stride(), C * Z);
    EXPECT_EQ(b.dim(1).stride(), W * C * Z);

    auto b2 = Buffer<uint8_t>(C, Z, W, H);
    EXPECT_EQ(b.dim(0).extent(), b2.dim(2).extent());
    EXPECT_EQ(b.dim(1).extent(), b2.dim(3).extent());
    EXPECT_EQ(b.dim(2).extent(), b2.dim(0).extent());
    EXPECT_EQ(b.dim(3).extent(), b2.dim(1).extent());
    EXPECT_EQ(b.dim(0).stride(), b2.dim(2).stride());
    EXPECT_EQ(b.dim(1).stride(), b2.dim(3).stride());
    EXPECT_EQ(b.dim(2).stride(), b2.dim(0).stride());
    EXPECT_EQ(b.dim(3).stride(), b2.dim(1).stride());

    b2.transpose({2, 3, 0, 1});
    EXPECT_EQ(b.dim(0).extent(), b2.dim(0).extent());
    EXPECT_EQ(b.dim(1).extent(), b2.dim(1).extent());
    EXPECT_EQ(b.dim(2).extent(), b2.dim(2).extent());
    EXPECT_EQ(b.dim(3).extent(), b2.dim(3).extent());
    EXPECT_EQ(b.dim(0).stride(), b2.dim(0).stride());
    EXPECT_EQ(b.dim(1).stride(), b2.dim(1).stride());
    EXPECT_EQ(b.dim(2).stride(), b2.dim(2).stride());
    EXPECT_EQ(b.dim(3).stride(), b2.dim(3).stride());
}

TEST(HalideBuffer, CustomAllocators) {
    // Test setting default allocate and deallocate functions.
    Buffer<>::set_default_allocate_fn(my_malloc);
    Buffer<>::set_default_deallocate_fn(my_free);

    EXPECT_EQ(my_malloc_count, 0);
    EXPECT_EQ(my_free_count, 0);
    auto b = Buffer<uint8_t, 2>(5, 4).fill(1);
    EXPECT_NE(my_malloced_addr, nullptr);
    EXPECT_LT(my_malloced_addr, b.data());
    EXPECT_EQ(my_malloc_count, 1);
    EXPECT_EQ(my_free_count, 0);
    b.deallocate();
    EXPECT_EQ(my_malloc_count, 1);
    EXPECT_EQ(my_free_count, 1);
    EXPECT_EQ(my_malloced_addr, my_freed_addr);
}
