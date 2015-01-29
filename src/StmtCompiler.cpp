#include "StmtCompiler.h"
#include "CodeGen.h"
#include "CodeGen_X86.h"
#include "CodeGen_GPU_Host.h"
#include "CodeGen_ARM.h"
#include "CodeGen_MIPS.h"
#include "CodeGen_PNaCl.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

StmtCompiler::StmtCompiler(Target target) {
    if (target.os == Target::OSUnknown) {
        target = get_host_target();
    }

    // The awkward mapping from targets to code generators
    if (target.features_any_of(vec(Target::CUDA,
                                   Target::OpenCL,
                                   Target::OpenGL))) {
#ifdef WITH_X86
        if (target.arch == Target::X86) {
            contents = new CodeGen_GPU_Host<CodeGen_X86>(target);
        }
#endif
#if defined(WITH_ARM) || defined(WITH_AARCH64)
        if (target.arch == Target::ARM) {
            contents = new CodeGen_GPU_Host<CodeGen_ARM>(target);
        }
#endif
#ifdef WITH_MIPS
        if (target.arch == Target::MIPS) {
            contents = new CodeGen_GPU_Host<CodeGen_MIPS>(target);
        }
#endif
#ifdef WITH_NATIVE_CLIENT
        if (target.arch == Target::PNaCl) {
            contents = new CodeGen_GPU_Host<CodeGen_PNaCl>(target);
        }
#endif
        if (!contents.defined()) {
            user_error << "Invalid target architecture for GPU backend: "
                       << target.to_string() << "\n";
        }
    } else if (target.arch == Target::X86) {
        contents = new CodeGen_X86(target);
    } else if (target.arch == Target::ARM) {
        contents = new CodeGen_ARM(target);
    } else if (target.arch == Target::MIPS) {
        contents = new CodeGen_MIPS(target);
    } else if (target.arch == Target::PNaCl) {
        contents = new CodeGen_PNaCl(target);
    }
}

void StmtCompiler::compile(Stmt stmt, string name,
                           const vector<Argument> &args,
                           const vector<Buffer> &images_to_embed) {
    contents.ptr->compile(stmt, name, args, images_to_embed);
}

void StmtCompiler::compile_to_bitcode(const string &filename) {
    contents.ptr->compile_to_bitcode(filename);
}

void StmtCompiler::compile_to_native(const string &filename, bool assembly) {
    contents.ptr->compile_to_native(filename, assembly);
}

JITModule StmtCompiler::compile_to_function_pointers() {
    return contents.ptr->compile_to_function_pointers();
}

}
}
