#include "Halide.h"
#include <algorithm>
#include <future>

#include <cstdio>

using namespace Halide;

template<typename T>
bool expect_eq(Buffer<T> actual, Buffer<T> expected) {
    bool eq = true;
    expected.for_each_value(
        [&](const T &expected_val, const T &actual_val) {
            if (actual_val != expected_val) {
                eq = false;
                fprintf(stderr, "Failed: expected %d, actual %d\n",
                        (int)expected_val, (int)actual_val);
            }
        },
        actual);
    return eq;
}

template<typename CHUNK_TYPE>
auto gen_random_chunks(std::initializer_list<int> dims) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(
        std::numeric_limits<CHUNK_TYPE>::min(),
        std::numeric_limits<CHUNK_TYPE>::max());

    Buffer<CHUNK_TYPE> buf(dims);
    buf.for_each_value([&](CHUNK_TYPE &v) { v = dist(gen); });

    return buf;
}

namespace {
enum class ChunkOrder {
    LowestFirst,
    HighestFirst,
};
};

template<typename CHUNK_TYPE>
bool test_with_n_times_chunk_type(int num_chunks, Target t) {
    const int width = 256 * num_chunks;
    Buffer<CHUNK_TYPE> input_buf = gen_random_chunks<CHUNK_TYPE>({width});

    using WIDE_STORAGE_TYPE = uint64_t;
    const int CHUNK_WIDTH = 8 * sizeof(CHUNK_TYPE);
    const int WIDE_TYPE_WIDTH = CHUNK_WIDTH * num_chunks;

    Var x("x");

    int wide_width = width / num_chunks;

    auto forward = [wide_width, num_chunks, x, WIDE_TYPE_WIDTH,
                    t](const Func &input, ChunkOrder chunk_order) {
        Buffer<WIDE_STORAGE_TYPE> wide(wide_width);
        std::vector<Expr> input_chunks;
        for (int i = 0; i != num_chunks; ++i)
            input_chunks.push_back(input((num_chunks * x) + i));
        if (chunk_order != ChunkOrder::LowestFirst)
            std::reverse(input_chunks.begin(), input_chunks.end());
        Func widen("widened");
        widen(x) = reinterpret(UInt(WIDE_TYPE_WIDTH),
                               Internal::Shuffle::make_concat(input_chunks));
        Func store("store");
        store(x) = cast<WIDE_STORAGE_TYPE>(widen(x));
        store.realize(wide, t);
        return wide;
    };

    auto forward_naive = [wide_width, num_chunks](Buffer<CHUNK_TYPE> input_buf,
                                                  ChunkOrder chunk_order) {
        Buffer<WIDE_STORAGE_TYPE> wide(wide_width);
        for (int32_t x = 0; x < wide_width; x++) {
            WIDE_STORAGE_TYPE &v = wide(x);
            v = 0;
            for (int chunk = 0; chunk != num_chunks; ++chunk) {
                int chunk_idx = chunk_order == ChunkOrder::HighestFirst ?
                                    chunk :
                                    (num_chunks - 1) - chunk;
                v <<= CHUNK_WIDTH;
                v |= (WIDE_STORAGE_TYPE)input_buf(num_chunks * x + chunk_idx);
            }
        }
        return wide;
    };

    Func input("input");
    input(x) = input_buf(x);

    bool success = true;
    for (ChunkOrder chunk_order :
         {ChunkOrder::LowestFirst, ChunkOrder::HighestFirst}) {
        const auto wide_actual = forward(input, chunk_order);
        const auto wide_expected = forward_naive(input_buf, chunk_order);
        success &= expect_eq(wide_actual, wide_expected);
    }

    return success;
}

template<typename CHUNK_TYPE>
bool test_with_chunk_type(Target t) {
    bool success = true;

    const int CHUNK_WIDTH = 8 * sizeof(CHUNK_TYPE);
    for (int num_chunks = 2; CHUNK_WIDTH * num_chunks <= 64; num_chunks *= 2) {
        success &= test_with_n_times_chunk_type<CHUNK_TYPE>(num_chunks, t);
    }

    return success;
}

bool test_all(Target t) {
    bool success = true;

    success &= test_with_chunk_type<uint8_t>(t);
    success &= test_with_chunk_type<uint16_t>(t);
    success &= test_with_chunk_type<uint32_t>(t);

    return success;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    bool success = test_all(target);

    if (!success) {
        fprintf(stderr, "Failed!\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
