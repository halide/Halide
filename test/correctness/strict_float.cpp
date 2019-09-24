#include "Halide.h"

#include <algorithm>
#include <ios>
#include <iostream>
#include <iomanip>
#include "HalideBuffer.h"

using namespace Halide;

#if defined(__SSE2__) || defined(__AVX__)
#include <immintrin.h>
#endif

#ifdef __SSE2__
float no_fma_dot_prod_sse(const float *in, int count) {
    __m128 sum = _mm_set1_ps(0.0f);
    const __m128 *in_v = (const __m128 *)in;
    for (int i = 0; i < count / 4; i++) {
      __m128 prod = _mm_mul_ps(in_v[i], in_v[i]);
        sum = _mm_add_ps(prod, sum);
    }
    float *f = (float *)&sum;
    float result = 0.0f;
    for (int i = 0; i < 4; i++) {
        result += f[i];
    }
    return result;
}
#endif

#if defined(__SSE2__) && defined(__FMA__)
float fma_dot_prod_sse(const float *in, int count) {
    __m128 sum = _mm_set1_ps(0.0f);
    const __m128 *in_v = (const __m128 *)in;
    for (int i = 0; i < count / 4; i++) {
        sum = _mm_fmadd_ps(in_v[i], in_v[i], sum);
    }
    float *f = (float *)&sum;
    float result = 0.0f;
    for (int i = 0; i < 4; i++) {
        result += f[i];
    }
    return result;
}
#endif

#if defined(__AVX__)
float no_fma_dot_prod_avx(const float *in, int count) {
    __m256 sum = _mm256_set1_ps(0.0f);
    const __m256 *in_v = (const __m256 *)in;
    for (int i = 0; i < count / 8; i++) {
        __m256 prod = _mm256_mul_ps(in_v[i], in_v[i]);
        sum = _mm256_add_ps(prod, sum);
    }
    float *f = (float *)&sum;
    float result = 0.0f;
    for (int i = 0; i < 8; i++) {
        result += f[i];
    }
    return result;
}
#endif

#if defined(__AVX__) && defined(__FMA__)
float fma_dot_prod_avx(const float *in, int count) {
    __m256 sum = _mm256_set1_ps(0.0f);
    const __m256 *in_v = (const __m256 *)in;
    for (int i = 0; i < count / 8; i++) {
        sum = _mm256_fmadd_ps(in_v[i], in_v[i], sum);
    }
    float *f = (float *)&sum;
    float result = 0.0f;
    for (int i = 0; i < 8; i++) {
        result += f[i];
    }
    return result;
}
#endif

Buffer<float> one_million_rando_floats() {
    Var x("x");
    Func randos;
    randos(x) = random_float();
    return randos.realize(1e6);
}

ImageParam in(Float(32), 1);

Expr term(Expr index) {
    return in(index) * in(index);
}

enum class FloatStrictness {
  Default,
  Strict
} global_strictness = FloatStrictness::Default;

std::string strictness_to_string(FloatStrictness strictness) {
    if (strictness == FloatStrictness::Strict) {
        return "strict_float";
    }
    return "default";
}

Expr apply_strictness(Expr x) {
    if (global_strictness == FloatStrictness::Strict) {
        return strict_float(x);
    }
    return x;
}

template <typename Accum>
Func simple_sum(int vectorize) {
    Func total("total");
    // Can't use rfactor because strict_float is not associative.
    if (vectorize != 0) {
        Func total_inner("total_inner");
        RDom r_outer(0, in.width() / vectorize);
        RDom r_lanes(0, vectorize);
        Var i("i");
        total_inner(i) = cast<Accum>(0);
        total_inner(i) = apply_strictness(total_inner(i) + cast<Accum>(term(r_outer * vectorize + i)));
        total() = cast<Accum>(0);
        total() = apply_strictness(total() + total_inner(r_lanes));
        total_inner.compute_at(total, Var::outermost());
        total_inner.vectorize(i);
        total_inner.update(0).vectorize(i);
    } else {
        RDom r(0, in.width(), "r");

        total() = apply_strictness(cast<Accum>(0));
        total() = apply_strictness(total() + cast<Accum>(term(r)));
    }
#if 0
    if (vectorize != 0) {
        RVar rxo("rxo"), rxi("rxi");
        Var u("u");
        Func intm = total.update(0).split(r, rxo, rxi, vectorize).rfactor({{rxi, u}});
        intm.compute_at(total, Var::outermost());
        intm.vectorize(u, vectorize);
        intm.update(0).vectorize(u, vectorize);
    }
#endif
    return lambda(apply_strictness(cast<float>(total())));
}

