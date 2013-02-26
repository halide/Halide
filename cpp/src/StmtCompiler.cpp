#include "StmtCompiler.h"
#include "CodeGen.h"
#include "CodeGen_X86.h"
#include "CodeGen_ARM.h"
#include <iostream>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

StmtCompiler::StmtCompiler(string arch) {
    if (arch.empty()) {
        char *target = getenv("HL_TARGET");
        if (target) arch = target;        
        else arch = "x86"; // default to x86 for now. In the future
                           // this should detect the native target.
    }
    if (arch == "x86") {
        contents = new CodeGen_X86(true, false);
    } else if (arch == "x86-avx") {
        contents = new CodeGen_X86(true, true);
    } else if (arch == "arm") {
        contents = new CodeGen_ARM(false);
    } else if (arch == "arm-android") {
        contents = new CodeGen_ARM(true);
    } else {
        std::cerr << "Unknown target " << arch << std::endl;
        std::cerr << "Known targets are: x86 x86-avx arm arm-android" << std::endl;
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
