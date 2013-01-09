#ifndef HALIDE_STMT_COMPILER_H
#define HALIDE_STMT_COMPILER_H

/** \file
 * Defines a compiler that produces native code from halide statements 
 */

#include "IR.h"
#include "JITCompiledModule.h"

#include <string>
#include <vector>

namespace Halide {
namespace Internal {

/** A handle to a generic statement compiler. Can take Halide
 * statements and turn them into assembly, bitcode, machine code, or a
 * jit-compiled module. */
class CodeGen;
class StmtCompiler {
    IntrusivePtr<CodeGen> contents;
public:

    /** Build a code generator for the given architecture. Valid
     * architectures are x86, x86-avx. Architectures to come include
     * x86-avx2, arm, arm-android, and ptx. If you leave the
     * architecture field blank, it uses the environment variable
     * HL_TARGET. */
    StmtCompiler(std::string arch = "");

    /** Compile a statement to an llvm module of the given name with
     * the given toplevel arguments. The module is stored internally
     * until one of the later functions is called: */
    void compile(Stmt stmt, std::string name, const std::vector<Argument> &args);

    /** Write the module to an llvm bitcode file */
    void compile_to_bitcode(const std::string &filename);

    /** Compile and write the module to either a binary object file,
     * or as assembly */
    void compile_to_native(const std::string &filename, bool assembly = false);

    /** Return a function pointer with type given by the vector of
     * Arguments passed to compile. Also returns a wrapped version of
     * the function, which is a single-argument function that takes an
     * array of void * (i.e. a void **). Each entry in this array
     * either points to a buffer_t, or to a scalar of the type
     * specified by the argument list.
     *
     * Also returns various other useful functions within the module,
     * such as a hook for setting the function to call when an assert
     * fails.
     */
    JITCompiledModule compile_to_function_pointers();    
};

}
}



#endif
