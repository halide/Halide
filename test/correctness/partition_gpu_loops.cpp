// Loop partitioning used to refuse any loop whose body contained a thread
// barrier or a warp shuffle, which ruled out most of the interesting loops in
// a GPU kernel. It now partitions them, and instead throws away only those
// simplifications whose condition depends on which thread is asking. Those are
// the unsafe ones: a condition that differs between the threads of a block has
// no range of the loop over which it holds, and partitioning on it would leave
// the threads at different copies of the same barrier. The clamp below is such
// a condition. For the same thing one level down, where a lane reads another
// lane's register, see correctness_register_shuffle.

#include "Halide.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

namespace {

Target cuda_target() {
    return get_host_target()
        .with_feature(Target::CUDA)
        .with_feature(Target::CUDACapability80);
}

// A stencil staged through shared memory, so the kernel has a barrier in it,
// with a boundary condition worth partitioning out of the loop over blocks.
Func stencil_pipeline(ImageParam in) {
    Var x("x"), y("y"), xo("xo"), xi("xi");
    Func staged("staged"), out("out");
    staged(x, y) = in(clamp(x, 0, in.dim(0).extent() - 1), y);
    out(x, y) = staged(x - 1, y) + staged(x, y) + staged(x + 1, y);

    out.gpu_tile(x, xo, xi, 32);
    staged.compute_at(out, xo).store_in(MemoryType::GPUShared).gpu_threads(x);
    return out;
}

bool check_partitioned() {
    ImageParam in(Int(32), 2, "in");
    Func out = stencil_pipeline(in);
    std::string path = Internal::get_test_tmp_dir() + "partition_gpu_loops.s";
    out.compile_to_assembly(path, {in}, "out", cuda_target());
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string asm_text = ss.str();

    // Partitioning duplicates the body, and with it the barrier. One copy
    // means the loop was left alone.
    size_t barriers = 0;
    for (size_t i = asm_text.find("bar.sync"); i != std::string::npos;
         i = asm_text.find("bar.sync", i + 1)) {
        barriers++;
    }
    if (barriers < 2) {
        printf("FAIL: found %d bar.sync, so the loop was not partitioned\n",
               (int)barriers);
        return false;
    }
    return true;
}

bool check_answer() {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] No CUDA feature in the target, so not running.\n");
        return true;
    }
    const int W = 200, H = 8;
    Buffer<int> input(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = x * 7 + y;
        }
    }
    ImageParam in(Int(32), 2, "in");
    in.set(input);
    Func out = stencil_pipeline(in);
    Buffer<int> result = out.realize({W, H}, t);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            auto sample = [&](int i) {
                return input(std::min(std::max(i, 0), W - 1), y);
            };
            int want = sample(x - 1) + sample(x) + sample(x + 1);
            if (result(x, y) != want) {
                printf("FAIL: out(%d, %d) = %d, expected %d\n",
                       x, y, result(x, y), want);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (!check_partitioned() || !check_answer()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
