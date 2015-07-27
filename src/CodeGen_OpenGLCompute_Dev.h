#ifndef HALIDE_CODEGEN_OPENGLCOMPUTE_DEV_H
#define HALIDE_CODEGEN_OPENGLCOMPUTE_DEV_H

/** \file
 * Defines the code-generator for producing GLSL kernel code for OpenGL Compute.
 */

#include <sstream>
#include <map>

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class CodeGen_OpenGLCompute_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_OpenGLCompute_Dev(Target target);

    // CodeGen_GPU_Dev interface
    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<GPU_Argument> &args);

    void init_module();

    std::vector<char> compile_to_src();

    std::string get_current_kernel_name();

    void dump();

    virtual std::string print_gpu_name(const std::string &name);

    std::string api_unique_name() { return "openglcompute"; }

protected:

    class CodeGen_OpenGLCompute_C : public CodeGen_C {
    public:
        CodeGen_OpenGLCompute_C(std::ostream &s) : CodeGen_C(s) {}
        void add_kernel(Stmt stmt,
                        Target target,
                        const std::string &name,
                        const std::vector<GPU_Argument> &args);

    protected:
        using CodeGen_C::visit;
        std::string print_type(Type type);
        std::string print_name(const std::string &);

        void visit(const Div *);
        void visit(const Mod *);
        void visit(const For *);
        void visit(const Ramp *op);
        void visit(const Broadcast *op);
        void visit(const Load *op);
        void visit(const Store *op);
        void visit(const Cast *op);
        void visit(const Allocate *op);
        void visit(const Free *op);
        void visit(const Select *op);

    public:
        int workgroup_size[3];
    };

    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_OpenGLCompute_C glc;
    Target target;
};

}}

#endif
