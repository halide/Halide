#ifndef HALIDE_CODEGEN_OPENGL_DEV_H
#define HALIDE_CODEGEN_OPENGL_DEV_H

/** \file
 * Defines the code-generator for producing GLSL kernel code
 */

#include <map>
#include <set>
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

    void visit(const Cast *) override;

    void visit(const FloatImm *) override;
    void visit(const UIntImm *) override;
    void visit(const IntImm *) override;

    void visit(const Max *op) override;
    void visit(const Min *op) override;
    void visit(const Call *op) override;

    void visit(const Mod *) override;

    // these have specific functions
    // in GLSL that operate on vectors
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;

    void visit(const Shuffle *) override;

    Type map_type(const Type &);

    std::map<std::string, std::string> builtin;

    // empty for GL 3.x and GLCompute which do not care about this (due to implicit conversion)
    // while GL 2.0 only support a small subset of builtin functions with ivec arguments
    std::set<std::string> support_non_float_type_builtin;

    // true for GL 3.x (GLSL >= 130 or ESSL >= 300) and GLCompute
    // false for GL 2.x which does not support uint/uvec
    bool support_native_uint = true;

    // true for GL 2.1 and 3.x (GLSL == 120, >= 130) and GLCompute
    // true for GL ES 3.1 with EXT_shader_implicit_conversions
    // false for GL 2.0 and GL ES 3.0
    bool support_int_to_float_implicit_conversion = true;

    // it seems that only GLSL ES implicitly does not support rounding of integer division
    // while GLSL specification does not talk about this issue
    // see GLSL ES Specification 1.00, issues 10.28, Rounding of Integer Division
    // see GLSL ES Specification 3.00, issues 12.33, Rounding of Integer Division
    bool support_integer_division_rounding = true;
};

/** Compile one statement into GLSL. */
class CodeGen_GLSL : public CodeGen_GLSLBase {
public:
    CodeGen_GLSL(std::ostream &s, const Target &t);

    void add_kernel(const Stmt &stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args);

    static void test();

protected:
    using CodeGen_GLSLBase::visit;

    void visit(const Div *) override;

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
    std::string get_vector_suffix(const Expr &e);

    std::vector<std::string> print_lanes(const Expr &expr);

    Scope<int> scalar_vars, vector_vars;
};

}  // namespace Internal
}  // namespace Halide

#endif
