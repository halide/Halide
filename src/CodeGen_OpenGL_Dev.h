#ifndef HALIDE_CODEGEN_OPENGL_DEV_H
#define HALIDE_CODEGEN_OPENGL_DEV_H

/** \file
 * Defines the code-generator for producing GLSL kernel code
 */

#include <map>
#include <sstream>

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class CodeGen_GLSL;

class CodeGen_OpenGL_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_OpenGL_Dev(const Target &target);
    ~CodeGen_OpenGL_Dev() override;

    // CodeGen_GPU_Dev interface
    void add_kernel(Stmt stmt, const std::string &name,
                    const std::vector<DeviceArgument> &args) override;

    void init_module() override;

    std::vector<char> compile_to_src() override;

    std::string get_current_kernel_name() override;

    void dump() override;

    std::string api_unique_name() override {
        return "opengl";
    }

private:
    CodeGen_GLSL *glc;

    std::string print_gpu_name(const std::string &name) override;

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

    std::string print_name(const std::string &name) override;
    std::string print_type(Type type, AppendSpaceIfNeeded space_option = DoNotAppendSpace) override;

protected:
    using CodeGen_C::visit;
    void visit(const FloatImm *) override;
    void visit(const UIntImm *) override;
    void visit(const IntImm *) override;

    void visit(const Max *op) override;
    void visit(const Min *op) override;
    void visit(const Div *op) override;
    void visit(const Mod *op) override;
    void visit(const Call *op) override;

    // these have specific functions
    // in GLSL that operate on vectors
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;

    void visit(const Shuffle *) override;

    std::map<std::string, std::string> builtin;
};

/** Compile one statement into GLSL. */
class CodeGen_GLSL : public CodeGen_GLSLBase {
public:
    CodeGen_GLSL(std::ostream &s, const Target &t);

    void add_kernel(Stmt stmt,
                    std::string name,
                    const std::vector<DeviceArgument> &args);

    static void test();

protected:
    using CodeGen_C::visit;

    void visit(const Cast *) override;
    void visit(const Let *) override;
    void visit(const For *) override;
    void visit(const Select *) override;

    void visit(const Load *) override;
    void visit(const Store *) override;
    void visit(const Allocate *) override;
    void visit(const Free *) override;

    void visit(const Call *) override;
    void visit(const AssertStmt *) override;
    void visit(const Ramp *op) override;
    void visit(const Broadcast *) override;

    void visit(const Evaluate *) override;
    void visit(const Atomic *) override;

private:
    std::string get_vector_suffix(Expr e);

    std::vector<std::string> print_lanes(Expr expr);

    Scope<int> scalar_vars, vector_vars;
};

}  // namespace Internal
}  // namespace Halide

#endif
