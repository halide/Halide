#ifndef HALIDE_TARGET_H
#define HALIDE_TARGET_H

#include <stdint.h>

namespace llvm {
class Module;
class LLVMContext;
}

namespace Halide {

struct Target {
    enum OS {OSUnknown = 0, Linux, Windows, OSX, Android, IOS, NaCl} os;
    enum Arch {ArchUnknown = 0, X86, ARM} arch;
    int bits; // Must be 0 for unknown, or 32 or 64
    enum Features {SSE41 = 1, AVX = 2, AVX2 = 4, CUDA = 8, OpenCL = 16, GPUDebug = 32};
    uint64_t features;

    Target() : os(OSUnknown), arch(ArchUnknown), bits(0), features(0) {}
    Target(OS o, Arch a, int b, uint64_t f) : os(o), arch(a), bits(b), features(f) {}
};

/** Return the target corresponding to the host machine. */
Target get_host_target();

/** Return the target that Halide will use. If HL_TARGET is set it
 * uses that. Otherwise calls \ref get_native_target */
Target get_target_from_environment();

/** Create an llvm module containing the support code for a given target. */
llvm::Module *get_initial_module_for_target(Target, llvm::LLVMContext *);

}


#endif
