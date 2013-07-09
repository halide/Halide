#include "StmtCompiler.h"
#include "CodeGen.h"
#include "CodeGen_X86.h"
#include "CodeGen_GPU_Host.h"
#include "CodeGen_ARM.h"
#include <iostream>
#ifdef _WIN32
#include <intrin.h>
#endif	// _WIN32

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

#ifndef __arm__
#ifdef _WIN32

#define cpuid __cpuid
#define cpuid_count __cpuidex

#else
// CPU feature detection code taken from ispc
// (https://github.com/ispc/ispc/blob/master/builtins/dispatch.ll)
static void cpuid(int info[4], int infoType) {
    __asm__ __volatile__ (
        "xchg{l}\t{%%}ebx, %1  \n\t"
        "cpuid                 \n\t"
        "xchg{l}\t{%%}ebx, %1  \n\t"
        : "=a" (info[0]), "=r" (info[1]), "=c" (info[2]), "=d" (info[3])
        : "0" (infoType));
}

/* Save %ebx in case it's the PIC register */
static void cpuid_count(int info[4], int level, int count) {
    __asm__ __volatile__ (
        "xchg{l}\t{%%}ebx, %1  \n\t"
        "cpuid                 \n\t"
        "xchg{l}\t{%%}ebx, %1  \n\t"
        : "=a" (info[0]), "=r" (info[1]), "=c" (info[2]), "=d" (info[3])
        : "0" (level), "2" (count));
}
#endif

string get_native_x86_target() {
     int info[4];
     cpuid(info, 1);

     bool use_64_bits = (sizeof(size_t) == 8);
     bool have_sse41 = info[2] & (1 << 19);
     bool have_sse2 = info[3] & (1 << 26);
     bool have_avx = info[2] & (1 << 28);
     bool have_f16 = info[2] & (1 << 29);
     bool have_rdrand = info[2] & (1 << 30);

     /* NOTE: the values returned below must be the same as the
        corresponding enumerant values in Target::ISA. */
     if (use_64_bits) {
         if (have_avx) {
             if (have_f16 && have_rdrand) {
                 // So far, so good.  AVX2?
                 // Call cpuid with eax=7, ecx=0
                 int info2[4];
                 cpuid_count(info2, 7, 0);
                 bool have_avx2 = info[2] & (1 << 5);
                 if (have_avx2) {
                     // AVX 2
                     // For now we just return avx.
                     return "x86-64-avx";
                 } else {
                     // AVX with float16 and rdrand
                     // For now we just return avx.
                     return "x86-64-avx";
                 }
             }
             // Regular AVX
             return "x86-64-avx";
         } else if (have_sse41) {
             // SSE4.1
             return "x86-64-sse41";
         } else if (have_sse2) {
             // SSE2
             return "x86-64";
         } else {

         }
     } else {
         // 32-bit targets
         if ((info[2] & (1 << 19)) != 0) {
             // SSE4.1
             return "x86-32-sse41";
         } else if ((info[3] & (1 << 26)) != 0) {
             // SSE2
             return "x86-32";
         }
     }
     std::cerr << "cpuid instruction returned " << std::hex
               << info[0] << ", "
               << info[1] << ", "
               << info[2] << ", "
               << info[3] << "\n";
     assert(false && "No SSE2 support, or failed to correctly interpret the result of cpuid.");
}
#endif

StmtCompiler::StmtCompiler(string arch) {
    #ifdef __arm__
    const char *native = "arm";
    #else
    string native = get_native_x86_target();
    #endif
    if (arch.empty()) {
        #ifdef _WIN32
        char target[128];
        size_t read = 0;
        getenv_s(&read, target, "HL_TARGET");
        if (read) arch = target;
        else arch = native;
        #else
        char *target = getenv("HL_TARGET");
        if (target) arch = target;
        else arch = native;
        #endif
    }

    if (arch.empty() || arch == "native") {
        arch = native;
    }

    if (arch == "x86-32") {
        contents = new CodeGen_X86();
    } else if (arch == "x86-32-sse41") {
        contents = new CodeGen_X86(X86_SSE41);
    } else if (arch == "x86-64") {
        // Lowest-common-denominator for 64-bit x86, including those
        // without SSE4.1 (e.g. Clovertown) or SSSE3 (e.g. early AMD parts)...
        // essentially, just SSE2.
        contents = new CodeGen_X86(X86_64Bit);
    } else if (arch == "x86-64-sse41") {
        contents = new CodeGen_X86(X86_64Bit | X86_SSE41);
    } else if (arch == "x86-64-avx") {
        contents = new CodeGen_X86(X86_64Bit | X86_SSE41 | X86_AVX);
    } else if (arch == "x86-32-nacl") {
        contents = new CodeGen_X86(X86_NaCl);
    } else if (arch == "x86-32-sse41-nacl") {
        contents = new CodeGen_X86(X86_SSE41 | X86_NaCl);
    } else if (arch == "x86-64-nacl") {
        contents = new CodeGen_X86(X86_64Bit | X86_NaCl);
    } else if (arch == "x86-64-sse41-nacl") {
        contents = new CodeGen_X86(X86_64Bit | X86_SSE41 | X86_NaCl);
    } else if (arch == "x86-64-avx-nacl") {
        contents = new CodeGen_X86(X86_64Bit | X86_SSE41 | X86_AVX | X86_NaCl);
    }

#ifndef _WIN32 // I've temporarily disabled ARM on Windows since it leads to a linking error on halide_internal_initmod_arm stuff (kwampler@adobe.com)
    else if (arch == "arm") {
        contents = new CodeGen_ARM();
    } else if (arch == "arm-android") {
        contents = new CodeGen_ARM(ARM_Android);
    } else if (arch == "arm-nacl") {
        contents = new CodeGen_ARM(ARM_NaCl);
    }

    // GPU backends are disabled on Windows until I'm sure it links, too (@jrk)
    else if (arch == "ptx") {
        // equivalent to "x86" on the host side, i.e. x86_64, no AVX
        contents = new CodeGen_GPU_Host(X86_64Bit | X86_SSE41 | GPU_PTX);
    } else if (arch == "opencl") {
        // equivalent to "x86" on the host side, i.e. x86_64, no AVX
        contents = new CodeGen_GPU_Host(X86_64Bit | X86_SSE41 | GPU_OpenCL);
    }
#endif // _WIN32
    else {
        std::cerr << "Unknown target \"" << arch << "\"\n"
                  << "Known targets are: "
                  << "x86-32 x86-32-sse41 "
                  << "x86-64 x86-64-sse41 x86-64-avx "
                  << "x86-32-nacl x86-32-sse41-nacl "
                  << "x86-64-nacl x86-64-sse41-nacl x86-64-avx-nacl "
                  << "arm arm-android "
                  << "ptx opencl "
                  << "native"
		  << "\n"
                  << "On this machine, native means " << native << "\n";

        assert(false);
    }
}

void StmtCompiler::compile(Stmt stmt, string name, const vector<Argument> &args) {
    contents.ptr->compile(stmt, name, args);
}

void StmtCompiler::compile_to_bitcode(const string &filename) {
    contents.ptr->compile_to_bitcode(filename);
}

void StmtCompiler::compile_to_native(const string &filename, bool assembly) {
    contents.ptr->compile_to_native(filename, assembly);
}

JITCompiledModule StmtCompiler::compile_to_function_pointers() {
    return contents.ptr->compile_to_function_pointers();
}

}
}
