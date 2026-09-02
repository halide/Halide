#ifndef SUPERBLURS_FLOATVECTOR_H
#define SUPERBLURS_FLOATVECTOR_H

#include <assert.h>
#include <cstring>
#include <stdint.h>
#include <stdio.h>
#include <tuple>

enum OutputMode {
    Assign,
    Accum
};

constexpr int vec_lanes = 16;
constexpr int log_vec_lanes = 4;
typedef float floatx128_vec __attribute__((ext_vector_type(128), aligned(64)));
typedef float floatx64_vec __attribute__((ext_vector_type(64), aligned(64)));
typedef float floatx32_vec __attribute__((ext_vector_type(32), aligned(64)));
typedef float float_vec __attribute__((ext_vector_type(vec_lanes)));
typedef float floatx8_vec __attribute__((ext_vector_type(8)));
typedef float floatx4_vec __attribute__((ext_vector_type(4)));
typedef float floatx2_vec __attribute__((ext_vector_type(2)));

typedef float unaligned_float_vec __attribute__((ext_vector_type(vec_lanes), aligned(4)));

template<int lanes>
struct float_vec_helper;

template<>
struct float_vec_helper<128> {
    using type = floatx128_vec;
};

template<>
struct float_vec_helper<64> {
    using type = floatx64_vec;
};

template<>
struct float_vec_helper<32> {
    using type = floatx32_vec;
};

template<>
struct float_vec_helper<16> {
    using type = float_vec;
};

template<>
struct float_vec_helper<8> {
    using type = floatx8_vec;
};

template<>
struct float_vec_helper<4> {
    using type = floatx4_vec;
};

template<>
struct float_vec_helper<2> {
    using type = floatx2_vec;
};

template<>
struct float_vec_helper<1> {
    using type = float;
};

template<int lanes>
using float_vec_t = typename float_vec_helper<lanes>::type;

// A static constructor that flushes denormals to zero for everything including
// this file.
static struct FlushDenormalsToZeroHelper {
    FlushDenormalsToZeroHelper() {
        // Make sure we're flushing denormals to zero. When checking the impulse
        // response of stable IIRs we're going to produce a lot of denormals in the
        // long tail.
        constexpr int CSR_FLUSH_TO_ZERO = (1 << 15);
        unsigned csr = __builtin_ia32_stmxcsr();
        csr |= CSR_FLUSH_TO_ZERO;
        __builtin_ia32_ldmxcsr(csr);
    }
} flush_denormals_to_zero_helper;

// Some helpers so we can define shuffle masks with constexpr functions rather
// than needing macros everywhere.
using shuffle_mask_16 = std::tuple<int, int, int, int, int, int, int, int,
                                   int, int, int, int, int, int, int, int>;

using shuffle_mask_8 = std::tuple<int, int, int, int, int, int, int, int>;

constexpr shuffle_mask_16 array_to_tuple_16(int arr[16]) {
    return std::make_tuple(arr[0], arr[1], arr[2], arr[3],
                           arr[4], arr[5], arr[6], arr[7],
                           arr[8], arr[9], arr[10], arr[11],
                           arr[12], arr[13], arr[14], arr[15]);
}

constexpr shuffle_mask_8 array_to_tuple_8(int arr[8]) {
    return std::make_tuple(arr[0], arr[1], arr[2], arr[3],
                           arr[4], arr[5], arr[6], arr[7]);
}

template<int len>
constexpr auto array_to_tuple(int arr[len]) {
    if constexpr (len == 8) {
        return array_to_tuple_8(arr);
    } else {
        return array_to_tuple_16(arr);
    }
}

constexpr shuffle_mask_16 generate_rotate(int o) {
    int result[16] = {};
    for (int i = 0; i < 16; i++) {
        result[i] = (i + o) % 16;
    }
    return array_to_tuple_16(result);
}

template<int... Is>
struct Shuffler {
    auto operator()(float_vec a, float_vec b) const {
        return __builtin_shufflevector(a, b, Is...);
    }
    auto operator()(floatx8_vec a, floatx8_vec b) const {
        return __builtin_shufflevector(a, b, Is...);
    }
};

