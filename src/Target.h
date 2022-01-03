#ifndef HALIDE_TARGET_H
#define HALIDE_TARGET_H

/** \file
 * Defines the structure that describes a Halide target.
 */

#include <bitset>
#include <cstdint>
#include <string>

#include "DeviceAPI.h"
#include "Type.h"
#include "runtime/HalideRuntime.h"

namespace Halide {

/** A struct representing a target machine and os to generate code for. */
struct Target {
    /** The operating system used by the target. Determines which
     * system calls to generate.
     * Corresponds to os_name_map in Target.cpp. */
    enum OS {
        OSUnknown = 0,
        Linux,
        Windows,
        OSX,
        Android,
        IOS,
        QuRT,
        NoOS,
        Fuchsia,
        WebAssemblyRuntime
    } os = OSUnknown;

    /** The architecture used by the target. Determines the
     * instruction set to use.
     * Corresponds to arch_name_map in Target.cpp. */
    enum Arch {
        ArchUnknown = 0,
        X86,
        ARM,
        MIPS,
        Hexagon,
        POWERPC,
        WebAssembly,
        RISCV
    } arch = ArchUnknown;

    /** The bit-width of the target machine. Must be 0 for unknown, or 32 or 64. */
    int bits = 0;

    /** Optional features a target can have.
     * Corresponds to feature_name_map in Target.cpp.
     * See definitions in HalideRuntime.h for full information.
     */
    enum Feature {
        JIT = halide_target_feature_jit,
        Debug = halide_target_feature_debug,
        NoAsserts = halide_target_feature_no_asserts,
        NoBoundsQuery = halide_target_feature_no_bounds_query,
        SSE41 = halide_target_feature_sse41,
        AVX = halide_target_feature_avx,
        AVX2 = halide_target_feature_avx2,
        FMA = halide_target_feature_fma,
        FMA4 = halide_target_feature_fma4,
        F16C = halide_target_feature_f16c,
        ARMv7s = halide_target_feature_armv7s,
        NoNEON = halide_target_feature_no_neon,
        VSX = halide_target_feature_vsx,
        POWER_ARCH_2_07 = halide_target_feature_power_arch_2_07,
        CUDA = halide_target_feature_cuda,
        CUDACapability30 = halide_target_feature_cuda_capability30,
        CUDACapability32 = halide_target_feature_cuda_capability32,
        CUDACapability35 = halide_target_feature_cuda_capability35,
        CUDACapability50 = halide_target_feature_cuda_capability50,
        CUDACapability61 = halide_target_feature_cuda_capability61,
        CUDACapability70 = halide_target_feature_cuda_capability70,
        CUDACapability75 = halide_target_feature_cuda_capability75,
        CUDACapability80 = halide_target_feature_cuda_capability80,
        CUDACapability86 = halide_target_feature_cuda_capability86,
        OpenCL = halide_target_feature_opencl,
        CLDoubles = halide_target_feature_cl_doubles,
        CLHalf = halide_target_feature_cl_half,
        CLAtomics64 = halide_target_feature_cl_atomic64,
        OpenGLCompute = halide_target_feature_openglcompute,
        EGL = halide_target_feature_egl,
        UserContext = halide_target_feature_user_context,
        Matlab = halide_target_feature_matlab,
        Profile = halide_target_feature_profile,
        NoRuntime = halide_target_feature_no_runtime,
        Metal = halide_target_feature_metal,
        CPlusPlusMangling = halide_target_feature_c_plus_plus_mangling,
        LargeBuffers = halide_target_feature_large_buffers,
        HexagonDma = halide_target_feature_hexagon_dma,
        HVX_128 = halide_target_feature_hvx_128,
        HVX = HVX_128,
        HVX_v62 = halide_target_feature_hvx_v62,
        HVX_v65 = halide_target_feature_hvx_v65,
        HVX_v66 = halide_target_feature_hvx_v66,
        HVX_shared_object = halide_target_feature_hvx_use_shared_object,
        FuzzFloatStores = halide_target_feature_fuzz_float_stores,
        SoftFloatABI = halide_target_feature_soft_float_abi,
        MSAN = halide_target_feature_msan,
        AVX512 = halide_target_feature_avx512,
        AVX512_KNL = halide_target_feature_avx512_knl,
        AVX512_Skylake = halide_target_feature_avx512_skylake,
        AVX512_Cannonlake = halide_target_feature_avx512_cannonlake,
        AVX512_SapphireRapids = halide_target_feature_avx512_sapphirerapids,
        TraceLoads = halide_target_feature_trace_loads,
        TraceStores = halide_target_feature_trace_stores,
        TraceRealizations = halide_target_feature_trace_realizations,
        TracePipeline = halide_target_feature_trace_pipeline,
        D3D12Compute = halide_target_feature_d3d12compute,
        StrictFloat = halide_target_feature_strict_float,
        TSAN = halide_target_feature_tsan,
        ASAN = halide_target_feature_asan,
        CheckUnsafePromises = halide_target_feature_check_unsafe_promises,
        EmbedBitcode = halide_target_feature_embed_bitcode,
        EnableLLVMLoopOpt = halide_target_feature_enable_llvm_loop_opt,
        DisableLLVMLoopOpt = halide_target_feature_disable_llvm_loop_opt,
        WasmSimd128 = halide_target_feature_wasm_simd128,
        WasmSignExt = halide_target_feature_wasm_signext,
        WasmSatFloatToInt = halide_target_feature_wasm_sat_float_to_int,
        WasmThreads = halide_target_feature_wasm_threads,
        WasmBulkMemory = halide_target_feature_wasm_bulk_memory,
        SVE = halide_target_feature_sve,
        SVE2 = halide_target_feature_sve2,
        ARMDotProd = halide_target_feature_arm_dot_prod,
        ARMFp16 = halide_target_feature_arm_fp16,
        LLVMLargeCodeModel = halide_llvm_large_code_model,
        RVV = halide_target_feature_rvv,
        ARMv81a = halide_target_feature_armv81a,
        SanitizerCoverage = halide_target_feature_sanitizer_coverage,
        FeatureEnd = halide_target_feature_end
    };
    Target() = default;
    Target(OS o, Arch a, int b, const std::vector<Feature> &initial_features = std::vector<Feature>())
        : os(o), arch(a), bits(b) {
        for (const auto &f : initial_features) {
            set_feature(f);
        }
    }

