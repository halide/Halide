#include "CodeGen_C.h"
#include "StmtToHtml.h"
#include "Output.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"

#include <fstream>

namespace Halide {

void output_object(const Module &module, const std::string &filename) {
    llvm::Module *llvm = output_llvm_module(module);
    output_object(llvm, filename);
    delete llvm;
}

void output_assembly(const Module &module, const std::string &filename)  {
    llvm::Module *llvm = output_llvm_module(module);
    output_assembly(llvm, filename);
    delete llvm;
}

void output_native(const Module &module,
                   const std::string &object_filename,
                   const std::string &assembly_filename) {
    llvm::Module *llvm = output_llvm_module(module);
    output_object(llvm, object_filename);
    output_assembly(llvm, assembly_filename);
    delete llvm;
}

void output_bitcode(const Module &module, const std::string &filename)  {
    llvm::Module *llvm = output_llvm_module(module);
    output_bitcode(llvm, filename);
    delete llvm;
}

void output_llvm_assembly(const Module &module, const std::string &filename)  {
    llvm::Module *llvm = output_llvm_module(module);
    output_llvm_assembly(llvm, filename);
    delete llvm;
}

void output_llvm(const Module &module,
                 const std::string &bitcode_filename,
                 const std::string &llvm_assembly_filename)  {
    llvm::Module *llvm = output_llvm_module(module);
    output_bitcode(llvm, bitcode_filename);
    output_llvm_assembly(llvm, llvm_assembly_filename);
    delete llvm;
}

void output_stmt_html(const Module &module, const std::string &filename) {
    Internal::print_to_html(filename, module.body);
}

void output_stmt_text(const Module &module, const std::string &filename) {
    std::ofstream file(filename.c_str());
    file << module.body;
}

void output_c_header(const Module &module, const std::string &filename) {
    std::ofstream file(filename.c_str());
    Internal::CodeGen_C cg(file, true, filename);
    cg.compile(module);
}

void output_c_source(const Module &module, const std::string &filename) {
    std::ofstream file(filename.c_str());
    Internal::CodeGen_C cg(file, false);
    cg.compile(module);
}

void output_c(const Module &module,
              const std::string &h_filename,
              const std::string &c_filename) {
    output_c_header(module, h_filename);
    output_c_source(module, c_filename);
}

}  // namespace Halide
