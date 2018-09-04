#ifndef HALIDE_CODEGEN_OPENGLCOMPUTE_DEV_H
#define HALIDE_CODEGEN_OPENGLCOMPUTE_DEV_H

/** \file
 * Defines the code-generator for producing GLSL kernel code for OpenGL Compute.
 */

#include <map>
#include <sstream>

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_OpenGL_Dev.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class CodeGen_OpenGLCompute_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_OpenGLCompute_Dev(Target target);

    // CodeGen_GPU_Dev interface
    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args);

    void init_module();

    std::vector<char> compile_to_src();

    std::string get_current_kernel_name();

    void dump();

    virtual std::string print_gpu_name(const std::string &name);

    std::string api_unique_name() { return "openglcompute"; }
    bool kernel_run_takes_types() const { return true; }

    
protected:

    class CodeGen_OpenGLCompute_C : public CodeGen_GLSLBase {
    public:
        CodeGen_OpenGLCompute_C(std::ostream &s, Target t) : CodeGen_GLSLBase(s, t) {}
        void add_kernel(Stmt stmt,
                        const std::string &name,
                        const std::vector<DeviceArgument> &args);

    protected:
        std::string print_type(Type type, AppendSpaceIfNeeded space_option = DoNotAppendSpace);

        using CodeGen_C::visit;
        void visit(const For *);
        void visit(const Ramp *op);
        void visit(const Broadcast *op);
        void visit(const Load *op);
        void visit(const Store *op);
        void visit(const Cast *op);
        void visit(const Call *op);
        void visit(const Allocate *op);
        void visit(const Free *op);
        void visit(const Select *op);
        void visit(const Evaluate *op);
        void visit(const IntImm *op);
        void visit(const UIntImm *op);

    public:
        int workgroup_size[3];
    };

    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_OpenGLCompute_C glc;
};

}  // namespace Internal
}  // namespace Halide

#endif
