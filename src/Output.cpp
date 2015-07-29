#include "CodeGen_C.h"
#include "StmtToHtml.h"
#include "Output.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"

#include <fstream>

namespace Halide {

void compile_module_to_object(const Module &module, std::string filename) {
    if (filename.empty()) {
        if (module.target().os == Target::Windows) {
            filename = module.name() + ".obj";
        } else {
            filename = module.name() + ".o";
        }
    }

    llvm::LLVMContext context;
    llvm::Module *llvm = compile_module_to_llvm_module(module, context);
    compile_llvm_module_to_object(llvm, filename);
    delete llvm;
}

void compile_module_to_assembly(const Module &module, std::string filename)  {
    if (filename.empty()) filename = module.name() + ".s";

    llvm::LLVMContext context;
    llvm::Module *llvm = compile_module_to_llvm_module(module, context);
    compile_llvm_module_to_assembly(llvm, filename);
    delete llvm;
}

void compile_module_to_native(const Module &module,
                   std::string object_filename,
                   std::string assembly_filename) {
    if (object_filename.empty()) {
        if (module.target().os == Target::Windows) {
            object_filename = module.name() + ".obj";
        } else {
            object_filename = module.name() + ".o";
        }
    }
    if (assembly_filename.empty()) {
        assembly_filename = module.name() + ".s";
    }

    llvm::LLVMContext context;
    llvm::Module *llvm = compile_module_to_llvm_module(module, context);
    compile_llvm_module_to_object(llvm, object_filename);
    compile_llvm_module_to_assembly(llvm, assembly_filename);
    delete llvm;
}

void compile_module_to_llvm_bitcode(const Module &module, std::string filename)  {
    if (filename.empty()) filename = module.name() + ".bc";

    llvm::LLVMContext context;
    llvm::Module *llvm = compile_module_to_llvm_module(module, context);
    compile_llvm_module_to_llvm_bitcode(llvm, filename);
    delete llvm;
}

void compile_module_to_llvm_assembly(const Module &module, std::string filename)  {
    if (filename.empty()) filename = module.name() + ".ll";

    llvm::LLVMContext context;
    llvm::Module *llvm = compile_module_to_llvm_module(module, context);
    compile_llvm_module_to_llvm_assembly(llvm, filename);
    delete llvm;
}

void compile_module_to_llvm(const Module &module,
                 std::string bitcode_filename,
                 std::string llvm_assembly_filename)  {
    if (bitcode_filename.empty()) bitcode_filename = module.name() + ".bc";
    if (llvm_assembly_filename.empty()) llvm_assembly_filename = module.name() + ".ll";

    llvm::LLVMContext context;
    llvm::Module *llvm = compile_module_to_llvm_module(module, context);
    compile_llvm_module_to_llvm_bitcode(llvm, bitcode_filename);
    compile_llvm_module_to_llvm_assembly(llvm, llvm_assembly_filename);
    delete llvm;
}

void compile_module_to_html(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".html";

    Internal::print_to_html(filename, module);
}

void compile_module_to_text(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".stmt";

    std::ofstream file(filename.c_str());
    file << module;
}

void compile_module_to_c_header(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".h";

    std::ofstream file(filename.c_str());
    Internal::CodeGen_C cg(file, true, filename);
    cg.compile(module);
}

void compile_module_to_c_source(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".c";

    std::ofstream file(filename.c_str());
    Internal::CodeGen_C cg(file, false);
    cg.compile(module);
}

void compile_module_to_c(const Module &module,
              std::string h_filename,
              std::string c_filename) {
    compile_module_to_c_header(module, h_filename);
    compile_module_to_c_source(module, c_filename);
}

void compile_standalone_runtime(std::string object_filename, Target t) {
    Module empty("standalone_runtime", t.without_feature(Target::NoRuntime));
    compile_module_to_object(empty, object_filename);
}

}  // namespace Halide
