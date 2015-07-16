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

namespace Halide {

/** A struct representing a target machine and os to generate code for. */
struct Target {
    /** The operating system used by the target. Determines which
     * system calls to generate. */
    enum OS {OSUnknown = 0, Linux, Windows, OSX, Android, IOS, NaCl} os;

    /** The architecture used by the target. Determines the
     * instruction set to use. For the PNaCl target, the "instruction
     * set" is actually llvm bitcode. */
    enum Arch {ArchUnknown = 0, X86, ARM, PNaCl, MIPS} arch;

    /** The bit-width of the target machine. Must be 0 for unknown, or 32 or 64. */
    int bits;

    /** Optional features a target can have. */
    enum Feature {
        JIT,  ///< Generate code that will run immediately inside the calling process.
        Debug,  ///< Turn on debug info and output for runtime code.
        NoAsserts,  ///< Disable all runtime checks, for slightly tighter code.
        NoBoundsQuery, ///< Disable the bounds querying functionality.

        SSE41,  ///< Use SSE 4.1 and earlier instructions. Only relevant on x86.
        AVX,  ///< Use AVX 1 instructions. Only relevant on x86.
        AVX2,  ///< Use AVX 2 instructions. Only relevant on x86.
        FMA,  ///< Enable x86 FMA instruction
        FMA4,  ///< Enable x86 (AMD) FMA4 instruction set
        F16C,  ///< Enable x86 16-bit float support

        ARMv7s,  ///< Generate code for ARMv7s. Only relevant for 32-bit ARM.
        NoNEON,  ///< Avoid using NEON instructions. Only relevant for 32-bit ARM.

        CUDA,  ///< Enable the CUDA runtime. Defaults to compute capability 2.0 (Fermi)
        CUDACapability30,  ///< Enable CUDA compute capability 3.0 (Kepler)
        CUDACapability32,  ///< Enable CUDA compute capability 3.2 (Tegra K1)
        CUDACapability35,  ///< Enable CUDA compute capability 3.5 (Kepler)
        CUDACapability50,  ///< Enable CUDA compute capability 5.0 (Maxwell)

        OpenCL,  ///< Enable the OpenCL runtime.
        CLDoubles,  ///< Enable double support on OpenCL targets

        OpenGL,  ///< Enable the OpenGL runtime.
        OpenGLCompute, ///< Enable OpenGL Compute runtime.

        Renderscript, ///< Enable the Renderscript runtime.

        UserContext,  ///< Generated code takes a user_context pointer as first argument

        RegisterMetadata,  ///< Generated code registers metadata for use with halide_enumerate_registered_filters

        Matlab,  ///< Generate a mexFunction compatible with Matlab mex libraries. See tools/mex_halide.m.

        Profile, ///< Launch a sampling profiler alongside the Halide pipeline that monitors and reports the runtime used by each Func
        NoRuntime, ///< Do not include a copy of the Halide runtime in any generated object file or assembly

        FeatureEnd
        // NOTE: Changes to this enum must be reflected in the definition of
        // to_string()!
    };

    Target() : os(OSUnknown), arch(ArchUnknown), bits(0) {}
    Target(OS o, Arch a, int b, std::vector<Feature> initial_features = std::vector<Feature>())
        : os(o), arch(a), bits(b) {
        for (size_t i = 0; i < initial_features.size(); i++) {
            set_feature(initial_features[i]);
        }
    }

    void set_feature(Feature f, bool value = true) {
        user_assert(f < FeatureEnd) << "Invalid Target feature.\n";
        features.set(f, value);
    }

    void set_features(std::vector<Feature> features_to_set, bool value = true) {
        for (size_t i = 0; i < features_to_set.size(); i++) {
            set_feature(features_to_set[i], value);
        }
    }

    bool has_feature(Feature f) const {
        user_assert(f < FeatureEnd) << "Invalid Target feature.\n";
        return features[f];
    }