Func kahan_sum(int vectorize) {
    // Item 0 of the tuple valued k_sum is the sum and item 1 is an error compensation term.
    // See: https://en.wikipedia.org/wiki/Kahan_summation_algorithm
    Func k_sum("k_sum");

    // rfactor cannot prove associativity for the non-strict formulation and strict_float is not associative.
    if (vectorize != 0) {
        Func k_sum_inner("k_sum_inner");
        RDom r_outer(0, in.width() / vectorize);
        RDom r_lanes(0, vectorize);
        Var i("i");
        k_sum_inner(i) = Tuple(0.0f, 0.0f);
        k_sum_inner(i) = Tuple(apply_strictness(k_sum_inner(i)[0] + (term(r_outer * vectorize + i) - k_sum_inner(i)[1])),
                               apply_strictness((k_sum_inner(i)[0] + (term(r_outer * vectorize + i)- k_sum_inner(i)[1])) - k_sum_inner(i)[0]) - (term(r_outer * vectorize + i) - k_sum_inner(i)[1]));
        k_sum() = Tuple(0.0f, 0.0f);
        k_sum() = Tuple(apply_strictness(k_sum()[0] + (k_sum_inner(r_lanes)[0] - k_sum()[1])),
                        apply_strictness((k_sum()[0] + (k_sum_inner(r_lanes)[0] - k_sum()[1])) - k_sum()[0]) - (k_sum_inner(r_lanes)[0] - k_sum()[1]));
        k_sum_inner.compute_at(k_sum, Var::outermost());
        k_sum_inner.vectorize(i);
        k_sum_inner.update(0).vectorize(i);
    } else {
        RDom r(0, in.width(), "r");

        k_sum() = Tuple(0.0f, 0.0f);
        k_sum() = Tuple(apply_strictness(k_sum()[0] + (term(r) - k_sum()[1])),
                        apply_strictness((k_sum()[0] + (term(r) - k_sum()[1])) - k_sum()[0]) - (term(r) - k_sum()[1]));
    }

    return lambda(k_sum()[0]);
}

float eval(Func f, const Target &t, const std::string &name, const std::string &suffix, float expected) {
    float val = ((Buffer<float>)f.realize(t))();
    std::cout << "        " << name << ": " << val;
    if (expected != 0.0f) {
        std::cout << " residual: " << val - expected;
    }
    std::cout << "\n";
    return val;
}

void run_one_condition(const Target &t, FloatStrictness strictness, Buffer<float> vals) {
    global_strictness = strictness;
    std::string suffix = "_" + t.to_string() + "_" + strictness_to_string(strictness);

    std::cout << "    Target: " << t.to_string() << " Strictness: " << strictness_to_string(strictness) << "\n";

    float simple_double = eval(simple_sum<double>(0), t, "simple_double", suffix, 0.0f);
    float simple_double_vec_4 = eval(simple_sum<double>(4), t, "simple_double_vec_4", suffix, simple_double);
    float simple_double_vec_8 = eval(simple_sum<double>(8), t, "simple_double_vec_8", suffix, simple_double);
    float simple_float = eval(simple_sum<float>(0), t, "simple_float", suffix, simple_double);
    float simple_float_vec_4 = eval(simple_sum<float>(4), t, "simple_float_vec_4", suffix, simple_double);
    float simple_float_vec_8 = eval(simple_sum<float>(8), t, "simple_float_vec_8", suffix, simple_double);
    float kahan = eval(kahan_sum(0), t, "kahan", suffix, simple_double);
    float kahan_vec_4 = eval(kahan_sum(4), t, "kahan_vec_4", suffix, simple_double);
    float kahan_vec_8 = eval(kahan_sum(8), t, "kahan_vec_8", suffix, simple_double);

#ifdef __SSE2__
    float vec_dot_prod_4 = no_fma_dot_prod_sse(&vals(0), vals.width());
    std::cout << "        four wide no fma: " << vec_dot_prod_4 << " residual: " << vec_dot_prod_4 - simple_double << "\n";
#endif

#if defined(__SSE2__) && defined(__FMA__)
    float fma_dot_prod_4 = fma_dot_prod_sse(&vals(0), vals.width());
    std::cout << "        four wide fma: " << fma_dot_prod_4 << " residual: " << fma_dot_prod_4 - simple_double << "\n";
#endif

#if defined(__AVX__)
    float vec_dot_prod_8 = no_fma_dot_prod_avx(&vals(0), vals.width());
    std::cout << "        eight wide no fma: " << vec_dot_prod_8 << " residual: " << vec_dot_prod_8 - simple_double << "\n";
#endif

#if defined(__AVX__) && defined(__FMA__)
    float fma_dot_prod_8 = fma_dot_prod_avx(&vals(0), vals.width());
    std::cout << "        eight wide fma: " << fma_dot_prod_8 << " residual: " << fma_dot_prod_8 - simple_double << "\n";
#endif

    if (strictness == FloatStrictness::Strict) {
        // assert kahan is more accurate than simple method
        assert((fabs(simple_double - kahan) <= fabs(simple_double - simple_float)));
        // assert vecotorized kahan is more accurate than simple method
        assert((fabs(simple_double - kahan_vec_4) <= fabs(simple_double - simple_float)));
        assert((fabs(simple_double - kahan_vec_8) <= fabs(simple_double - simple_float)));
        // Just use some vars for now.
        assert(simple_double_vec_4 != 0 &&  simple_double_vec_8 != 0 && simple_float_vec_4 != 0 && simple_float_vec_8 != 0);
    }
}