#define SHUFFLER16(m) Shuffler<std::get<0>(m), std::get<1>(m), std::get<2>(m), std::get<3>(m),   \
                               std::get<4>(m), std::get<5>(m), std::get<6>(m), std::get<7>(m),   \
                               std::get<8>(m), std::get<9>(m), std::get<10>(m), std::get<11>(m), \
                               std::get<12>(m), std::get<13>(m), std::get<14>(m), std::get<15>(m)>()

#define SHUFFLER8(m) Shuffler<std::get<0>(m), std::get<1>(m), std::get<2>(m), std::get<3>(m), \
                              std::get<4>(m), std::get<5>(m), std::get<6>(m), std::get<7>(m)>()

template<int len>
constexpr auto extract_slice_mask(int offset) {
    int m[len] = {};
    for (int i = 0; i < len; i++) {
        m[i] = i + offset;
    }
    return array_to_tuple<len>(m);
}

inline float_vec extract_slice(float_vec a, float_vec b, int offset) {
#define CASE(x)                                       \
    if (offset == x) {                                \
        constexpr auto m = extract_slice_mask<16>(x); \
        return SHUFFLER16(m)(a, b);                   \
    }
    CASE(0);
    CASE(1);
    CASE(2);
    CASE(3);
    CASE(4);
    CASE(5);
    CASE(6);
    CASE(7);
    CASE(8);
    CASE(9);
    CASE(10);
    CASE(11);
    CASE(12);
    CASE(13);
    CASE(14);
    CASE(15);
    CASE(16);
    fprintf(stderr, "Bad offset in extract slice: %d\n", offset);
    assert(false);
    return float_vec{};
#undef CASE
}

inline floatx8_vec extract_slice(floatx8_vec a, floatx8_vec b, int offset) {
#define CASE(x)                                      \
    if (offset == x) {                               \
        constexpr auto m = extract_slice_mask<8>(x); \
        return SHUFFLER8(m)(a, b);                   \
    }
    CASE(0);
    CASE(1);
    CASE(2);
    CASE(3);
    CASE(4);
    CASE(5);
    CASE(6);
    CASE(7);
    CASE(8);
    assert(false);
    return floatx8_vec{};
#undef CASE
}

template<int len>
constexpr auto repeat_lanes_mask(int start, int length) {
    int m[len];
    for (int i = 0; i < len; i += length) {
        for (int j = 0; j < length; j++) {
            m[i + j] = start + j;
        }
    }
    return array_to_tuple<len>(m);
}

float_vec repeat_lanes(float_vec in, int start, int length) {
#define CASE(a, b)                                      \
    if (start == a && length == b) {                    \
        constexpr auto m = repeat_lanes_mask<16>(a, b); \
        return SHUFFLER16(m)(in, in);                   \
    }

    CASE(0, 2);
    CASE(2, 2);
    CASE(4, 2);
    CASE(6, 2);
    CASE(8, 2);
    CASE(10, 2);
    CASE(12, 2);
    CASE(14, 2);

    CASE(0, 4);
    CASE(4, 4);
    CASE(8, 4);
    CASE(12, 4);

    CASE(0, 8);
    CASE(8, 8);

    CASE(0, 16);

    fprintf(stderr, "Bad arguments to repeat_lanes: %d %d\n", start, length);
    assert(false);
    return float_vec{};
#undef CASE
}

float_vec repeat_lanes(floatx8_vec in, int start, int length) {
#define CASE(a, b)                                      \
    if (start == a && length == b) {                    \
        constexpr auto m = repeat_lanes_mask<16>(a, b); \
        return SHUFFLER16(m)(in, in);                   \
    }

    CASE(0, 2);
    CASE(2, 2);
    CASE(4, 2);
    CASE(6, 2);

    CASE(0, 4);
    CASE(4, 4);

    CASE(0, 8);

    assert(false);
    return float_vec{};
#undef CASE
}

template<int len>
constexpr auto splat_lanes_mask(int offset, int group_size) {
    int m[len];
    for (int i = 0; i < len; i += group_size) {
        for (int j = 0; j < group_size; j++) {
            m[i + j] = i + offset;
        }
    }
    return array_to_tuple<len>(m);
}