    bool features_any_of(std::vector<Feature> test_features) const {
        for (size_t i = 0; i < test_features.size(); i++) {
            user_assert(test_features[i] < FeatureEnd) << "Invalid Target feature.\n";

            if (features[test_features[i]]) {
                return true;
            }
        }
        return false;
    }

    bool features_all_of(std::vector<Feature> test_features) const {
        for (size_t i = 0; i < test_features.size(); i++) {
            user_assert(test_features[i] < FeatureEnd) << "Invalid Target feature.\n";

            if (!features[test_features[i]]) {
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

    /** Is OpenCL or CUDA enabled in this target? I.e. is
     * Func::gpu_tile and similar going to work? We do not include
     * OpenGL, because it is not capable of gpgpu, and is not
     * scheduled via Func::gpu_tile. */
    bool has_gpu_feature() const {
        return has_feature(CUDA) || has_feature(OpenCL);
    }

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
     * Note that is guaranteed that t2.from_string(t1.to_string()) == t1,
     * but not that from_string(s).to_string() == s (since there can be
     * multiple strings that parse to the same Target)...
     * *unless* t1 contains 'unknown' fields (in which case you'll get a string
     * that can't be parsed, which is intentional).
     */
    EXPORT std::string to_string() const;

    /**
     * Parse the contents of 'target' and merge into 'this',
     * replacing only the parts that are specified. (e.g., if 'target' specifies
     * only an arch, only the arch field of 'this' will be changed, leaving
     * the other fields untouched). Any features specified in 'target'
     * are added to 'this', whether or not originally present.
     *
     * If the string contains unknown tokens, or multiple tokens of the
     * same category (e.g. multiple arch values), return false
     * (possibly leaving 'this' munged). (Multiple feature specifications
     * will not cause a failure.)
     *
     * If 'target' contains "host" as the first token, it replaces the entire
     * contents of 'this' with get_host_target(), then proceeds to parse the
     * remaining tokens (allowing for things like "host-opencl" to mean
     * "host configuration, but with opencl added").
     *
     * Note that unlike parse_from_string(), this will never print to cerr or
     * assert in the event of a parse failure. Note also that an empty target
     * string is essentially a no-op, leaving 'this' unaffected.
     */
    EXPORT bool merge_string(const std::string &target);

    /**
     * Like merge_string(), but reset the contents of 'this' first.
     */
    EXPORT bool from_string(const std::string &target) {
        *this = Target();
        return merge_string(target);
    }

    /** Given a data type, return an estimate of the "natural" vector size
     * for that data type when compiling for this Target. */
    int natural_vector_size(Halide::Type t) const {
        const bool is_avx2 = has_feature(Halide::Target::AVX2);
        const bool is_avx = has_feature(Halide::Target::AVX) && !is_avx2;
        const bool is_integer = t.is_int() || t.is_uint();

        // AVX has 256-bit SIMD registers, other existing targets have 128-bit ones.
        // However, AVX has a very limited complement of integer instructions;
        // restricting us to SSE4.1 size for integer operations produces much
        // better performance. (AVX2 does have good integer operations for 256-bit
        // registers.)
        const int vector_byte_size = (is_avx2 || (is_avx && !is_integer)) ? 32 : 16;
        const int data_size = t.bits / 8;
        return vector_byte_size / data_size;
    }

    /** Given a data type, return an estimate of the "natural" vector size
     * for that data type when compiling for this Target. */
    template <typename data_t>
    int natural_vector_size() const {
        return natural_vector_size(type_of<data_t>());
    }

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

/** Given a string of the form used in HL_TARGET (e.g. "x86-64-avx"),
 * return the Target it specifies. Note that this always starts with
 * the result of get_host_target(), replacing only the parts found in the
 * target string, so if you omit (say) an OS specification, the host OS
 * will be used instead. An empty string is exactly equivalent to get_host_target().
 */
EXPORT Target parse_target_string(const std::string &target);

}

#endif
