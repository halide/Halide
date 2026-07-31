#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <regex>

using namespace Halide;

// A barrier between two pieces of a kernel is only needed if the second one
// reads what the first one wrote. When it doesn't, and the second one has a
// barrier of its own further on, that later barrier can do the job of both,
// and the earlier one is elided with its fence types folded into the later.
//
// Getting this wrong in the eliding direction is a race rather than a wrong
// answer, so these check the emitted barriers rather than just the results:
// each scenario says how many barriers a correct compiler must emit, and the
// results are checked too where a device is available.

namespace {

int failures = 0;

// The number of barriers in the kernels of a pipeline. Counted from the
// conceptual stmt, which still has the barrier intrinsics in it and is not
// duplicated the way the debug-level PTX dump is.
int count_barriers(Func out, const std::vector<Argument> &args, const Target &t) {
    std::string stmt_file = Internal::get_test_tmp_dir() + "gpu_barrier_sinking.stmt";
    out.compile_to_conceptual_stmt(stmt_file, args, Halide::Text, t);

    std::ifstream f(stmt_file);
    std::string body((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    std::regex barrier("gpu_thread_barrier");
    return (int)std::distance(
        std::sregex_iterator(body.begin(), body.end(), barrier),
        std::sregex_iterator());
}

void check_barriers(const char *name, int got, int expected) {
    if (got != expected) {
        printf("[%s] emitted %d barriers, expected %d\n", name, got, expected);
        failures++;
        return;
    }
    printf("[%s] OK (%d barriers)\n", name, got);
}

Target gpu_target() {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        t = t.with_feature(Target::CUDA);
    }
    return t;
}

Buffer<float> make_input(int w, int h) {
    Buffer<float> in(w, h);
    in.fill([](int x, int y) { return (float)(x + y * 3); });
    return in;
}

// Two staged Funcs, each read by the output. The consumer of the first reads
// what it wrote, so the barrier after it is needed and cannot be sunk.
void test_barrier_needed() {
    const int W = 256, H = 32;
    Buffer<float> in = make_input(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func sa("sa"), sb("sb"), out("out");
    sa(x, y) = in(x, y);
    sb(x, y) = sa(x, y) + 1.f;
    out(x, y) = sb(x, y);

    out.gpu_tile(x, y, xi, yi, 64, 8);
    sa.compute_at(out, x).store_in(MemoryType::GPUShared).gpu_threads(y);
    sb.compute_at(out, x).store_in(MemoryType::GPUShared).gpu_threads(y);

    // sb reads sa, and out reads sb, so both barriers have to stay.
    check_barriers("barrier_needed",
                   count_barriers(out, {in}, gpu_target()), 2);
}

// Two staged Funcs that don't read each other. The barrier after the first
// can be sunk into the one after the second.
void test_barrier_sunk() {
    const int W = 256, H = 32;
    Buffer<float> a = make_input(W, H), b = make_input(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func sa("sa"), sb("sb"), out("out");
    sa(x, y) = a(x, y);
    sb(x, y) = b(x, y);
    out(x, y) = sa(x, y) + sb(x, y);

    out.gpu_tile(x, y, xi, yi, 64, 8);
    sa.compute_at(out, x).store_in(MemoryType::GPUShared).gpu_threads(y);
    sb.compute_at(out, x).store_in(MemoryType::GPUShared).gpu_threads(y);

    // Nothing reads sa until after sb's barrier, so one barrier covers both.
    check_barriers("barrier_sunk",
                   count_barriers(out, {a, b}, gpu_target()), 1);
}

// The same shape as above, but the second producer reads the first, so the
// barrier is needed even though the two are otherwise independent-looking.
void test_read_between_barriers() {
    const int W = 256, H = 32;
    Buffer<float> a = make_input(W, H), b = make_input(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func sa("sa"), sb("sb"), out("out");
    sa(x, y) = a(x, y);
    // Reads sa, which another thread wrote, so sa's barrier must remain.
    sb(x, y) = b(x, y) + sa(W - 1 - x, y);
    out(x, y) = sb(x, y);

    out.gpu_tile(x, y, xi, yi, 64, 8);
    sa.compute_at(out, x).store_in(MemoryType::GPUShared).gpu_threads(y);
    sb.compute_at(out, x).store_in(MemoryType::GPUShared).gpu_threads(y);

    check_barriers("read_between_barriers",
                   count_barriers(out, {a, b}, gpu_target()), 2);
}

// Run the sinking case and check the answers, so that a barrier wrongly
// elided has a chance to show up as a wrong result too.
void test_results() {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("[results] skipped, no GPU target\n");
        return;
    }

    const int W = 256, H = 32;
    Buffer<float> a = make_input(W, H), b = make_input(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func sa("sa"), sb("sb"), out("out");
    sa(x, y) = a(x, y);
    sb(x, y) = b(x, y) + sa(W - 1 - x, y);
    out(x, y) = sb(W - 1 - x, y);

    out.gpu_tile(x, y, xi, yi, 64, 8);
    sa.compute_at(out, x).store_in(MemoryType::GPUShared).gpu_threads(y);
    sb.compute_at(out, x).store_in(MemoryType::GPUShared).gpu_threads(y);

    Buffer<float> result(W, H);
    out.realize(result);
    result.copy_to_host();

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float correct = b(W - 1 - x, y) + a(x, y);
            if (result(x, y) != correct) {
                printf("[results] result(%d, %d) = %f instead of %f\n",
                       x, y, result(x, y), correct);
                failures++;
                return;
            }
        }
    }
    printf("[results] OK\n");
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature() && !t.has_feature(Target::CUDA)) {
        // The counting tests only compile, so they run anywhere the CUDA
        // backend is built, but skip if no GPU backend is available at all.
        printf("[SKIP] No GPU target available.\n");
        return 0;
    }

    test_barrier_needed();
    test_barrier_sunk();
    test_read_between_barriers();
    test_results();

    if (failures) {
        printf("%d case(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
