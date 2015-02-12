#include "CodeGen_C.h"
#include "StmtToHtml.h"
#include "Output.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"

#include <fstream>

namespace Halide {

void output_object(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".o";

    llvm::LLVMContext context;
    llvm::Module *llvm = output_llvm_module(module, context);
    output_object(llvm, filename);
    delete llvm;
}

void output_assembly(const Module &module, std::string filename)  {
    if (filename.empty()) filename = module.name() + ".s";

    llvm::LLVMContext context;
    llvm::Module *llvm = output_llvm_module(module, context);
    output_assembly(llvm, filename);
    delete llvm;
}

void output_native(const Module &module,
                   std::string object_filename,
                   std::string assembly_filename) {
    if (object_filename.empty()) object_filename = module.name() + ".o";
    if (assembly_filename.empty()) assembly_filename = module.name() + ".s";

    llvm::LLVMContext context;
    llvm::Module *llvm = output_llvm_module(module, context);
    output_object(llvm, object_filename);
    output_assembly(llvm, assembly_filename);
    delete llvm;
}

void output_bitcode(const Module &module, std::string filename)  {
    if (filename.empty()) filename = module.name() + ".bc";

    llvm::LLVMContext context;
    llvm::Module *llvm = output_llvm_module(module, context);
    output_bitcode(llvm, filename);
    delete llvm;
}

void output_llvm_assembly(const Module &module, std::string filename)  {
    if (filename.empty()) filename = module.name() + ".ll";

    llvm::LLVMContext context;
    llvm::Module *llvm = output_llvm_module(module, context);
    output_llvm_assembly(llvm, filename);
    delete llvm;
}

void output_llvm(const Module &module,
                 std::string bitcode_filename,
                 std::string llvm_assembly_filename)  {
    if (bitcode_filename.empty()) bitcode_filename = module.name() + ".bc";
    if (llvm_assembly_filename.empty()) llvm_assembly_filename = module.name() + ".ll";

    llvm::LLVMContext context;
    llvm::Module *llvm = output_llvm_module(module, context);
    output_bitcode(llvm, bitcode_filename);
    output_llvm_assembly(llvm, llvm_assembly_filename);
    delete llvm;
}

void output_html(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".html";

    Internal::print_to_html(filename, module);
}

void output_text(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".stmt";

    std::ofstream file(filename.c_str());
    file << module;
}

void output_c_header(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".h";

    std::ofstream file(filename.c_str());
    Internal::CodeGen_C cg(file, true, filename);
    cg.compile(module);
}

void output_c_source(const Module &module, std::string filename) {
    if (filename.empty()) filename = module.name() + ".c";

    std::ofstream file(filename.c_str());
    Internal::CodeGen_C cg(file, false);
    cg.compile(module);
}

void output_c(const Module &module,
              std::string h_filename,
              std::string c_filename) {
    output_c_header(module, h_filename);
    output_c_source(module, c_filename);
}

}  // namespace Halide
