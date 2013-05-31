#include "StmtCompiler.h"
#include "CodeGen.h"
#include "CodeGen_X86.h"
#include "CodeGen_GPU_Host.h"
#include "CodeGen_ARM.h"
#include <iostream>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

StmtCompiler::StmtCompiler(string arch) {
    #ifdef __arm__
    const char *native = "arm";
    #else
    const char *native = "x86";
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

    if (arch == "x86") {
        contents = new CodeGen_X86(X86_64Bit | X86_SSE41);
    } else if (arch == "x86-32") {
        contents = new CodeGen_X86();
    } else if (arch == "x86-avx") {
        contents = new CodeGen_X86(X86_64Bit | X86_SSE41 | X86_AVX);
    } else if (arch == "x86-nacl") {
        contents = new CodeGen_X86(X86_64Bit | X86_SSE41 | X86_NaCl);
    } else if (arch == "x86-32-nacl") {
        contents = new CodeGen_X86(X86_NaCl);
    } else if (arch == "x86-32-sse41-nacl") {
        contents = new CodeGen_X86(X86_SSE41 | X86_NaCl);
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
    }
#endif // _WIN32
    else {
        std::cerr << "Unknown target " << arch << '\n';
        std::cerr << "Known targets are: "
                  << "x86 x86-avx x86-32 arm arm-android " 
                  << "x86-nacl x86-32-nacl x86-32-sse41-nacl arm-nacl "
                  << "ptx"
		  << std::endl;
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
