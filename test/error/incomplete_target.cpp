#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestIncompleteTarget() {
    Target t("debug");
}
}  // namespace

TEST(ErrorTests, IncompleteTarget) {
    EXPECT_COMPILE_ERROR(TestIncompleteTarget, MatchesPattern(R"(Did not understand Halide target debug\nExpected format is arch-bits-os-processor-feature\d+-feature\d+-\.\.\.\nWhere arch is: arch_unknown, arm, hexagon, powerpc, riscv, wasm, x\d+\.\nbits is either 32 or 64\.\nos is: android, fuchsia, ios, linux, noos, os_unknown, osx, qurt, wasmrt, windows\.\nprocessor is: tune_amdfam\d+, tune_bdver\d+, tune_bdver\d+, tune_bdver\d+, tune_bdver\d+, tune_btver\d+, tune_btver\d+, tune_generic, tune_k\d+, tune_k\d+_sse\d+, tune_znver\d+, tune_znver\d+, tune_znver\d+, tune_znver\d+, tune_znver\d+\.\n\nIf arch, bits, or os are omitted, they default to the host\.\n\nIf processor is omitted, it defaults to tune_generic\.\n\nFeatures are: arm_dot_prod, arm_fp\d+, armv\d+s, armv\d+a, armv\d+a, armv\d+a, armv\d+a\narmv\d+a, armv\d+a, armv\d+a, armv\d+a, armv\d+a, armv\d+a, asan, avx, avx\d+_\d+\navx\d+, avx\d+, avx\d+_cannonlake, avx\d+_knl, avx\d+_sapphirerapids, avx\d+_skylake\navx\d+_zen\d+, avx\d+_zen\d+, avxvnni, c_plus_plus_name_mangling, check_unsafe_promises\ncl_atomics\d+, cl_doubles, cl_half, cuda, cuda_capability_\d+, cuda_capability_\d+\ncuda_capability_\d+, cuda_capability_\d+, cuda_capability_\d+, cuda_capability_\d+\ncuda_capability_\d+, cuda_capability_\d+, cuda_capability_\d+, d\d+d\d+compute\ndebug, egl, embed_bitcode, enable_backtraces, enable_llvm_loop_opt, f\d+c\nfma, fma\d+, fuzz_float_stores, hexagon_dma, hvx, hvx_\d+, hvx_v\d+, hvx_v\d+\nhvx_v\d+, hvx_v\d+, jit, large_buffers, llvm_large_code_model, metal, msan\nno_asserts, no_bounds_query, no_neon, no_runtime, opencl, power_arch_\d+_\d+\nprofile, profile_by_timer, rvv, sanitizer_coverage, semihosting, soft_float_abi\nspirv, sse\d+, strict_float, sve, sve\d+, trace_loads, trace_pipeline, trace_realizations\ntrace_stores, tsan, user_context, vk_float\d+, vk_float\d+, vk_int\d+, vk_int\d+\nvk_int\d+, vk_v\d+, vk_v\d+, vk_v\d+, vsx, vulkan, wasm_bulk_memory, wasm_mvponly\nwasm_simd\d+, wasm_threads, webgpu, x\d+apx\.\n\nThe target can also begin with \"host\", which sets the host's architecture, os, and feature set, with the exception of the GPU runtimes, which default to off\.\n\nOn this platform, the host target is: arm-64-osx-arm_dot_prod-arm_fp\d+)"));
}
