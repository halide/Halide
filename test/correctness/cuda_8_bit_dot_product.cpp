#include "Halide.h"

#include <regex>

using namespace Halide;

template<typename Out, typename A, typename B>
void test(Target t) {
    for (int factor : {4, 16}) {
        for (int vec : {1, 4}) {
            std::cout
                << "Testing dot product of "
                << type_of<A>() << " * " << type_of<B>() << " -> " << type_of<Out>()
                << " with vector width " << vec
                << " and reduction factor " << factor << "\n";
            Func in_a, in_b;
            Var x, y;

            in_a(x, y) = cast<A>(x - y * 17);
            in_a.compute_root();

            in_b(x, y) = cast<B>(x * 3 + y * 7);
            in_b.compute_root();

            Func g;
            RDom r(0, factor * 4);
            g(x, y) += cast<Out>(in_a(r, x)) * in_b(r, y);

            Func h;
            h(x, y) = g(x, y);

            Var xi, yi;
            g.update().atomic().vectorize(r, factor).unroll(r);
            h.gpu_tile(x, y, xi, yi, 32, 8, TailStrategy::RoundUp);

            Buffer<Out> out(128, 128);
            h.realize(out);
            out.copy_to_host();

            for (int y = 0; y < out.height(); y++) {
                for (int x = 0; x < out.width(); x++) {
                    Out correct = 0;
                    for (int r = 0; r < factor * 4; r++) {
                        A in_a_r_x = (A)(r - x * 17);
                        B in_b_r_y = (B)(r * 3 + y * 7);
                        correct += ((Out)(in_a_r_x)) * in_b_r_y;
                    }
                    if (out(x, y) != correct) {
                        printf("out(%d, %d) = %d instead of %d\n", x, y, (int)(out(x, y)), (int)(correct));
                        exit(1);
                    }
                }
            }

            // Check the instruction was emitted intended by just grepping the
            // compiled code (the PTX source is an embedded string).
            Buffer<uint8_t> buf = h.compile_to_module(std::vector<Argument>(), "h", t).compile_to_buffer();
            std::basic_regex<char> regex("dp[24]a[.lo]*[us]32[.][us]32");
            if (!std::regex_search((const char *)buf.begin(), (const char *)buf.end(), regex)) {
                printf("Did not find use of dp2a or dp4a in compiled code. Rerun test with HL_DEBUG_CODEGEN=1 to debug\n");
                exit(1);
            }
        }
    }
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDACapability61)) {
        printf("[SKIP] Cuda (with compute capability 6.1) is not enabled in target: %s\n",
               t.to_string().c_str());
        return 0;
    }

    test<int32_t, int8_t, int8_t>(t);
    test<int32_t, int8_t, uint8_t>(t);
    test<int32_t, uint8_t, int8_t>(t);
    test<uint32_t, uint8_t, uint8_t>(t);
    test<int32_t, int16_t, int8_t>(t);
    test<int32_t, int16_t, uint8_t>(t);
    test<int32_t, uint16_t, int8_t>(t);
    test<uint32_t, uint16_t, uint8_t>(t);
    test<int32_t, int8_t, int16_t>(t);
    test<int32_t, int8_t, uint16_t>(t);
    test<int32_t, uint8_t, int16_t>(t);
    test<uint32_t, uint16_t, uint8_t>(t);

    printf("Success!\n");
    return 0;
}
