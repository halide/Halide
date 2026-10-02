#include "Halide.h"
#include <cmath>
#include <cstdio>

using namespace Halide;

// GPU torture test for struct types (see Type::Struct, test/correctness/struct_type.cpp):
// a struct-valued Func consumed on the GPU, once compute_at'd inside a
// gpu_threads() loop (thread-local/register scratch on the device) and once
// compute_at'd at the gpu_blocks() level and stored in GPUShared memory.
//
// Device-side codegen is a completely separate code generator per backend,
// with its own type-to-size mapping -- this is what caught two bugs beyond
// the CPU LLVM backend's own (CodeGen_LLVM::llvm_type_of mapping a struct to
// i8 instead of an array of its true byte size): CodeGen_Metal_Dev's
// non-shared Allocate path under-sized a struct-typed stack scratch buffer
// the same way, and FindIntrinsics.cpp's generic lower_intrinsic fallback
// (used by every CodeGen_C-derived backend, i.e. every GPU target here) had
// no case for extract_bits/concat_bits at all -- those were only ever
// handled by CodeGen_LLVM calling the lowering helpers directly. Both fixed
// in CodeGen_Metal_Dev.cpp / FindIntrinsics.cpp. Run with
// HL_JIT_TARGET=host-metal or HL_JIT_TARGET=host-opencl (etc.) to select a
// backend; skipped if the environment doesn't select a GPU target at all.
void run_variant(const char *name, float d_offset, int code_mul, MemoryType producer_storage) {
    Type block_t = Type::Struct({{"d", Float(32)}, {"qs", Int(8), 8}});
    const int num_blocks = 6;

    Var i("i"), blk("blk"), bo("bo"), bi("bi");

    Func producer("producer");
    std::vector<Expr> vals;
    vals.push_back(cast<float>(blk) + d_offset);
    for (int k = 0; k < 8; k++) {
        vals.push_back(cast<int8_t>(blk * code_mul + k));
    }
    producer(blk) = pack_struct(block_t, vals);

    Func result("result");
    result(i, blk) = cast<float>(field(producer(blk), "qs")[i]) * cast<float>(field(producer(blk), "d"));

    result.gpu_tile(blk, bo, bi, 1);
    if (producer_storage == MemoryType::GPUShared) {
        producer.compute_at(result, bo);
        producer.store_in(MemoryType::GPUShared);
    } else {
        producer.compute_at(result, bi);
    }

    Buffer<float> out = result.realize({8, num_blocks});
    out.copy_to_host();

    for (int b = 0; b < num_blocks; b++) {
        float d = (float)b + d_offset;
        for (int k = 0; k < 8; k++) {
            float expected = (float)(int8_t)(b * code_mul + k) * d;
            if (std::abs(out(k, b) - expected) > 1e-4f) {
                printf("gpu_struct_type (%s): result(%d, %d) = %f instead of %f\n",
                       name, k, b, out(k, b), expected);
                exit(1);
            }
        }
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    run_variant("gpu_threads compute_at", 0.5f, 3, MemoryType::Register);
    run_variant("gpu shared compute_at", 1.5f, 5, MemoryType::GPUShared);

    printf("Success!\n");
    return 0;
}
