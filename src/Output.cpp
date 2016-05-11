#include "CodeGen_C.h"
#include "StmtToHtml.h"
#include "Output.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"

#include <fstream>

namespace Halide {

void compile_module_to_outputs(const Module &module, const Outputs &output_files) {
    if (!output_files.object_name.empty() || !output_files.assembly_name.empty() ||
        !output_files.bitcode_name.empty() || !output_files.llvm_assembly_name.empty()) {
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(module, context));

        if (!output_files.object_name.empty()) {
            if (module.target().arch == Target::PNaCl) {
                compile_llvm_module_to_llvm_bitcode(*llvm_module, output_files.object_name);
            } else {
                compile_llvm_module_to_object(*llvm_module, output_files.object_name);
            }
        }
        if (!output_files.assembly_name.empty()) {
            if (module.target().arch == Target::PNaCl) {
                compile_llvm_module_to_llvm_assembly(*llvm_module, output_files.assembly_name);
            } else {
                compile_llvm_module_to_assembly(*llvm_module, output_files.assembly_name);
            }
        }
        if (!output_files.bitcode_name.empty()) {
            compile_llvm_module_to_llvm_bitcode(*llvm_module, output_files.bitcode_name);
        }
        if (!output_files.llvm_assembly_name.empty()) {
            compile_llvm_module_to_llvm_assembly(*llvm_module, output_files.llvm_assembly_name);
        }
    }
    if (!output_files.c_header_name.empty()) {
        std::ofstream file(output_files.c_header_name.c_str());
        Internal::CodeGen_C cg(file,
                               module.target().has_feature(Target::CPlusPlusMangling) ?
                               Internal::CodeGen_C::CPlusPlusHeader : Internal::CodeGen_C::CHeader,
                               output_files.c_header_name);
        cg.compile(module);
    }
    if (!output_files.c_source_name.empty()) {
        std::ofstream file(output_files.c_source_name.c_str());
        Internal::CodeGen_C cg(file,
                               module.target().has_feature(Target::CPlusPlusMangling) ?
                               Internal::CodeGen_C::CPlusPlusImplementation : Internal::CodeGen_C::CImplementation);
        cg.compile(module);
    }
    if (!output_files.stmt_name.empty()) {
        std::ofstream file(output_files.stmt_name.c_str());
        file << module;
    }
    if (!output_files.stmt_html_name.empty()) {
        Internal::print_to_html(output_files.stmt_html_name, module);
    }
}

void compile_standalone_runtime(std::string object_filename, Target t) {
    Module empty("standalone_runtime", t.without_feature(Target::NoRuntime).without_feature(Target::JIT));
    compile_module_to_outputs(empty, Outputs().object(object_filename));
}

}  // namespace Halide
