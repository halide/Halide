#ifndef HALIDE_TARGET_H
#define HALIDE_TARGET_H

#include <stdint.h>
#include <string>
#include "Util.h"

namespace llvm {
class Module;
class LLVMContext;
}

namespace Halide {

struct Target {
    enum OS {OSUnknown = 0, Linux, Windows, OSX, Android, IOS, NaCl} os;
    enum Arch {ArchUnknown = 0, X86, ARM} arch;
    int bits; // Must be 0 for unknown, or 32 or 64
    enum Features {JIT = 1, SSE41 = 2, AVX = 4, AVX2 = 8, CUDA = 16, OpenCL = 32, GPUDebug = 64, SPIR = 128, SPIR64 = 256};
    uint64_t features;

    Target() : os(OSUnknown), arch(ArchUnknown), bits(0), features(0) {}
    Target(OS o, Arch a, int b, uint64_t f) : os(o), arch(a), bits(b), features(f) {}
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
