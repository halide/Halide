#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Halide;

// An atomic update that can't be done as a single atomic read-modify-write is
// done as a compare-and-swap loop. The new value must be computed from the
// value the cmpxchg compares against, so the loop must not contain any other
// (non-atomic) load of the location being updated. This test checks the
// generated code for host LLVM, CUDA/PTX, OpenCL, and D3D12Compute, as well as
// related upstream passes (ClampUnsafeAccesses and AddAtomicMutex).

namespace {

std::string read_file(const std::string &path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Extract and unescape an LLVM IR private constant byte string like
// `@opencl_gpu_source_kernels.data = private constant [... x i8] c"...\00"`.
std::string extract_llvm_string_global(const std::string &ll, const std::string &global_prefix) {
    size_t pos = ll.find(global_prefix);
    if (pos == std::string::npos) {
        return "";
    }
    size_t cpos = ll.find(" c\"", pos);
    if (cpos == std::string::npos) {
        return "";
    }
    size_t i = cpos + 3;
    std::string out;
    while (i < ll.size() && ll[i] != '"') {
        if (ll[i] == '\\' && i + 2 < ll.size()) {
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int hi = hex_val(ll[i + 1]);
            int lo = hex_val(ll[i + 2]);
            if (hi >= 0 && lo >= 0) {
                char ch = static_cast<char>((hi << 4) | lo);
                if (ch != '\0') {
                    out.push_back(ch);
                }
                i += 3;
                continue;
            }
        }
        out.push_back(ll[i++]);
    }
    return out;
}

// Returns the number of basic blocks containing a cmpxchg, or -1 if any
// cmpxchg block (or cas.* block) also contains a non-atomic load.
int check_cas_loops(const std::string &name, const std::string &path) {
    std::ifstream in(path);
    std::string line;
    std::vector<std::string> block;
    int cas_blocks = 0;
    bool ok = true;
    auto finish_block = [&]() {
        bool has_cmpxchg = false;
        bool is_cas_block = !block.empty() && block[0].find("cas.") == 0;
        for (const auto &l : block) {
            has_cmpxchg |= l.find(" cmpxchg ") != std::string::npos;
        }
        if (has_cmpxchg) {
            cas_blocks++;
        }
        if (has_cmpxchg || is_cas_block) {
            for (const auto &l : block) {
                if (l.find(" = load ") != std::string::npos &&
                    l.find(" = load atomic ") == std::string::npos) {
                    printf("%s: non-atomic load in a compare-and-swap loop:\n", name.c_str());
                    for (const auto &b : block) {
                        printf("  %s\n", b.c_str());
                    }
                    ok = false;
                    break;
                }
            }
        }
        block.clear();
    };
    while (std::getline(in, line)) {
        // Instructions are indented; anything else (a label, a function
        // definition, a closing brace) ends the current basic block.
        if (line.empty() || line[0] != ' ') {
            finish_block();
        }
        block.push_back(line);
    }
    finish_block();
    return ok ? cas_blocks : -1;
}

bool check_opencl_cas(const std::string &name,
                      const std::string &src,
                      const std::string &expected_cmpxchg) {
    if (src.empty()) {
        printf("%s: failed to extract OpenCL kernel source\n", name.c_str());
        return false;
    }
    if (src.find(expected_cmpxchg + "(") == std::string::npos) {
        printf("%s: expected %s in OpenCL kernel:\n%s\n",
               name.c_str(), expected_cmpxchg.c_str(), src.c_str());
        return false;
    }
    // Check lines inside `for (;;)` loop: any `_hist` array reference must be
    // inside an atomic cmpxchg call, never a plain load.
    std::istringstream in(src);
    std::string line;
    bool in_loop = false;
    int brace_depth = 0;
    int loop_count = 0;
    bool saw_feedback = false;
    while (std::getline(in, line)) {
        if (!in_loop && line.find("for (;;)") != std::string::npos) {
            in_loop = true;
            brace_depth = 1;
            loop_count++;
            continue;
        }
        if (in_loop) {
            for (char c : line) {
                if (c == '{') brace_depth++;
                if (c == '}') brace_depth--;
            }
            if (line.find("_hist") != std::string::npos &&
                line.find("[") != std::string::npos &&
                line.find(expected_cmpxchg + "(") == std::string::npos) {
                printf("%s: non-atomic buffer load inside OpenCL CAS loop:\n  %s\nFull kernel:\n%s\n",
                       name.c_str(), line.c_str(), src.c_str());
                return false;
            }
            if (line.find("old_val = prev_val") != std::string::npos ||
                line.find("old_bits = prev_bits") != std::string::npos) {
                saw_feedback = true;
            }
            if (brace_depth <= 0) {
                in_loop = false;
            }
        }
    }
    if (loop_count == 0 || !saw_feedback) {
        printf("%s: missing expected OpenCL CAS loop or cmpxchg feedback:\n%s\n",
               name.c_str(), src.c_str());
        return false;
    }
    return true;
}

bool check_d3d12_cas(const std::string &name,
                     const std::string &src,
                     const std::string &expected_interlocked) {
    if (src.empty()) {
        printf("%s: failed to extract D3D12Compute HLSL kernel source\n", name.c_str());
        return false;
    }
    if (src.find('$') != std::string::npos) {
        printf("%s: invalid '$' character in D3D12Compute HLSL identifier:\n%s\n",
               name.c_str(), src.c_str());
        return false;
    }
    if (src.find(expected_interlocked + "(") == std::string::npos) {
        printf("%s: expected %s in HLSL kernel:\n%s\n",
               name.c_str(), expected_interlocked.c_str(), src.c_str());
        return false;
    }
    // Check inside `do { ... } while`: any buffer indexing (`_hist[`) must be
    // inside the InterlockedCompareExchange* intrinsic, never a plain load.
    std::istringstream in(src);
    std::string line;
    bool in_loop = false;
    int brace_depth = 0;
    int loop_count = 0;
    bool saw_old_update = false;
    while (std::getline(in, line)) {
        if (!in_loop && line.find("do {") != std::string::npos) {
            in_loop = true;
            brace_depth = 1;
            loop_count++;
            continue;
        }
        if (in_loop) {
            for (char c : line) {
                if (c == '{') brace_depth++;
                if (c == '}') brace_depth--;
            }
            if (line.find("_hist") != std::string::npos &&
                line.find("[") != std::string::npos &&
                line.find(expected_interlocked + "(") == std::string::npos) {
                printf("%s: non-atomic buffer load inside HLSL CAS loop:\n  %s\nFull kernel:\n%s\n",
                       name.c_str(), line.c_str(), src.c_str());
                return false;
            }
            if (line.find("_old") != std::string::npos &&
                line.find("= ") != std::string::npos &&
                line.find("_orig") != std::string::npos) {
                saw_old_update = true;
            }
            if (brace_depth <= 0) {
                in_loop = false;
            }
        }
    }
    if (loop_count == 0 || !saw_old_update) {
        printf("%s: missing expected HLSL CAS loop or old=orig feedback:\n%s\n",
               name.c_str(), src.c_str());
        return false;
    }
    return true;
}

bool check_cuda_cas(const std::string &name, const std::string &ptx) {
    if (ptx.empty()) {
        printf("%s: failed to extract CUDA PTX kernel source\n", name.c_str());
        return false;
    }
    if (ptx.find("atom.global.cas.") == std::string::npos) {
        printf("%s: no atom.global.cas found in PTX:\n%s\n", name.c_str(), ptx.c_str());
        return false;
    }
    std::istringstream in(ptx);
    std::string line;
    bool in_cas = false;
    while (std::getline(in, line)) {
        if (line.find("%casloop.start") != std::string::npos) {
            in_cas = true;
            continue;
        }
        if (line.find("%casloop.end") != std::string::npos) {
            in_cas = false;
            continue;
        }
        if (in_cas && line.find("ld.global.") != std::string::npos) {
            printf("%s: non-volatile ld.global inside CUDA PTX CAS loop:\n  %s\nFull PTX:\n%s\n",
                   name.c_str(), line.c_str(), ptx.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    const Target target = get_host_target()
                              .with_feature(Target::NoAsserts)
                              .with_feature(Target::NoRuntime)
                              .with_feature(Target::NoBoundsQuery);
    const std::string dir = Internal::get_test_tmp_dir();

    const int hist_size = 7;
    bool ok = true;

    // 1. Host LLVM CAS loop checks.
    for (int i = 0; i < 8; i++) {
        Func hist("hist");
        Var x("x");
        RDom r(0, 1000, "r");
        Expr idx = r % hist_size;
        std::string name;
        int vector_width = 1;
        bool check_no_mutex = false;
        switch (i) {
        case 0:
            name = "min_i32";
            hist(x) = 0;
            hist(idx) = min(hist(idx) + 1, 100);
            break;
        case 1:
            name = "max_f32";
            hist(x) = 0.0f;
            hist(idx) = max(hist(idx), cast<float>(r));
            break;
        case 2:
            name = "mul_u32";
            hist(x) = cast<uint32_t>(1);
            hist(idx) = hist(idx) * cast<uint32_t>(3);
            break;
        case 3:
            name = "min_i32_vectorized";
            hist(x) = 0;
            hist(idx) = min(hist(idx) + 1, 100);
            vector_width = 8;
            break;
        case 4:
            name = "mul_u32_vectorized_ramp";
            hist(x) = cast<uint32_t>(1);
            hist(r) = hist(r) * cast<uint32_t>(3);
            vector_width = 8;
            break;
        case 5:
            name = "min_f16";
            hist(x) = cast<float16_t>(0);
            hist(idx) = min(hist(idx) + cast<float16_t>(1), cast<float16_t>(100));
            break;
        case 6:
            // Repeated self-load introduces an internal Let inside Store::value.
            // AddAtomicMutex must not force a mutex lock for this.
            name = "repeated_self_load";
            hist(x) = 1;
            hist(idx) = select(idx == 0, hist(idx) * 2, hist(idx) + 1);
            check_no_mutex = true;
            break;
        case 7:
            // Runtime-conditional alias: hist(0) may alias hist(idx).
            // All loads inside the CAS loop must be atomic monotonic.
            name = "conditional_alias";
            hist(x) = 1;
            hist(idx) = select(idx == 0, hist(0) * 2, hist(idx) + 1);
            check_no_mutex = true;
            break;
        }

        if (vector_width > 1) {
            RVar ro("ro"), ri("ri");
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, vector_width)
                .parallel(ro)
                .vectorize(ri);
        } else {
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .parallel(r);
        }

        const std::string ll = dir + "atomics_cas_loop_" + name + ".ll";
        hist.compile_to_llvm_assembly(ll, {}, "atomics_cas_loop_" + name, target);
        Internal::assert_file_exists(ll);

        int cas_blocks = check_cas_loops(name, ll);
        if (cas_blocks == 0) {
            printf("%s: no compare-and-swap loop found in %s\n", name.c_str(), ll.c_str());
            ok = false;
        } else if (cas_blocks < 0) {
            ok = false;
        }
        if (check_no_mutex) {
            std::string contents = read_file(ll);
            if (contents.find("halide_mutex_array_lock") != std::string::npos) {
                printf("%s: unexpectedly emitted mutex lock in %s\n", name.c_str(), ll.c_str());
                ok = false;
            }
        }
    }

    // 2. ClampUnsafeAccesses must clamp Provide::args symmetrically with
    // Call::args so an atomic += indexed by a compute_root producer stays on
    // the native atomicrmw add fast path instead of falling back to a CAS loop.
    {
        Func producer("producer"), hist("hist");
        Var x("x");
        RDom r(0, 1000, "r");
        producer(x) = (x * x) % hist_size;
        hist(x) = 0;
        hist(producer(r)) += 1;
        producer.compute_root().parallel(x);
        hist.compute_root();
        hist.update().atomic(true /*override_associativity_test*/).parallel(r);

        const std::string ll = dir + "atomics_cas_loop_clamp_add.ll";
        hist.compile_to_llvm_assembly(ll, {}, "atomics_cas_loop_clamp_add", target);
        Internal::assert_file_exists(ll);
        std::string contents = read_file(ll);
        if (contents.find("atomicrmw add") == std::string::npos ||
            contents.find(" cmpxchg ") != std::string::npos) {
            printf("clamp_add: expected atomicrmw add without cmpxchg in %s\n", ll.c_str());
            ok = false;
        }
    }

    // 3. OpenCL CAS loop codegen checks (compile-only, no device required).
    {
        const Target ocl_target = target.with_feature(Target::OpenCL);
        if (ocl_target.supported()) {
            for (int i = 0; i < 4; i++) {
                Func hist("hist");
                Var x("x");
                RDom r(0, 1024, "r");
                RVar ro("ro"), ri("ri");
                Expr idx = r % hist_size;
                std::string name;
                Target t = ocl_target;
                std::string expected_fn = "atomic_cmpxchg";
                switch (i) {
                case 0:
                    name = "opencl_min_i32";
                    hist(x) = 0;
                    hist(idx) = min(hist(idx) + 1, 100);
                    break;
                case 1:
                    name = "opencl_max_f32";
                    hist(x) = 0.0f;
                    hist(idx) = max(hist(idx), cast<float>(r));
                    break;
                case 2:
                    name = "opencl_max_f64";
                    t = ocl_target.with_feature(Target::CLAtomics64).with_feature(Target::CLDoubles);
                    hist(x) = cast<double>(0);
                    hist(idx) = max(hist(idx), cast<double>(r));
                    expected_fn = "atom_cmpxchg";
                    break;
                case 3:
                    name = "opencl_cond_alias";
                    hist(x) = 1;
                    hist(idx) = select(idx == 0, hist(0) * 2, hist(idx) + 1);
                    break;
                }
                if (!t.supported()) {
                    continue;
                }
                hist.compute_root();
                hist.update()
                    .atomic(true /*override_associativity_test*/)
                    .split(r, ro, ri, 32)
                    .gpu_blocks(ro, DeviceAPI::OpenCL)
                    .gpu_threads(ri, DeviceAPI::OpenCL);

                const std::string ll = dir + "atomics_cas_loop_" + name + ".ll";
                hist.compile_to_llvm_assembly(ll, {}, "atomics_cas_loop_" + name, t);
                Internal::assert_file_exists(ll);
                std::string src = extract_llvm_string_global(read_file(ll), "@opencl_gpu_source_kernels");
                ok &= check_opencl_cas(name, src, expected_fn);
            }
        }
    }

    // 4. D3D12Compute CAS loop codegen checks (compile-only, no device required).
    {
        const Target d3d_target = target.with_feature(Target::D3D12Compute)
                                      .with_feature(Target::HLSL_SM66);
        if (d3d_target.supported()) {
            for (int i = 0; i < 4; i++) {
                Func hist("hist");
                Var x("x");
                RDom r(0, 1024, "r");
                RVar ro("ro"), ri("ri");
                Expr idx = r % hist_size;
                std::string name;
                std::string expected_fn = "InterlockedCompareExchange";
                switch (i) {
                case 0:
                    name = "d3d12_min_i32";
                    hist(x) = 0;
                    hist(idx) = min(hist(idx) + 1, 100);
                    break;
                case 1:
                    name = "d3d12_add_f32";
                    hist(x) = 0.0f;
                    hist(idx) += 1.0f;
                    expected_fn = "InterlockedCompareExchangeFloatBitwise";
                    break;
                case 2:
                    name = "d3d12_cond_alias";
                    hist(x) = 1;
                    hist(idx) = select(idx == 0, hist(0) * 2, hist(idx) + 1);
                    break;
                case 3: {
                    // Two CAS updates in the same module to verify unique_name
                    // never emits invalid '$' characters in HLSL identifiers.
                    name = "d3d12_two_cas";
                    hist(x) = 0;
                    hist(idx) = min(hist(idx) + 1, 100);
                    hist(idx) = max(hist(idx) - 1, 0);
                    break;
                }
                }
                hist.compute_root();
                for (int u = 0; u < hist.num_update_definitions(); u++) {
                    RVar uro("uro"), uri("uri");
                    hist.update(u)
                        .atomic(true /*override_associativity_test*/)
                        .split(r, uro, uri, 32)
                        .gpu_blocks(uro, DeviceAPI::D3D12Compute)
                        .gpu_threads(uri, DeviceAPI::D3D12Compute);
                }

                const std::string ll = dir + "atomics_cas_loop_" + name + ".ll";
                hist.compile_to_llvm_assembly(ll, {}, "atomics_cas_loop_" + name, d3d_target);
                Internal::assert_file_exists(ll);
                std::string src = extract_llvm_string_global(read_file(ll), "@d3d12compute_gpu_source_kernels");
                ok &= check_d3d12_cas(name, src, expected_fn);
            }
        }
    }

    // 5. CUDA/PTX CAS loop codegen checks (compile-only, no device required).
    {
        const Target cuda_target = target.with_feature(Target::CUDA);
        if (cuda_target.supported()) {
            for (int i = 0; i < 2; i++) {
                Func hist("hist");
                Var x("x");
                RDom r(0, 1024, "r");
                RVar ro("ro"), ri("ri"), rio("rio"), rii("rii");
                std::string name;
                if (i == 0) {
                    name = "cuda_min_i32";
                    Expr idx = r % hist_size;
                    hist(x) = 0;
                    hist(idx) = min(hist(idx) + 1, 100);
                    hist.compute_root();
                    hist.update()
                        .atomic(true /*override_associativity_test*/)
                        .split(r, ro, ri, 32)
                        .gpu_blocks(ro, DeviceAPI::CUDA)
                        .gpu_threads(ri, DeviceAPI::CUDA);
                } else {
                    // 4-wide vectorized 32-bit atomic store must not be
                    // rewritten into an unsupported i128 store on PTX.
                    name = "cuda_mul_u32_vec4";
                    hist(x) = cast<uint32_t>(1);
                    hist(r) = hist(r) * cast<uint32_t>(3);
                    hist.compute_root();
                    hist.update()
                        .atomic(true /*override_associativity_test*/)
                        .split(r, ro, ri, 32)
                        .split(ri, rio, rii, 4)
                        .gpu_blocks(ro, DeviceAPI::CUDA)
                        .gpu_threads(rio, DeviceAPI::CUDA)
                        .vectorize(rii);
                }
                const std::string ll = dir + "atomics_cas_loop_" + name + ".ll";
                hist.compile_to_llvm_assembly(ll, {}, "atomics_cas_loop_" + name, cuda_target);
                Internal::assert_file_exists(ll);
                std::string ptx = extract_llvm_string_global(read_file(ll), "@cuda_gpu_source_kernels");
                ok &= check_cuda_cas(name, ptx);
            }
        }
    }

    // 6. Runtime correctness check for repeated self-load and conditional alias
    // CAS updates under parallel execution.
    {
        for (int mode = 0; mode < 2; mode++) {
            Func hist("hist_rt");
            Var x("x");
            RDom r(0, 100, "r");
            Expr idx = r % hist_size;
            hist(x) = 1;
            if (mode == 0) {
                hist(idx) = select(idx == 0, hist(idx) * 2, hist(idx) + 1);
            } else {
                hist(idx) = select(idx == 0, hist(0) * 2, hist(idx) + 1);
            }
            hist.compute_root();
            hist.update().atomic(true /*override_associativity_test*/).parallel(r);

            std::vector<int> expected(hist_size, 1);
            for (int i = 0; i < 100; i++) {
                int k = i % hist_size;
                if (k == 0) {
                    expected[0] *= 2;
                } else {
                    expected[k] += 1;
                }
            }
            for (int iter = 0; iter < 10; iter++) {
                Buffer<int> out = hist.realize({hist_size});
                for (int k = 0; k < hist_size; k++) {
                    if (out(k) != expected[k]) {
                        printf("runtime mode %d iter %d: out(%d) = %d, expected %d\n",
                               mode, iter, k, out(k), expected[k]);
                        ok = false;
                    }
                }
            }
        }
    }

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
