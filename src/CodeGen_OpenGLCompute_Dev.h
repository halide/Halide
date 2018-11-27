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
                    const std::vector<DeviceArgument> &args) override;

    void init_module() override;

    std::vector<char> compile_to_src() override;

    std::string get_current_kernel_name() override;

    void dump() override;

    std::string print_gpu_name(const std::string &name) override;

    std::string api_unique_name() override { return "openglcompute"; }
    bool kernel_run_takes_types() const override { return true; }


protected:

    class CodeGen_OpenGLCompute_C : public CodeGen_GLSLBase {
    public:
        CodeGen_OpenGLCompute_C(std::ostream &s, Target t);
        void add_kernel(Stmt stmt,
                        const std::string &name,
                        const std::vector<DeviceArgument> &args);

    protected:
        std::string print_type(Type type, AppendSpaceIfNeeded space_option = DoNotAppendSpace) override;

        using CodeGen_C::visit;
        void visit(const For *) override;
        void visit(const Ramp *op) override;
        void visit(const Broadcast *op) override;
        void visit(const Load *op) override;
        void visit(const Store *op) override;
        void visit(const Cast *op) override;
        void visit(const Call *op) override;
        void visit(const Allocate *op) override;
        void visit(const Free *op) override;
        void visit(const Select *op) override;
        void visit(const Evaluate *op) override;
        void visit(const IntImm *op) override;

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