// In each contiguos group of 'group_size' lanes, take the offset-th lane within
// the group and splat it out to the full group.
float_vec splat_lanes(float_vec in, int offset, int group_size) {
    if (group_size == 1) {
        return in;
    }

#define CASE(o, g)                                     \
    if (offset == o && group_size == g) {              \
        constexpr auto m = splat_lanes_mask<16>(o, g); \
        return SHUFFLER16(m)(in, in);                  \
    }

    CASE(0, 16);
    CASE(1, 16);
    CASE(2, 16);
    CASE(3, 16);
    CASE(4, 16);
    CASE(5, 16);
    CASE(6, 16);
    CASE(7, 16);
    CASE(8, 16);
    CASE(9, 16);
    CASE(10, 16);
    CASE(11, 16);
    CASE(12, 16);
    CASE(13, 16);
    CASE(14, 16);
    CASE(15, 16);
    CASE(0, 8);
    CASE(1, 8);
    CASE(2, 8);
    CASE(3, 8);
    CASE(4, 8);
    CASE(5, 8);
    CASE(6, 8);
    CASE(7, 8);
    CASE(0, 4);
    CASE(1, 4);
    CASE(2, 4);
    CASE(3, 4);
    CASE(0, 2);
    CASE(1, 2);
    assert(false);
    return float_vec{};
}

/*
template<int len>
constexpr auto extract_lanes_mask(int start) {
    int m[len];
    for (int i = 0; i < len; i++) {
        m[i] = start + i;
    }
    return array_to_tuple<len>(m);
}

template<int lanes>
float_vec_t<lanes> extract_lanes(float_vec in, int start) {
#define CASE(a)                                          \
    if (start == a) {                                    \
        constexpr auto m = extract_lanes_mask<lanes>(a); \
        return SHUFFLER16(m)(in, in);                    \
    }

    CASE(0, 2);
    CASE(2, 2);
    CASE(4, 2);
    CASE(6, 2);
    CASE(8, 2);
    CASE(10, 2);
    CASE(12, 2);
    CASE(14, 2);

    CASE(0, 4);
    CASE(4, 4);
    CASE(8, 4);
    CASE(12, 4);

    CASE(0, 8);
    CASE(8, 8);

    assert(false);
    return float_vec{};
#undef CASE
}
*/

template<int len>
constexpr auto reverse_mask() {
    int m[len];
    for (int i = 0; i < len; i++) {
        m[i] = len - 1 - i;
    }
    return array_to_tuple<len>(m);
}

float_vec reverse(float_vec in) {
    constexpr auto mask = reverse_mask<16>();
    return SHUFFLER16(mask)(in, in);
}

floatx8_vec reverse(floatx8_vec in) {
    constexpr auto mask = reverse_mask<8>();
    return SHUFFLER8(mask)(in, in);
}

