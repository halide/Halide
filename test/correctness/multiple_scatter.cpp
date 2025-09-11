#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using std::vector;

namespace {
class MultipleScatterTest : public ::testing::Test {};
}  // namespace

TEST_F(MultipleScatterTest, SortingNetwork) {
    // Implement a sorting network using update definitions that write to multiple outputs

    // The links in the sorting network. Sorts 8 things.
    int network_[19][2] =
        {{0, 1},
         {2, 3},
         {4, 5},
         {6, 7},
         {0, 2},
         {1, 3},
         {4, 6},
         {5, 7},
         {1, 2},
         {5, 6},
         {0, 4},
         {3, 7},
         {1, 5},
         {2, 6},
         {1, 4},
         {3, 6},
         {2, 4},
         {3, 5},
         {3, 4}};

    Buffer<int> network(&network_[0][0], 2, 19);

    Buffer<int> input(128, 8);

    input.fill(std::mt19937{0});

    Func sorted1;
    Var x, y;

    // Run the sorting network with an RDom over the links
    sorted1(x, y) = input(x, y);
    RDom r(0, network.dim(1).extent());

    // We know that the network we'll use caps out at 7, but the
    // compiler doesn't know that because it's coming from an input
    // buffer, so use unsafe_promise_clamped.
    Expr min_idx = unsafe_promise_clamped(network(0, r), 0, 7);
    Expr max_idx = unsafe_promise_clamped(network(1, r), 0, 7);

    sorted1(x, scatter(min_idx, max_idx)) =
        gather(min(sorted1(x, min_idx), sorted1(x, max_idx)),
               max(sorted1(x, min_idx), sorted1(x, max_idx)));

    sorted1.vectorize(x, 8).update().vectorize(x, 8);

    Buffer<int> output1(128, 8), output2(128, 8);
    sorted1.realize({output1});

    // Run the sorting network fully unrolled as a single big multi-scatter
    Func sorted2;
    sorted2(x, y) = input(x, y);

    vector<Expr> lhs, rhs;
    for (int i = 0; i < 8; i++) {
        lhs.emplace_back(i);
        rhs.emplace_back(sorted2(x, i));
    }

    for (int l = 0; l < network.dim(1).extent(); l++) {
        int min_idx = network(0, l);
        int max_idx = network(1, l);
        Expr tmp = rhs[min_idx];
        // We're going to be asking a lot of CSE
        rhs[min_idx] = min(rhs[min_idx], rhs[max_idx]);
        rhs[max_idx] = max(tmp, rhs[max_idx]);
    }

    sorted2(x, scatter(lhs)) = gather(rhs);
    sorted2.vectorize(x, 8).update().vectorize(x, 8);

    sorted2.realize({output2});

    for (int i = 0; i < output1.dim(0).extent(); i++) {
        vector<int> correct(output1.dim(1).extent());
        for (int j = 0; j < output1.dim(1).extent(); j++) {
            correct[j] = input(i, j);
        }
        std::sort(correct.begin(), correct.end());
        for (int j = 0; j < output1.dim(1).extent(); j++) {
            SCOPED_TRACE(testing::Message() << "i = " << i << ", j = " << j);
            EXPECT_EQ(output1(i, j), correct[j]);
            EXPECT_EQ(output2(i, j), correct[j]);
        }
    }
}

TEST_F(MultipleScatterTest, InPlaceRotation) {
    // An update definitions that rotates a square region in-place.
    const int sz = 17;
    SCOPED_TRACE(testing::Message() << "sz = " << sz);

    Buffer<uint8_t> input(sz, sz);
    std::mt19937 rng;
    input.fill([&](int x, int y) { return (uint8_t)(rng() & 0xff); });

    Func rot;
    Var x, y;
    rot(x, y) = input(x, y);

    RDom r(0, (sz + 1) / 2, 0, sz / 2);

    vector<Expr> src_x{r.x, sz - 1 - r.y, sz - 1 - r.x, r.y};
    vector<Expr> src_y{r.y, r.x, sz - 1 - r.y, sz - 1 - r.x};
    vector<Expr> dst_x = src_x, dst_y = src_y;

    std::rotate(dst_x.begin(), dst_x.begin() + 1, dst_x.end());
    std::rotate(dst_y.begin(), dst_y.begin() + 1, dst_y.end());

    rot(scatter(dst_x), scatter(dst_y)) =
        rot(gather(src_x), gather(src_y));

    Buffer<uint8_t> output = rot.realize({sz, sz});

    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            EXPECT_EQ(output(x, y), input(y, sz - 1 - x)) << "x = " << x << ", y = " << y;
        }
    }
}