    /** Given a string of the form used in HL_TARGET
     * (e.g. "x86-64-avx"), construct the Target it specifies. Note
     * that this always starts with the result of get_host_target(),
     * replacing only the parts found in the target string, so if you
     * omit (say) an OS specification, the host OS will be used
     * instead. An empty string is exactly equivalent to
     * get_host_target().
     *
     * Invalid target strings will fail with a user_error.
     */
    // @{
    explicit Target(const std::string &s);
    explicit Target(const char *s);
    // @}

    /** Check if a target string is valid. */
    static bool validate_target_string(const std::string &s);

    /** Return true if any of the arch/bits/os fields are "unknown"/0;
        return false otherwise. */
    bool has_unknowns() const;

    void set_feature(Feature f, bool value = true);

    void set_features(const std::vector<Feature> &features_to_set, bool value = true);

    bool has_feature(Feature f) const;

    inline bool has_feature(halide_target_feature_t f) const {
        return has_feature((Feature)f);
    }

    bool features_any_of(const std::vector<Feature> &test_features) const;

    bool features_all_of(const std::vector<Feature> &test_features) const;

    /** Return a copy of the target with the given feature set.
     * This is convenient when enabling certain features (e.g. NoBoundsQuery)
     * in an initialization list, where the target to be mutated may be
     * a const reference. */
    Target with_feature(Feature f) const;

    /** Return a copy of the target with the given feature cleared.
     * This is convenient when disabling certain features (e.g. NoBoundsQuery)
     * in an initialization list, where the target to be mutated may be
     * a const reference. */
    Target without_feature(Feature f) const;

    /** Is a fully feature GPU compute runtime enabled? I.e. is
     * Func::gpu_tile and similar going to work? Currently includes
     * CUDA, OpenCL, Metal and D3D12Compute. We do not include OpenGL,
     * because it is not capable of gpgpu, and is not scheduled via
     * Func::gpu_tile.
     * TODO: Should OpenGLCompute be included here? */
    bool has_gpu_feature() const;

    /** Does this target allow using a certain type. Generally all
     * types except 64-bit float and int/uint should be supported by
     * all backends.
     *
     * It is likely better to call the version below which takes a DeviceAPI.
     */
    bool supports_type(const Type &t) const;

    /** Does this target allow using a certain type on a certain device.
     * This is the prefered version of this routine.
     */
    bool supports_type(const Type &t, DeviceAPI device) const;

