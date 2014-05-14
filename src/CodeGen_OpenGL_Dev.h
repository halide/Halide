#ifndef HALIDE_CODEGEN_OPENGL_DEV_H
#define HALIDE_CODEGEN_OPENGL_DEV_H

/** \file
 * Defines the code-generator for producing GLSL kernel code
 */

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"

#include <sstream>

namespace Halide {
namespace Internal {

class CodeGen_GLSL;

class CodeGen_OpenGL_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_OpenGL_Dev();
    ~CodeGen_OpenGL_Dev();

    // CodeGen_GPU_Dev interface
    void add_kernel(Stmt stmt, std::string name, const std::vector<Argument> &args);
    void init_module();
    std::vector<char> compile_to_src();
    std::string get_current_kernel_name();
    void dump();

private:
    CodeGen_GLSL *glc;
    std::ostringstream src_stream;
    std::string cur_kernel_name;
};


/** Compile one statement into GLSL. */
class CodeGen_GLSL : public CodeGen_C {
public:
    CodeGen_GLSL(std::ostream &s) : CodeGen_C(s) {}
    void compile(Stmt stmt, std::string name, const std::vector<Argument> &args);

protected:
    using CodeGen_C::visit;
    std::string print_type(Type type);

    void visit(const FloatImm *op);

    void visit(const Cast *);
    void visit(const For *);

    void visit(const Max *);
    void visit(const Min *);
    void visit(const Load *);
    void visit(const Store *);

    void visit(const Call *);
    void visit(const AssertStmt *);
    void visit(const Broadcast *);

    void visit(const Evaluate *op);

private:
    std::string get_vector_suffix(Expr e);

};

}}

#endif