void deinterleave_blocks(const float_vec *in_a, const float_vec *in_b,
                         float_vec *out_a, float_vec *out_b, int block_size) {
    if (block_size == 1) {

        *out_a = __builtin_shufflevector(*in_a, *in_b, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
        *out_b = __builtin_shufflevector(*in_a, *in_b, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    } else if (block_size == 2) {
        *out_a = __builtin_shufflevector(*in_a, *in_b, 0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29);
        *out_b = __builtin_shufflevector(*in_a, *in_b, 2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31);
    } else if (block_size == 4) {
        *out_a = __builtin_shufflevector(*in_a, *in_b, 0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27);
        *out_b = __builtin_shufflevector(*in_a, *in_b, 4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31);
    } else if (block_size == 8) {
        *out_a = __builtin_shufflevector(*in_a, *in_b, 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23);
        *out_b = __builtin_shufflevector(*in_a, *in_b, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31);
    } else if ((block_size & (block_size - 1)) == 0) {
    } else {
        assert(false);
    }
}

// It's most natural to write this recursively, but then the compiler won't
// inline it because it doesn't know the recursion is bounded. So we make it
// template-recursive and dispatch in the helper below.
template<int num>
__attribute__((always_inline)) inline float_vec
horizontal_sum(const float_vec *in) {

    // The goal in here is to avoid using any shuffle instructions that require
    // a register to store the mask. We want registers to be useful for actual
    // data.

    if (num == 1) {
        return in[0];
    } else if (num == 2 || num == 4) {
        float_vec tmp[num];
        for (int i = 0; i < num; i++) {
            tmp[i] = in[i];
        }

        // There are several masks we could use to do a hierarchical horizontal
        // reduction. The ones below stay within 128-bit blocks, so shuffling by
        // these compiles to a shufps with immediate instruction, and requires
        // one final shuffle at the end (which is worth the cost of being able
        // to use the immediate).
        for (int step = 1; step < num; step *= 2) {
            for (int i = 0; i < num; i += 2 * step) {
                tmp[i] = __builtin_shufflevector(tmp[i], tmp[i + step], 0, 2, 16, 18, 4, 6, 20, 22, 8, 10, 24, 26, 12, 14, 28, 30) +
                         __builtin_shufflevector(tmp[i], tmp[i + step], 1, 3, 17, 19, 5, 7, 21, 23, 9, 11, 25, 27, 13, 15, 29, 31);
            }
        }

#if 0
        // Don't let LLVM move the shuffles around into a form where it uses a
        // register again, by telling it that a copy of tmp[0] is captured in
        // some mysterious way. This is slower in microbenchmarks. Need to
        // benchmark with more registers occupied.
        float_vec v = tmp[0];
        asm volatile(""
                     :
                     : "v"(v));
#endif

        if (num == 2) {
            return __builtin_shufflevector(tmp[0], tmp[0], 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
        } else {  // num == 4
            return __builtin_shufflevector(tmp[0], tmp[0], 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);
        }
    } else if ((num & (num - 1)) == 0) {
        // These shuffles move around entire 128-bit blocks, which can be
        // done cheaply too.
        float_vec tmp[num / 2];
        for (int i = 0; i < num / 2; i++) {
            if (num >= vec_lanes * 2) {
                tmp[i] = in[2 * i] + in[2 * i + 1];
            } else {
                tmp[i] =
                    __builtin_shufflevector(in[2 * i], in[2 * i + 1], 0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27) +
                    __builtin_shufflevector(in[2 * i], in[2 * i + 1], 4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31);
            }
        }
        return horizontal_sum<num / 2>(tmp);
    } else {
        assert(false);
    }
}

__attribute__((always_inline)) inline float_vec
horizontal_sum(const float_vec *in, int num) {
    if (num == 2) {
        return horizontal_sum<2>(in);
    } else if (num == 4) {
        return horizontal_sum<4>(in);
    } else if (num == 8) {
        return horizontal_sum<8>(in);
    } else if (num == 16) {
        return horizontal_sum<16>(in);
    } else {
        assert(false);
        return {};
    }
}

void interleave_blocks(const float_vec *in_a, const float_vec *in_b,
                       float_vec *out_a, float_vec *out_b, int block_size) {
    if (block_size == 1) {
        *out_a = __builtin_shufflevector(*in_a, *in_b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
        *out_b = __builtin_shufflevector(*in_a, *in_b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    } else if (block_size == 2) {
        *out_a = __builtin_shufflevector(*in_a, *in_b, 0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21, 6, 7, 22, 23);
        *out_b = __builtin_shufflevector(*in_a, *in_b, 8, 9, 24, 25, 10, 11, 26, 27, 12, 13, 28, 29, 14, 15, 30, 31);
    } else if (block_size == 4) {
        *out_a = __builtin_shufflevector(*in_a, *in_b, 0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23);
        *out_b = __builtin_shufflevector(*in_a, *in_b, 8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31);
    } else if (block_size == 8) {
        *out_a = __builtin_shufflevector(*in_a, *in_b, 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23);
        *out_b = __builtin_shufflevector(*in_a, *in_b, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31);
    } else if ((block_size & (block_size - 1)) == 0) {
    } else {
        assert(false);
    }
}

template<int num>
__attribute__((always_inline)) inline void interleave_vectors(const float_vec *in, float_vec *out) {
    if (num == 1) {
        *out = *in;
    } else if (num == 2) {
        interleave_blocks(in, in + 1, out, out + 1, 1);
    } else if ((num & (num - 1)) == 0) {
        float_vec tmp[num];
        constexpr int n = num / 2;
        for (int i = 0; i < num; i += n) {
            interleave_vectors<n>(in + i, tmp + i);
        }
        for (int i = 0; i < n; i++) {
            interleave_blocks(tmp + i, tmp + i + n, out + 2 * i, out + 2 * i + 1, n);
        }
    } else {
        assert(false);
    }
}

__attribute__((always_inline)) inline void interleave_vectors(const float_vec *in, float_vec *out, int num) {
    if (num == 1) {
        *out = *in;
    } else if (num == 2) {
        interleave_vectors<2>(in, out);
    } else if (num == 4) {
        interleave_vectors<4>(in, out);
    } else if (num == 8) {
        interleave_vectors<8>(in, out);
    } else if (num == 16) {
        interleave_vectors<16>(in, out);
    } else {
        assert(false);
    }
}

template<int num>
__attribute__((always_inline)) inline void deinterleave_vectors(const float_vec *in, float_vec *out) {
    if (num == 1) {
        *out = *in;
    } else if (num == 2) {
        deinterleave_blocks(in, in + 1, out, out + 1, 1);
    } else if ((num & (num - 1)) == 0) {
        float_vec tmp[num];
        constexpr int n = num / 2;
        for (int i = 0; i < num; i += n) {
            deinterleave_vectors<n>(in + i, tmp + i);
        }
        for (int i = 0; i < n; i++) {
            deinterleave_blocks(tmp + i, tmp + i + n, out + 2 * i, out + 2 * i + 1, n);
        }
    } else {
        assert(false);
    }
}

__attribute__((always_inline)) inline void deinterleave_vectors(const float_vec *in, float_vec *out, int num) {
    if (num == 1) {
        *out = *in;
    } else if (num == 2) {
        deinterleave_vectors<2>(in, out);
    } else if (num == 4) {
        deinterleave_vectors<4>(in, out);
    } else if (num == 8) {
        deinterleave_vectors<8>(in, out);
    } else if (num == 16) {
        deinterleave_vectors<16>(in, out);
    } else {
        assert(false);
    }
}

__attribute__((always_inline)) inline floatx8_vec first_half(float_vec vec) {
    return __builtin_shufflevector(vec, vec, 0, 1, 2, 3, 4, 5, 6, 7);
}

__attribute__((always_inline)) inline floatx8_vec second_half(float_vec vec) {
    return __builtin_shufflevector(vec, vec, 8, 9, 10, 11, 12, 13, 14, 15);
}

inline void print_vec(float_vec v) {
    for (int i = 0; i < vec_lanes; i++) {
        fprintf(stderr, "%1.2f ", v[i]);
    }
}

inline float load_coefficient(const float *x, int i) {
    // X86 can fuse a load and broadcast of a scalar into an fma, but it tends
    // to be a loop invariant, so LLVM wants to promote that broadcast scalar
    // out of the for loop and store it in a vector instead. This uses a hell of
    // a lot of registers, requires a mov before the fma (because fma is
    // destructive), and is generally a bad idea. To force LLVM to use the fused
    // form, we do a volatile load.
    //
    // This is currently only used in UpSample, because DownSample doesn't use a
    // scalar coefficient, and DenseFIR reuses each loaded tap multiple times.
    return *((volatile const float *)(x + i));
}

float horizontal_sum_reduce(floatx8_vec a) {
    auto a4 = (__builtin_shufflevector(a, a, 0, 1, 2, 3) +
               __builtin_shufflevector(a, a, 4, 5, 6, 7));
    auto a2 = (__builtin_shufflevector(a4, a4, 0, 1) +
               __builtin_shufflevector(a4, a4, 2, 3));
    return a2[0] + a2[1];
}

float horizontal_sum_reduce(float_vec a) {
    auto a8 = (__builtin_shufflevector(a, a, 0, 1, 2, 3, 4, 5, 6, 7) +
               __builtin_shufflevector(a, a, 8, 9, 10, 11, 12, 13, 14, 15));
    return horizontal_sum_reduce(a8);
}

template<int lanes>
float_vec_t<lanes> incomplete_horizontal_sum_reduce(float_vec a) {
    if constexpr (lanes == 16) {
        return a;
    }

    auto a8 = (__builtin_shufflevector(a, a, 0, 1, 2, 3, 4, 5, 6, 7) +
               __builtin_shufflevector(a, a, 8, 9, 10, 11, 12, 13, 14, 15));
    if constexpr (lanes == 8) {
        return a8;
    }

    auto a4 = (__builtin_shufflevector(a8, a8, 0, 1, 2, 3) +
               __builtin_shufflevector(a8, a8, 4, 5, 6, 7));
    if constexpr (lanes == 4) {
        return a4;
    }

    auto a2 = (__builtin_shufflevector(a4, a4, 0, 1) +
               __builtin_shufflevector(a4, a4, 2, 3));
    if constexpr (lanes == 2) {
        return a2;
    }

    return a2[0] + a2[1];
}

auto incomplete_horizontal_sum_reduce_2(float_vec a) {
    auto a8 = (__builtin_shufflevector(a, a, 0, 1, 2, 3, 4, 5, 6, 7) +
               __builtin_shufflevector(a, a, 8, 9, 10, 11, 12, 13, 14, 15));
    auto a4 = (__builtin_shufflevector(a8, a8, 0, 1, 2, 3) +
               __builtin_shufflevector(a8, a8, 4, 5, 6, 7));
    auto a2 = (__builtin_shufflevector(a4, a4, 0, 1) +
               __builtin_shufflevector(a4, a4, 2, 3));
    return a2;
}

auto incomplete_horizontal_sum_reduce_4(float_vec a) {
    auto a8 = (__builtin_shufflevector(a, a, 0, 1, 2, 3, 4, 5, 6, 7) +
               __builtin_shufflevector(a, a, 8, 9, 10, 11, 12, 13, 14, 15));
    auto a4 = (__builtin_shufflevector(a8, a8, 0, 1, 2, 3) +
               __builtin_shufflevector(a8, a8, 4, 5, 6, 7));
    return a4;
}

auto incomplete_horizontal_sum_reduce_8(float_vec a) {
    auto a8 = (__builtin_shufflevector(a, a, 0, 1, 2, 3, 4, 5, 6, 7) +
               __builtin_shufflevector(a, a, 8, 9, 10, 11, 12, 13, 14, 15));
    return a8;
}

ptrdiff_t clamp(ptrdiff_t x, ptrdiff_t min_x, ptrdiff_t max_x) {
    return std::max(min_x, std::min(max_x, x));
}

__attribute__((always_inline)) uint64_t generate_mask_64(int first, int last) {
    uint64_t m = (1ll << 63) >> ((last - first - 1) & 63);
    m >>= ((-last) & 63);
    m = (first >= last) ? 0 : m;
    return m;
}

#include <immintrin.h>

// TODO: Is there a generic LLVM IR way to express these?

template<typename V = float_vec>
__attribute__((always_inline)) V partial_load(const float *ptr, int num_lanes) {
    float_vec tmp = _mm512_maskz_loadu_ps((1 << num_lanes) - 1, ptr);
    V vec;
    static_assert(sizeof(vec) <= sizeof(tmp));
    std::memcpy(&vec, &tmp, sizeof(vec));
    return vec;
}

template<typename V = float_vec>
__attribute__((always_inline)) V partial_load(const float *ptr, int first, int last) {
    int mask = ((1 << last) - 1) & ~((1 << first) - 1);
    float_vec tmp = _mm512_maskz_loadu_ps(mask, ptr);
    V vec;
    static_assert(sizeof(vec) <= sizeof(tmp));
    std::memcpy(&vec, &tmp, sizeof(vec));
    return vec;
}

__attribute__((always_inline)) float_vec masked_load(const float *ptr, uint16_t mask) {
    return _mm512_maskz_loadu_ps(mask, ptr);
}

template<int N>
__attribute__((always_inline)) void partial_load_many(const float *ptr,
                                                      int first,
                                                      int last,
                                                      float_vec *dst) {
    static_assert(N == 1 || N == 2 || N == 4);
    if constexpr (N == 1) {
        // dst[0] = masked_load(ptr, ((1 << last) - 1) & ~((1 << first) - 1));
        dst[0] = partial_load(ptr, first, last);
    } else if constexpr (N == 2) {
        constexpr uint32_t one = 1;
        auto mask = _cvtu32_mask32(((one << last) - one) & ~((one << first) - one));
        dst[0] = _mm512_maskz_loadu_ps((__mmask16)mask, ptr);
        mask = _kshiftri_mask32(mask, 16);
        dst[1] = _mm512_maskz_loadu_ps((__mmask16)mask, ptr);
    } else if constexpr (N == 4) {
        auto mask = _cvtu64_mask64(generate_mask_64(first, last));

        dst[0] = _mm512_maskz_loadu_ps((__mmask16)mask, ptr);
        mask = _kshiftri_mask64(mask, vec_lanes);
        ptr += vec_lanes;
        dst[1] = _mm512_maskz_loadu_ps((__mmask16)mask, ptr);
        mask = _kshiftri_mask64(mask, vec_lanes);
        ptr += vec_lanes;
        dst[2] = _mm512_maskz_loadu_ps((__mmask16)mask, ptr);
        mask = _kshiftri_mask64(mask, vec_lanes);
        ptr += vec_lanes;
        dst[3] = _mm512_maskz_loadu_ps((__mmask16)mask, ptr);
    }
}

template<typename V = float_vec>
__attribute__((always_inline)) auto partial_store(float *ptr, V vec, int num_lanes) {
    float_vec tmp;
    static_assert(sizeof(vec) <= sizeof(tmp));
    std::memcpy(&tmp, &vec, sizeof(vec));
    return _mm512_mask_storeu_ps(ptr, (1 << num_lanes) - 1, tmp);
}

template<OutputMode mode, typename V = float_vec>
__attribute__((always_inline)) auto partial_store_or_accum(float *ptr, V vec, int num_lanes) {
    float_vec tmp;
    static_assert(sizeof(vec) <= sizeof(tmp));
    std::memcpy(&tmp, &vec, sizeof(vec));
    if (mode == Accum) {
        tmp += partial_load(ptr, num_lanes);
    }
    return _mm512_mask_storeu_ps(ptr, (1 << num_lanes) - 1, tmp);
}

template<OutputMode mode, typename V = float_vec>
__attribute__((always_inline)) auto partial_store_or_accum(float *ptr, V vec, int first, int last) {
    int mask = ((1 << last) - 1) & ~((1 << first) - 1);
    float_vec tmp;
    static_assert(sizeof(vec) <= sizeof(tmp));
    std::memcpy(&tmp, &vec, sizeof(vec));
    if (mode == Accum) {
        tmp += partial_load(ptr, first, last);
    }
    return _mm512_mask_storeu_ps(ptr, mask, tmp);
}

template<OutputMode mode, int N>
__attribute__((always_inline)) void partial_store_or_accum_many(float *ptr,
                                                                const float_vec *vecs,
                                                                int first,
                                                                int last) {

    float_vec tmp[N]{};
    if (mode == Accum) {
        partial_load_many<N>(ptr, first, last, tmp);
    }

    static_assert(N == 1 || N == 2 || N == 4);
    if constexpr (N == 1) {
        int mask = ((1 << last) - 1) & ~((1 << first) - 1);
        _mm512_mask_storeu_ps(ptr, mask, vecs[0] + tmp[0]);
    } else if constexpr (N == 2) {
        constexpr uint32_t one = 1;
        auto mask = _cvtu32_mask32(((one << last) - one) & ~((one << first) - one));
        _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[0] + tmp[0]);
        mask = _kshiftri_mask32(mask, 16);
        ptr += vec_lanes;
        _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[1] + tmp[1]);
    } else if constexpr (N == 4) {
        auto mask = _cvtu64_mask64(generate_mask_64(first, last));

        if (mode == Accum) {
            _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[0] + tmp[0]);
            mask = _kshiftri_mask64(mask, vec_lanes);
            ptr += vec_lanes;
            _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[1] + tmp[1]);
            mask = _kshiftri_mask64(mask, vec_lanes);
            ptr += vec_lanes;
            _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[2] + tmp[2]);
            mask = _kshiftri_mask64(mask, vec_lanes);
            ptr += vec_lanes;
            _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[3] + tmp[3]);
        } else {
            _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[0]);
            mask = _kshiftri_mask64(mask, vec_lanes);
            ptr += vec_lanes;
            _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[1]);
            mask = _kshiftri_mask64(mask, vec_lanes);
            ptr += vec_lanes;
            _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[2]);
            mask = _kshiftri_mask64(mask, vec_lanes);
            ptr += vec_lanes;
            _mm512_mask_storeu_ps(ptr, (__mmask16)mask, vecs[3]);
        }
    }
}

// This overload only exists if there's a run2d method that accepts lots of args
template<typename A,
         typename test = decltype(std::declval<A>().template run_2d<Assign>(nullptr, 0, 0, 0, nullptr, 0, 0, 0,
                                                                            0, 0, 0, 0))>
constexpr bool supports_partial_output(int) {
    return true;
}

// This overload always exists, but is unpreferred due to the argument needing a typecase
template<typename A>
constexpr bool supports_partial_output(double) {
    return false;
}

inline bool any_non_zero(float_vec v) {
    for (int i = 0; i < vec_lanes; i++) {
        if (v[i] != 0) return true;
    }
    return false;
}

#endif
