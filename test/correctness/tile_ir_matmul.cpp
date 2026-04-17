#include "Halide.h"
#include <random>

using namespace Halide;

int main(int argc, char **argv) {

    std::mt19937 rng;

    const int N = 256;

    Buffer<float16_t> A(N, N), B(N, N);
    Buffer<float> correct(N, N), C(N, N);
    Var i, j;
    RDom k(0, N);

    A.for_each_value([&](float16_t &a) {
        a = float16_t(rng() & 1);
    });
    B.for_each_value([&](float16_t &b) {
        b = float16_t(rng() & 1);
    });

    auto &A_u16 = *reinterpret_cast<Buffer<uint16_t> *>(&A);
    auto &B_u16 = *reinterpret_cast<Buffer<uint16_t> *>(&B);
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            float c = 0;
            for (int k = 0; k < N; k++) {
                uint16_t a_u16 = A_u16(k, i);
                uint16_t b_u16 = B_u16(j, k);
                c += (float)(float16_t::make_from_bits(a_u16)) *
                     (float)(float16_t::make_from_bits(b_u16));
            }
            correct(j, i) = c;
        }
    }

    Func mm{"mm"}, wrap{"wrap"};
    mm(j, i) += cast<float>(A(k, i)) * B(j, k);

    wrap(j, i) = mm(j, i);

    wrap.bound(i, 0, N).bound(j, 0, N);

    Target t = get_jit_target_from_environment();
    if (t.has_gpu_feature()) {
        Var ji{"ji"}, ii{"ii"};
        RVar ki{"ki"}, ko{"ko"};
        wrap.gpu_tile(j, i, ji, ii, 16, 16);
        mm.compute_at(wrap, j)
            .gpu_threads(i, j)
            .update()
            .atomic()
            .split(k, ko, ki, 16)
            .reorder(ki, j, i, ko)
            .gpu_threads(i, j)
            .vectorize(ki);
    } else {
        Var ji{"ji"}, ii{"ii"};
        RVar ki{"ki"}, ko{"ko"};
        wrap.tile(j, i, ji, ii, 16, 16);
        wrap.vectorize(ji).vectorize(ii);
        mm.compute_at(wrap, j)
            .vectorize(i)
            .vectorize(j)
            .update()
            .atomic()
            .split(k, ko, ki, 16)
            .reorder(ki, j, i, ko)
            .vectorize(ki)
            .vectorize(i)
            .vectorize(j);
    }

    wrap.realize(C);

    C.copy_to_host();

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            // Should be exact, because the matrix entries were 0/1
            if (C(j, i) != correct(j, i)) {
                printf("C(%d, %d) = %f instead of %f\n",
                       j, i, C(j, i), correct(j, i));
                return 1;
            }
        }
    }

    printf("Success!\n");

    return 0;
}
