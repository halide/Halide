#include "Halide.h"
#include "halide_benchmark.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

void check(int r) {
    assert(r == 0);
}

void run_test(Target t) {
    // Must call this so that the changes in cached runtime are noticed
    Halide::Internal::JITSharedRuntime::release_all();

    class TestGen1 : public Generator<TestGen1> {
    public:
        Input<Buffer<uint32_t, 2>> img_{"img"};
        Input<uint32_t> offset_{"offset"};
        Output<Buffer<uint32_t, 2>> out_{"out"};

        void generate() {
            Var x("x"), y("y");

            // Make a copy so that halide_malloc() is called by the generated
            // code (since Halide::Runtime::Buffer doesn't use halid_malloc(),
            // see https://github.com/halide/Halide/issues/7188).
            Func copy("copy");
            copy(x, y) = img_(x, y);

            out_(x, y) = u32_sat(copy(x, y) + offset_);

            copy.compute_root().store_in(MemoryType::Heap).vectorize(x, natural_vector_size<uint32_t>());
            out_.vectorize(x, natural_vector_size<uint32_t>());
        }
    };

    // Make it large enough so that we won't attempt to use stack instead of heap
    const int W = t.natural_vector_size<uint32_t>() * 256;
    const int H = 2048;

    Buffer<uint32_t> in1(W, H);
    Buffer<uint32_t> in2(W, H);

    for (int i = 0; i < W; i++) {
        for (int j = 0; j < H; j++) {
            in1(i, j) = i + j * 10;
            in2(i, j) = i * 10 + j;
        }
    }

    const GeneratorContext context(t);

    auto gen = TestGen1::create(context);
    Callable c = gen->compile_to_callable();

    const uint32_t offset1 = 42;
    Buffer<uint32_t> out1(W, H);
    check(c(in1, offset1, out1));

    const uint32_t offset2 = 22;
    Buffer<uint32_t> out2(W, H);
    check(c(in2, offset2, out2));

    const uint32_t offset3 = 12;
    Buffer<uint32_t> out3(W, H);
    check(c(in1, offset3, out3));

    const uint32_t offset4 = 16;
    Buffer<uint32_t> out4(W, H);
    check(c(in2, offset4, out4));

    for (int i = 0; i < W; i++) {
        for (int j = 0; j < H; j++) {
            assert(out1(i, j) == i + j * 10 + offset1);
            assert(out2(i, j) == i * 10 + j + offset2);
            assert(out3(i, j) == i + j * 10 + offset3);
            assert(out4(i, j) == i * 10 + j + offset4);
        }
    }

    // Now run a benchmark, but don't check it
    double time = Halide::Tools::benchmark(10, 100, [&]() {
        check(c(in2, offset4, out4));
    });
    const float megapixels = (W * H) / (1024.f * 1024.f);
    printf("Benchmark: %dx%d -> %f mpix/s for %s\n", W, H, megapixels / time, t.to_string().c_str());
}

}  // namespace

int main(int argc, char **argv) {
    const Target t = get_jit_target_from_environment();
    if (t.arch == Target::WebAssembly) {
        printf("[SKIP] This test is too slow for Wasm.\n");
        return 0;
    }

    printf("Testing with malloc()... ");
    run_test(t.with_feature(Target::NoAlignedAlloc));

    printf("Testing with aligned_alloc()... ");
    run_test(t.without_feature(Target::NoAlignedAlloc));

    printf("Success!\n");
}
