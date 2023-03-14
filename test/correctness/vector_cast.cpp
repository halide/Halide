#include "Halide.h"
#include "test_sharding.h"

#include <stdio.h>

using namespace Halide;

template<typename A>
const char *string_of_type();

#define DECL_SOT(name)                   \
    template<>                           \
    const char *string_of_type<name>() { \
        return #name;                    \
    }

DECL_SOT(uint8_t);
DECL_SOT(int8_t);
DECL_SOT(uint16_t);
DECL_SOT(int16_t);
DECL_SOT(uint32_t);
DECL_SOT(int32_t);
DECL_SOT(float);
DECL_SOT(double);

template<typename T>
bool is_type_supported(int vec_width, const Target &target) {
    DeviceAPI device = DeviceAPI::Default_GPU;

    if (target.has_feature(Target::HVX)) {
        device = DeviceAPI::Hexagon;
    }
    return target.supports_type(type_of<T>().with_lanes(vec_width), device);
}

template<typename A, typename B>
bool test(int vec_width, const Target &target) {
    // Useful for debugging; leave in (commented out)
    // printf("Test %s x %d -> %s x %d\n",
    //         string_of_type<A>(), vec_width,
    //         string_of_type<B>(), vec_width);

    if (!is_type_supported<A>(vec_width, target) || !is_type_supported<B>(vec_width, target)) {
        // Type not supported, return pass.
        return true;
    }

    int W = 1024;
    int H = 1;

    Buffer<A> input(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // Casting from an out-of-range float to an int is UB, so
            // we have to pick our values a little carefully.
            input(x, y) = (A)((rand() & 0xffff) / 512.0);
        }
    }

    Var x, y;
    Func f;

    f(x, y) = cast<B>(input(x, y));

    if (target.has_gpu_feature()) {
        Var xo, xi;
        f.gpu_tile(x, xo, xi, 64);
    } else {
        if (target.has_feature(Target::HVX)) {
            // TODO: Non-native vector widths hang the compiler here.
            // f.hexagon();
        }
        if (vec_width > 1) {
            f.vectorize(x, vec_width);
        }
    }

    Buffer<B> output = f.realize({W, H});

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
                fprintf(stderr, "%s x %d -> %s x %d failed\n",
                        string_of_type<A>(), vec_width,
                        string_of_type<B>(), vec_width);
                fprintf(stderr, "At %d %d, %f -> %f instead of %f\n",
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

struct Task {
    std::function<bool()> fn;
};

template<typename A>
void add_all(int vec_width, const Target &target, std::vector<Task> &tasks) {
    tasks.push_back({[=]() { return test<A, float>(vec_width, target); }});
    tasks.push_back({[=]() { return test<A, float>(vec_width, target); }});
    tasks.push_back({[=]() { return test<A, double>(vec_width, target); }});
    tasks.push_back({[=]() { return test<A, uint8_t>(vec_width, target); }});
    tasks.push_back({[=]() { return test<A, uint16_t>(vec_width, target); }});
    tasks.push_back({[=]() { return test<A, uint32_t>(vec_width, target); }});
    tasks.push_back({[=]() { return test<A, int8_t>(vec_width, target); }});
    tasks.push_back({[=]() { return test<A, int16_t>(vec_width, target); }});
    tasks.push_back({[=]() { return test<A, int32_t>(vec_width, target); }});
}

int main(int argc, char **argv) {
// TODO: is this still relevant?
// We don't test this on windows, because float-to-int conversions
// on windows use _ftol2, which has its own unique calling
// convention, and older LLVMs (e.g. pnacl) don't do it right so
// you get clobbered registers.
#ifdef WIN32
    printf("[SKIP] float-to-int conversions don't work with older LLVMs on Windows\n");
    return 0;
#endif

    Target target = get_jit_target_from_environment();

    // We only test power-of-two vector widths for now
    int vec_width_max = 64;
    if (target.arch == Target::WebAssembly) {
        // The wasm jit is very slow, so shorten this test here.
        vec_width_max = 16;
    }
    std::vector<Task> tasks;
    for (int vec_width = 1; vec_width <= vec_width_max; vec_width *= 2) {
        add_all<float>(vec_width, target, tasks);
        add_all<double>(vec_width, target, tasks);
        add_all<uint8_t>(vec_width, target, tasks);
        add_all<uint16_t>(vec_width, target, tasks);
        add_all<uint32_t>(vec_width, target, tasks);
        add_all<int8_t>(vec_width, target, tasks);
        add_all<int16_t>(vec_width, target, tasks);
        add_all<int32_t>(vec_width, target, tasks);
    }

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        if (!task.fn()) {
            exit(1);
        }
    }

    printf("Success!\n");
    return 0;
}
