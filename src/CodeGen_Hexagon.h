#ifndef HALIDE_CODEGEN_HEXAGON_H
#define HALIDE_CODEGEN_HEXAGON_H
/* A lot of code here has been taken from CodeGen_ARM.h */
/** \file
 * Defines the code-generator for producing ARM machine code
 */

#include "CodeGen_Posix.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A code generator that emits ARM code from a given Halide stmt. */
class CodeGen_Hexagon : public CodeGen_Posix {
public:
    /** Create an ARM code generator for the given arm target. */
    CodeGen_Hexagon(Target);

    /** Compile to an internally-held llvm module. Takes a halide
     * statement, the name of the function produced, and the arguments
     * to the function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the Hexagon machine
     * code. */
    void compile(Stmt stmt, std::string name,
                 const std::vector<Argument> &args,
                 const std::vector<Buffer> &images_to_embed);

    static void test();

protected:

    llvm::Triple get_target_triple() const;

    /* /\** Generate a call to a neon intrinsic *\/ */
    /* // @{ */
    /* llvm::Value *call_intrin(Type t, const std::string &name, std::vector<Expr>); */
    /* llvm::Value *call_intrin(llvm::Type *t, const std::string &name, std::vector<llvm::Value *>); */
    /* llvm::Instruction *call_void_intrin(const std::string &name, std::vector<Expr>); */
    /* llvm::Instruction *call_void_intrin(const std::string &name, std::vector<llvm::Value *>); */
    /* // @} */

    using CodeGen_Posix::visit;

    /* /\** Nodes for which we want to emit specific neon intrinsics *\/ */
    /* // @{ */
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Broadcast *);
    void visit(const Div *);
    /* // @} */


    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
};

}}

#endif
