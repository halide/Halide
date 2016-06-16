#include "Halide.h"
#include <stdio.h>

using namespace Halide;

template<typename A>
const char *string_of_type();

#define DECL_SOT(name)                                          \
    template<>                                                  \
    const char *string_of_type<name>() {return #name;}

DECL_SOT(uint8_t);
DECL_SOT(int8_t);
DECL_SOT(uint16_t);
DECL_SOT(int16_t);
DECL_SOT(uint32_t);
DECL_SOT(int32_t);
DECL_SOT(float);
DECL_SOT(double);

template <typename T>
bool is_type_supported(int vec_width, const Target &target) {
    return target.supports_type(type_of<T>().with_lanes(vec_width));
}

template <>
bool is_type_supported<float>(int vec_width, const Target &target) {
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        return vec_width == 1;
    } else {
        return true;
    }
}

template <>
bool is_type_supported<double>(int vec_width, const Target &target) {
    if (target.has_feature(Target::OpenCL) &&
        !target.has_feature(Target::CLDoubles)) {
        return false;
    } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        return vec_width == 1;
    } else {
        return true;
    }
}

template<typename A, typename B>
bool test(int vec_width, const Target &target) {
    if (!is_type_supported<A>(vec_width, target) || !is_type_supported<B>(vec_width, target)) {
        // Type not supported, return pass.
        return true;
    }

    int W = 1024;
    int H = 1;

    Image<A> input(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = (A)((rand()&0xffff)*0.1);
        }
    }

    Var x, y;
    Func f;

    f(x, y) = cast<B>(input(x, y));

    if (target.has_gpu_feature()) {
        f.gpu_tile(x, 64);
    } else {
        if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            // TODO: Non-native vector widths hang the compiler here.
            //f.hexagon();
        }
        if (vec_width > 1) {
            f.vectorize(x, vec_width);
        }
    }

    Image<B> output(W, H);
    f.realize(output);

    /*
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
	    printf("%d %d -> %d %d\n", x, y, (int)(input(x, y)), (int)(output(x, y)));
        }
    }
    */

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {

            bool ok = ((B)(input(x, y)) == output(x, y));
            if (!ok) {
                printf("%s x %d -> %s x %d failed\n",
                       string_of_type<A>(), vec_width,
                       string_of_type<B>(), vec_width);
                printf("At %d %d, %f -> %f instead of %f\n",
                       x, y,
                       (double)(input(x, y)),
                       (double)(output(x, y)),
                       (double)((B)(input(x, y))));
                return false;
            }
        }
    }

    return true;
}

template<typename A>
bool test_all(int vec_width, const Target &target) {
    bool ok = true;
    ok = ok && test<A, float>(vec_width, target);
    ok = ok && test<A, double>(vec_width, target);
    ok = ok && test<A, uint8_t>(vec_width, target);
    ok = ok && test<A, int8_t>(vec_width, target);
    ok = ok && test<A, uint16_t>(vec_width, target);
    ok = ok && test<A, int16_t>(vec_width, target);
    ok = ok && test<A, uint32_t>(vec_width, target);
    ok = ok && test<A, int32_t>(vec_width, target);
    return ok;
}


int main(int argc, char **argv) {

    // We don't test this on windows, because float-to-int conversions
    // on windows use _ftol2, which has its own unique calling
    // convention, and older LLVMs (e.g. pnacl) don't do it right so
    // you get clobbered registers.
    #ifdef WIN32
    printf("Not testing on windows\n");
    return 0;
    #endif

    Target target = get_jit_target_from_environment();

    bool ok = true;

    // We only test power-of-two vector widths for now
    for (int vec_width = 1; vec_width <= 64; vec_width*=2) {
        printf("Testing vector width %d\n", vec_width);
        ok = ok && test_all<float>(vec_width, target);
        ok = ok && test_all<double>(vec_width, target);
        ok = ok && test_all<uint8_t>(vec_width, target);
        ok = ok && test_all<int8_t>(vec_width, target);
        ok = ok && test_all<uint16_t>(vec_width, target);
        ok = ok && test_all<int16_t>(vec_width, target);
        ok = ok && test_all<uint32_t>(vec_width, target);
        ok = ok && test_all<int32_t>(vec_width, target);
    }

    if (!ok) return -1;
    printf("Success!\n");
    return 0;
}