void run_all_conditions(const char *name, Buffer<float> &vals) {
    std::cout << "Running on " << name << " data:\n";

    Target loose{get_jit_target_from_environment().without_feature(Target::StrictFloat)};
    Target strict{loose.with_feature(Target::StrictFloat)};
  
    run_one_condition(loose, FloatStrictness::Default, vals);
    run_one_condition(strict, FloatStrictness::Default, vals);
    run_one_condition(loose, FloatStrictness::Strict, vals);
    run_one_condition(strict, FloatStrictness::Strict, vals);
}

Buffer<float> block_transposed_by_n(Buffer<float> &buf, int vectorize) {
    Buffer<float> result(buf.width());

    int block_size = buf.width() / vectorize;
    for (int32_t i = 0; i < block_size; i++) {
        for (int32_t j = 0; j < vectorize; j++) {
            result(i * vectorize + j) = buf(j * block_size + i);
        }
    }
    return result;
}

int main(int argc, char **argv) {
    std::cout << std::setprecision(10);
    Buffer<float> vals = one_million_rando_floats();
    Buffer<float> transposed;
    in.set(vals);
    // Clean up stmt file by asserting clean division. Also eliminates needing boundary conditions.
    in.dim(0).set_bounds(0, 1000000);

    // Random data, average case for error.
    run_all_conditions("random", vals);
    transposed = block_transposed_by_n(vals, 4);
    in.set(transposed);
    run_all_conditions("random transposed", transposed);
    
    // Ascending, best case for error.
    std::sort(vals.begin(), vals.end());
    in.set(vals);
    run_all_conditions("sorted ascending", vals);
    transposed = block_transposed_by_n(vals, 4);
    in.set(transposed);
    run_all_conditions("sorted ascending transposed", transposed);

    // Descending, worst case for error.
    std::sort(vals.begin(), vals.end(), std::greater<float>());
    in.set(vals);
    run_all_conditions("sorted descending", vals);
    transposed = block_transposed_by_n(vals, 4);
    in.set(transposed);
    run_all_conditions("sorted descending transposed", transposed);

    // TODO: This needs to be made in to a test. Currently it is not
    // reproducing the reported failure however because both max_diff_offset and
    // max_diff_strict_offset are zero.
    //
    // Check case from reported bug where CSE in frontend was breaking strict_float.
    // (See: https://github.com/halide/Halide/issues/3813)
    Var x, y;
    Func f, f_offset, f_strict, f_strict_offset;
    Expr sval_mul = 43758.5453123f * sin(y * 78.233f + x * 12.9898f);
    Expr sval_mul_offset = 43758.5453123f * sin(y * 78.233f + (x + 1) * 12.9898f);
    Expr rand_val = sval_mul - floor(sval_mul);
    Expr rand_val_offset = sval_mul_offset - floor(sval_mul_offset);
    f(x, y) = rand_val;
    f_offset(x, y) = rand_val_offset;
    f_strict(x, y) = strict_float(rand_val);
    f_strict_offset(x, y) = strict_float(rand_val_offset);

    Buffer<float> result = f.realize(513, 512);
    Buffer<float> result_offset = f_offset.realize(512, 512);
    Buffer<float> result_strict = f_strict.realize(513, 512);
    Buffer<float> result_strict_offset = f_strict_offset.realize(512, 512);

    float max_diff = 0.0f;
    float max_diff_offset = 0.0f;
    float max_diff_strict_offset = 0.0f;
    for (int32_t y = 0; y < 512; y++) {
      for (int32_t x = 0; x < 512; x++) {
        float diff = fabs(result(x, y) - result_strict(x, y));
        float diff_offset = fabs(result(x + 1, y) - result_offset(x, y));
        float diff_strict_offset = fabs(result_strict(x + 1, y) - result_strict_offset(x, y));

        if (diff > max_diff) {
          max_diff = diff;
        }
        if (diff_offset > max_diff_offset) {
          max_diff_offset = diff_offset;
        }
        if (diff_strict_offset > max_diff_strict_offset) {
          max_diff_strict_offset = diff_strict_offset;
        }
      }
    }
    printf("Max diff %f max diff offset %f max diff strict offset %f.\n", max_diff, max_diff_offset, max_diff_strict_offset);

    // TODO: Get first one to fail to demonstrate bug. Only second one
    // should persist and it should get a tolerance.
    assert(max_diff_offset == 0.0f);
    assert(max_diff_strict_offset == 0.0f);

    printf("Success!\n");
    
    return 0;
}
