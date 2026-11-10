#include "Halide.h"
#include <cstdio>
#include <regex>

using namespace Halide;

// A vectorized sum of 8-bit values widened to 32 bits is their dot product
// with ones: dp4a.
template<typename Out, typename A>
int test(const Target &t) {
    for (int vec : {4, 8, 16}) {
        Func in, g, h;
        Var x, y, xi, yi;
        in(x, y) = cast<A>(x * 37 - y * 11);
        in.compute_root();
        RDom r(0, 64);
        g(x, y) += cast<Out>(in(r, x + y));
        h(x, y) = g(x, y);
        g.update().atomic().vectorize(r, vec).unroll(r);
        h.gpu_tile(x, y, xi, yi, 32, 8, TailStrategy::RoundUp);
        Buffer<Out> out = h.realize({64, 64}, t);
        out.copy_to_host();
        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                Out correct = 0;
                for (int k = 0; k < 64; k++) {
                    correct += (Out)(A)(k * 37 - (x + y) * 11);
                }
                if (out(x, y) != correct) {
                    printf("vec = %d: out(%d, %d) = %d instead of %d\n", vec, x, y, (int)out(x, y), (int)correct);
                    return 1;
                }
            }
        }
        Buffer<uint8_t> buf = h.compile_to_module(std::vector<Argument>(), "h", t).compile_to_buffer();
        std::regex re("dp4a[.][us]32[.][us]32");
        if (!std::regex_search((const char *)buf.begin(), (const char *)buf.end(), re)) {
            printf("vec = %d: no dp4a in the PTX. Rerun with HL_DEBUG_CODEGEN=1 to debug\n", vec);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA) || t.get_cuda_capability_lower_bound() < 61) {
        printf("[SKIP] Cuda (with compute capability 6.1) is not enabled in target: %s\n",
               t.to_string().c_str());
        return 0;
    }
    if (test<int32_t, int8_t>(t) || test<int32_t, uint8_t>(t) || test<uint32_t, uint8_t>(t)) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
