#ifndef HALIDE_CODEGEN_OPENGL_DEV_H
#define HALIDE_CODEGEN_OPENGL_DEV_H

/** \file
 * Defines the code-generator for producing GLSL kernel code
 */

#include <sstream>
#include <map>

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class CodeGen_GLSL;

class CodeGen_OpenGL_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_OpenGL_Dev(const Target &target);
    ~CodeGen_OpenGL_Dev();

    // CodeGen_GPU_Dev interface
    void add_kernel(Stmt stmt, const std::string &name,
                    const std::vector<DeviceArgument> &args);

    void init_module();

    std::vector<char> compile_to_src();

    std::string get_current_kernel_name();

    void dump();

    std::string api_unique_name() { return "opengl"; }

private:
    CodeGen_GLSL *glc;

    virtual std::string print_gpu_name(const std::string &name);

private:
    std::ostringstream src_stream;
    std::string cur_kernel_name;
    Target target;
};

/**
  * This class handles GLSL arithmetic, shared by CodeGen_GLSL and CodeGen_OpenGLCompute_C.
  */
class CodeGen_GLSLBase : public CodeGen_C {
public:
    CodeGen_GLSLBase(std::ostream &s, Target t);

    std::string print_name(const std::string &name);
    std::string print_type(Type type, AppendSpaceIfNeeded space_option = DoNotAppendSpace);

protected:
    using CodeGen_C::visit;
    void visit(const Max *op);
    void visit(const Min *op);
    void visit(const Div *op);
    void visit(const Mod *op);
    void visit(const Call *op);

    // these have specific functions
    // in GLSL that operate on vectors
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);

    void visit(const Shuffle *);

private:
    std::map<std::string, std::string> builtin;
};


/** Compile one statement into GLSL. */
class CodeGen_GLSL : public CodeGen_GLSLBase {
public:
    CodeGen_GLSL(std::ostream &s, const Target &t) : CodeGen_GLSLBase(s, t) {}

    void add_kernel(Stmt stmt,
                    std::string name,
                    const std::vector<DeviceArgument> &args);

    EXPORT static void test();

protected:
    using CodeGen_C::visit;

    void visit(const FloatImm *);
    void visit(const UIntImm *);
    void visit(const IntImm *);

    void visit(const Cast *);
    void visit(const Let *);
    void visit(const For *);
    void visit(const Select *);

    void visit(const Load *);
    void visit(const Store *);
    void visit(const Allocate *);
    void visit(const Free *);

    void visit(const Call *);
    void visit(const AssertStmt *);
    void visit(const Ramp *op);
    void visit(const Broadcast *);

    void visit(const Evaluate *);

private:
    std::string get_vector_suffix(Expr e);

    std::vector<std::string> print_lanes(Expr expr);

    Scope<int> scalar_vars, vector_vars;
};

}}

#endif