TEST_F(MultipleScatterTest, AtomicComplexMultiplication) {
    // Atomic complex multiplication modulo 2^256 where the complex numbers
    // are a dimension of the Func rather than a tuple

    Buffer<uint8_t> input(2, 100);
    std::mt19937 rng;
    input.fill([&](int x, int y) { return (uint8_t)(rng() & 0xff); });

    Func prod;
    Var x;
    RDom r(0, input.dim(1).extent());
    prod(x) = cast<uint8_t>(mux(x, {1, 0}));
    prod(scatter(0, 1)) =
        gather(prod(0) * input(0, r) - prod(1) * input(1, r),
               prod(0) * input(1, r) + prod(1) * input(0, r));

    // TODO: We don't currently recognize this as an
    //   associative update, so for now we force it by passing
    //   'true' to atomic().
    prod.update().atomic(true).parallel(r);

    Buffer<uint8_t> result = prod.realize({2});

    uint8_t correct_re = 1, correct_im = 0;
    for (int i = 0; i < input.dim(1).extent(); i++) {
        int new_re = correct_re * input(0, i) - correct_im * input(1, i);
        int new_im = correct_re * input(1, i) + correct_im * input(0, i);
        correct_re = new_re;
        correct_im = new_im;
    }

    EXPECT_EQ(result(0), correct_re) << "Real part mismatch";
    EXPECT_EQ(result(1), correct_im) << "Imaginary part mismatch";
}

TEST_F(MultipleScatterTest, LexicographicBubbleSort) {
    // Lexicographic bubble sort on tuples
    Func f;
    Var x, y;

    f(x) = {13 - (x % 10), cast<uint8_t>(x * 17)};

    RDom r(0, 99, 0, 99);
    r.where(r.x < 99 - r.y);

    Expr should_swap = (f(r.x)[0] > f(r.x + 1)[0] ||
                        (f(r.x)[0] == f(r.x + 1)[0] &&
                         f(r.x)[1] > f(r.x + 1)[1]));
    r.where(should_swap);

    // Swap elements that satisfy the RDom predicate
    f(scatter(r.x, r.x + 1)) = f(gather(r.x + 1, r.x));

    Buffer<int> out_0(100);
    Buffer<uint8_t> out_1(100);
    f.realize({out_0, out_1});

    for (int i = 0; i < 99; i++) {
        auto out_i = std::make_pair(out_0(i), (int)out_1(i));
        auto out_j = std::make_pair(out_0(i + 1), (int)out_1(i + 1));
        EXPECT_LT(out_i, out_j) << "i = " << i << ", j = " << i + 1;
    }
}

TEST_F(MultipleScatterTest, ScatterWithoutGather) {
    // A scatter can exist without a gather if you're just broadcasting
    Func f;
    Var x;
    f(x) = 0;
    f(scatter(0, 1, 2, 3)) = 5;

    Buffer<int> out = f.realize({5});
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(out(i), i < 4 ? 5 : 0) << "i = " << i;
    }
}

TEST_F(MultipleScatterTest, GatherWithoutScatter) {
    // A gather can exist without a scatter, but it's sort of
    // silly because last element wins. It's not outright
    // disallowed because it may be a degenerate case of some
    // generic code.
    Func f;
    Var x;
    f(x) = 0;
    f(3) = gather(1, 9);

    Buffer<int> out = f.realize({5});
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(out(i), i == 3 ? 9 : 0) << "i = " << i;
    }
}
