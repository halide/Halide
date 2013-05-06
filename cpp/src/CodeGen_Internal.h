#ifndef HALIDE_CODEGEN_INTERNAL_H
#define HALIDE_CODEGEN_INTERNAL_H

#include "CodeGen.h"
#include "IRVisitor.h"
#include "LLVM_Headers.h"

namespace Halide { 
namespace Internal {

/** A helper class to manage closures - used for parallel for loops */
class CodeGen::Closure : public IRVisitor {
protected:
    Scope<int> ignore;

    using IRVisitor::visit;

    void visit(const Let *op);
    void visit(const LetStmt *op);
    void visit(const For *op);
    void visit(const Load *op);
    void visit(const Store *op);
    void visit(const Allocate *op);
    void visit(const Variable *op);

public:
    Closure(Stmt s, const std::string &loop_variable);
    std::map<std::string, Type> vars, reads, writes;

    llvm::StructType *build_type(CodeGen *gen);
    void pack_struct(CodeGen *gen, llvm::Value *dst, const Scope<llvm::Value *> &src, llvm::IRBuilder<> *builder);
    void unpack_struct(CodeGen *gen, Scope<llvm::Value *> &dst, llvm::Value *src, llvm::IRBuilder<> *builder, llvm::Module *module, llvm::LLVMContext &context);
    std::vector<llvm::Type*> llvm_types(CodeGen *gen);
    std::vector<std::string> names();
};

}}

#endif