    /** Returns whether a particular device API can be used with this
     * Target. */
    bool supports_device_api(DeviceAPI api) const;

    /** If this Target (including all Features) requires a specific DeviceAPI,
     * return it. If it doesn't, return DeviceAPI::None.  If the Target has
     * features with multiple (different) DeviceAPI requirements, the result
     * will be an arbitrary DeviceAPI. */
    DeviceAPI get_required_device_api() const;

    bool operator==(const Target &other) const {
        return os == other.os &&
               arch == other.arch &&
               bits == other.bits &&
               features == other.features;
    }

    bool operator!=(const Target &other) const {
        return !(*this == other);
    }

    /**
     * Create a "greatest common denominator" runtime target that is compatible with
     * both this target and \p other. Used by generators to conveniently select a suitable
     * runtime when linking together multiple functions.
     *
     * @param other The other target from which we compute the gcd target.
     * @param[out] result The gcd target if we return true, otherwise unmodified. Can be the same as *this.
     * @return Whether it was possible to find a compatible target (true) or not.
     */
    bool get_runtime_compatible_target(const Target &other, Target &result);

    /** Convert the Target into a string form that can be reconstituted
     * by merge_string(), which will always be of the form
     *
     *   arch-bits-os-feature1-feature2...featureN.
     *
     * Note that is guaranteed that Target(t1.to_string()) == t1,
     * but not that Target(s).to_string() == s (since there can be
     * multiple strings that parse to the same Target)...
     * *unless* t1 contains 'unknown' fields (in which case you'll get a string
     * that can't be parsed, which is intentional).
     */
    std::string to_string() const;

    /** Given a data type, return an estimate of the "natural" vector size
     * for that data type when compiling for this Target. */
    int natural_vector_size(const Halide::Type &t) const;

    /** Given a data type, return an estimate of the "natural" vector size
     * for that data type when compiling for this Target. */
    template<typename data_t>
    int natural_vector_size() const {
        return natural_vector_size(type_of<data_t>());
    }

    /** Return true iff 64 bits and has_feature(LargeBuffers). */
    bool has_large_buffers() const {
        return bits == 64 && has_feature(LargeBuffers);
    }

    /** Return the maximum buffer size in bytes supported on this
     * Target. This is 2^31 - 1 except on 64-bit targets when the LargeBuffers
     * feature is enabled, which expands the maximum to 2^63 - 1. */
    int64_t maximum_buffer_size() const {
        if (has_large_buffers()) {
            return (((uint64_t)1) << 63) - 1;
        } else {
            return (((uint64_t)1) << 31) - 1;
        }
    }

    /** Get the minimum cuda capability found as an integer. Returns
     * 20 (our minimum supported cuda compute capability) if no cuda
     * features are set. */
    int get_cuda_capability_lower_bound() const;

    /** Was libHalide compiled with support for this target? */
    bool supported() const;

    /** Return a bitset of the Featuress set in this Target (set = 1).
     * Note that while this happens to be the current internal representation,
     * that might not always be the case. */
    const std::bitset<FeatureEnd> &get_features_bitset() const {
        return features;
    }

    /** Return the name corresponding to a given Feature, in the form
     * used to construct Target strings (e.g., Feature::Debug is "debug" and not "Debug"). */
    static std::string feature_to_name(Target::Feature feature);

    /** Return the feature corresponding to a given name, in the form
     * used to construct Target strings (e.g., Feature::Debug is "debug" and not "Debug").
     * If the string is not a known feature name, return FeatureEnd. */
    static Target::Feature feature_from_name(const std::string &name);

private:
    /** A bitmask that stores the active features. */
    std::bitset<FeatureEnd> features;
};

/** Return the target corresponding to the host machine. */
Target get_host_target();

/** Return the target that Halide will use. If HL_TARGET is set it
 * uses that. Otherwise calls \ref get_host_target */
Target get_target_from_environment();

/** Return the target that Halide will use for jit-compilation. If
 * HL_JIT_TARGET is set it uses that. Otherwise calls \ref
 * get_host_target. Throws an error if the architecture, bit width,
 * and OS of the target do not match the host target, so this is only
 * useful for controlling the feature set. */
Target get_jit_target_from_environment();

/** Get the Target feature corresponding to a DeviceAPI. For device
 * apis that do not correspond to any single target feature, returns
 * Target::FeatureEnd */
Target::Feature target_feature_for_device_api(DeviceAPI api);

namespace Internal {

void target_test();
}

}  // namespace Halide

#endif
