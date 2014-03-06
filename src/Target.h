#ifndef HALIDE_TARGET_H
#define HALIDE_TARGET_H

/** \file
 * Defines the structure that describes a Halide target.
 */

#include <stdint.h>
#include <string>
#include "Util.h"

namespace llvm {
class Module;
class LLVMContext;
}

namespace Halide {

/** A struct representing a target machine and os to generate code for. */
struct Target {
    /** The operating system used by the target. Determines which
     * system calls to generate. */
    enum OS {OSUnknown = 0, Linux, Windows, OSX, Android, IOS, NaCl} os;

    /** The architecture used by the target. Determines the
     * instruction set to use. For the PNaCl target, the "instruction
     * set" is actually llvm bitcode. */
    enum Arch {ArchUnknown = 0, X86, ARM, PNaCl} arch;

    /** The bit-width of the target machine. Must be 0 for unknown, or 32 or 64. */
    int bits;

    /** Optional features a target can have. */
    enum Features {JIT = 1,       /// Generate code that will run immediately inside the calling process.
                   SSE41 = 2,     /// Use SSE 4.1 and earlier instructions. Only relevant on x86.
                   AVX = 4,       /// Use AVX 1 instructions. Only relevant on x86.
                   AVX2 = 8,      /// Use AVX 2 instructions. Only relevant on x86.
                   CUDA = 16,     /// Enable the CUDA runtime.
                   OpenCL = 32,   /// Enable the OpenCL runtime.
                   GPUDebug = 64, /// Increase the level of checking and the verbosity of the gpu runtimes.
                   SPIR = 128,    /// Enable the OpenCL SPIR runtime in 32-bit mode
                   SPIR64 = 256   /// Enable the OpenCL SPIR runtime in 64-bit mode
    };

    /** A bitmask that stores the active features. */
    uint64_t features;

    Target() : os(OSUnknown), arch(ArchUnknown), bits(0), features(0) {}
    Target(OS o, Arch a, int b, uint64_t f) : os(o), arch(a), bits(b), features(f) {}

    bool has_gpu_feature() {
        return (features & (CUDA|OpenCL|SPIR|SPIR64));
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
 * return the Target it specifies. */
EXPORT Target parse_target_string(const std::string &target);

namespace Internal {

/** Create an llvm module containing the support code for a given target. */
llvm::Module *get_initial_module_for_target(Target, llvm::LLVMContext *);

/** Create an llvm module containing the support code for ptx device. */
llvm::Module *get_initial_module_for_ptx_device(llvm::LLVMContext *c);

/** Create an llvm module containing the support code for SPIR device code. */
llvm::Module *get_initial_module_for_spir_device(llvm::LLVMContext *c, int bits);

}

}


#endif
