#ifndef HALIDE_TARGET_H
#define HALIDE_TARGET_H

/** \file
 * Defines the structure that describes a Halide target.
 */

#include <stdint.h>
#include <bitset>
#include <string>

#include "Error.h"
#include "Type.h"
#include "Util.h"
#include "Expr.h"
#include "runtime/HalideRuntime.h"

namespace Halide {

/** A struct representing a target machine and os to generate code for. */
struct Target {
    /** The operating system used by the target. Determines which
     * system calls to generate.
     * Corresponds to os_name_map in Target.cpp. */
    enum OS {OSUnknown = 0, Linux, Windows, OSX, Android, IOS, QuRT, NoOS} os;

    /** The architecture used by the target. Determines the
     * instruction set to use.
     * Corresponds to arch_name_map in Target.cpp. */
    enum Arch {ArchUnknown = 0, X86, ARM, MIPS, Hexagon, POWERPC} arch;

    /** The bit-width of the target machine. Must be 0 for unknown, or 32 or 64. */
    int bits;

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
        OpenCL = halide_target_feature_opencl,
        CLDoubles = halide_target_feature_cl_doubles,
        OpenGL = halide_target_feature_opengl,
        OpenGLCompute = halide_target_feature_openglcompute,
        UserContext = halide_target_feature_user_context,
        Matlab = halide_target_feature_matlab,
        Profile = halide_target_feature_profile,
        NoRuntime = halide_target_feature_no_runtime,
        Metal = halide_target_feature_metal,
        MinGW = halide_target_feature_mingw,
        CPlusPlusMangling = halide_target_feature_c_plus_plus_mangling,
        LargeBuffers = halide_target_feature_large_buffers,
        HVX_64 = halide_target_feature_hvx_64,
        HVX_128 = halide_target_feature_hvx_128,
        HVX_v62 = halide_target_feature_hvx_v62,
        FuzzFloatStores = halide_target_feature_fuzz_float_stores,
        SoftFloatABI = halide_target_feature_soft_float_abi,
        MSAN = halide_target_feature_msan,
        AVX512 = halide_target_feature_avx512,
        AVX512_KNL = halide_target_feature_avx512_knl,
        AVX512_Skylake = halide_target_feature_avx512_skylake,
        AVX512_Cannonlake = halide_target_feature_avx512_cannonlake,
        FeatureEnd = halide_target_feature_end
    };
    Target() : os(OSUnknown), arch(ArchUnknown), bits(0) {}
    Target(OS o, Arch a, int b, std::vector<Feature> initial_features = std::vector<Feature>())
        : os(o), arch(a), bits(b) {
        for (size_t i = 0; i < initial_features.size(); i++) {
            set_feature(initial_features[i]);
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
    EXPORT explicit Target(const std::string &s);
    EXPORT explicit Target(const char *s);
    // @}

    /** Check if a target string is valid. */
    EXPORT static bool validate_target_string(const std::string &s);

    void set_feature(Feature f, bool value = true) {
        if (f == FeatureEnd) return;
        user_assert(f < FeatureEnd) << "Invalid Target feature.\n";
        features.set(f, value);
    }

    void set_features(std::vector<Feature> features_to_set, bool value = true) {
        for (Feature f : features_to_set) {
            set_feature(f, value);
        }
    }

    bool has_feature(Feature f) const {
        if (f == FeatureEnd) return true;
        user_assert(f < FeatureEnd) << "Invalid Target feature.\n";
        return features[f];
    }

    bool features_any_of(std::vector<Feature> test_features) const {
        for (Feature f : test_features) {
            if (has_feature(f)) {
                return true;
            }
        }
        return false;
    }

    bool features_all_of(std::vector<Feature> test_features) const {
        for (Feature f : test_features) {
            if (!has_feature(f)) {
                return false;
            }
        }
        return true;
    }

    /** Return a copy of the target with the given feature set.
     * This is convenient when enabling certain features (e.g. NoBoundsQuery)
     * in an initialization list, where the target to be mutated may be
     * a const reference. */
    Target with_feature(Feature f) const {
        Target copy = *this;
        copy.set_feature(f);
        return copy;
    }

    /** Return a copy of the target with the given feature cleared.
     * This is convenient when disabling certain features (e.g. NoBoundsQuery)
     * in an initialization list, where the target to be mutated may be
     * a const reference. */
    Target without_feature(Feature f) const {
        Target copy = *this;
        copy.set_feature(f, false);
        return copy;
    }

    /** Is a fully feature GPU compute runtime enabled? I.e. is
     * Func::gpu_tile and similar going to work? Currently includes
     * CUDA, OpenCL, and Metal. We do not include OpenGL, because it
     * is not capable of gpgpu, and is not scheduled via
     * Func::gpu_tile.
     * TODO: Should OpenGLCompute be included here? */
    bool has_gpu_feature() const {
      return has_feature(CUDA) || has_feature(OpenCL) || has_feature(Metal);
    }

    /** Does this target allow using a certain type. Generally all
     * types except 64-bit float and int/uint should be supported by
     * all backends.
     */
    bool supports_type(const Type &t) const {
        if (t.bits() == 64) {
            if (t.is_float()) {
                return !has_feature(Metal) &&
                       (!has_feature(Target::OpenCL) || has_feature(Target::CLDoubles));
            } else {
                return !has_feature(Metal);
            }
        }
        return true;
    }

    /** Returns whether a particular device API can be used with this
     * Target. */
    bool supports_device_api(DeviceAPI api) const;

    bool operator==(const Target &other) const {
      return os == other.os &&
          arch == other.arch &&
          bits == other.bits &&
          features == other.features;
    }

    bool operator!=(const Target &other) const {
      return !(*this == other);
    }

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
    EXPORT std::string to_string() const;

    /** Given a data type, return an estimate of the "natural" vector size
     * for that data type when compiling for this Target. */
    int natural_vector_size(Halide::Type t) const {
        user_assert(os != OSUnknown && arch != ArchUnknown && bits != 0)
            << "natural_vector_size cannot be used on a Target with Unknown values.\n";

        const bool is_integer = t.is_int() || t.is_uint();
        const int data_size = t.bytes();

        if (arch == Target::Hexagon) {
            if (is_integer) {
                // HVX is either 64 or 128 *byte* vector size.
                if (has_feature(Halide::Target::HVX_128)) {
                    return 128 / data_size;
                } else if (has_feature(Halide::Target::HVX_64)) {
                    return 64 / data_size;
                } else {
                    user_error << "Target uses hexagon arch without hvx_128 or hvx_64 set.\n";
                    return 0;
                }
            } else {
                // HVX does not have vector float instructions.
                return 1;
            }
        } else if (arch == Target::X86) {
            if (is_integer && (has_feature(Halide::Target::AVX512_Skylake) ||
                               has_feature(Halide::Target::AVX512_Cannonlake))) {
                // AVX512BW exists on Skylake and Cannonlake
                return 64 / data_size;
            } else if (t.is_float() && (has_feature(Halide::Target::AVX512) ||
                                        has_feature(Halide::Target::AVX512_KNL) ||
                                        has_feature(Halide::Target::AVX512_Skylake) ||
                                        has_feature(Halide::Target::AVX512_Cannonlake))) {
                // AVX512F is on all AVX512 architectures
                return 64 / data_size;
            } else if (has_feature(Halide::Target::AVX2)) {
                // AVX2 uses 256-bit vectors for everything.
                return 32 / data_size;
            } else if (!is_integer && has_feature(Halide::Target::AVX)) {
                // AVX 1 has 256-bit vectors for float, but not for
                // integer instructions.
                return 32 / data_size;
            } else {
                // SSE was all 128-bit. We ignore MMX.
                return 16 / data_size;
            }
        } else {
            // Assume 128-bit vectors on other targets.
            return 16 / data_size;
        }
    }

    /** Given a data type, return an estimate of the "natural" vector size
     * for that data type when compiling for this Target. */
    template <typename data_t>
    int natural_vector_size() const {
        return natural_vector_size(type_of<data_t>());
    }

    /** Return the maximum buffer size in bytes supported on this
     * Target. This is 2^31 - 1 except when the LargeBuffers feature
     * is enabled, which expands the maximum to 2^63 - 1. */
    int64_t maximum_buffer_size() const {
        if (bits == 64 && has_feature(LargeBuffers)) {
            return (((uint64_t)1) << 63) - 1;
        } else {
            return (((uint64_t)1) << 31) - 1;
        }
    }

    /** Was libHalide compiled with support for this target? */
    EXPORT bool supported() const;

private:
    /** A bitmask that stores the active features. */
    std::bitset<FeatureEnd> features;
};

/** Return the target corresponding to the host machine. */
EXPORT Target get_host_target();

/** Return the target that Halide will use. If HL_TARGET is set it
 * uses that. Otherwise calls \ref get_host_target */
EXPORT Target get_target_from_environment();

/** Return the target that Halide will use for jit-compilation. If
 * HL_JIT_TARGET is set it uses that. Otherwise calls \ref
 * get_host_target. Throws an error if the architecture, bit width,
 * and OS of the target do not match the host target, so this is only
 * useful for controlling the feature set. */
EXPORT Target get_jit_target_from_environment();

/** Get the Target feature corresponding to a DeviceAPI. For device
 * apis that do not correspond to any single target feature, returns
 * Target::FeatureEnd */
EXPORT Target::Feature target_feature_for_device_api(DeviceAPI api);

namespace Internal {

EXPORT void target_test();

}

}

#endif
