#ifndef HALIDE_CODEGEN_OPENCL_DEV_H
#define HALIDE_CODEGEN_OPENCL_DEV_H

/** \file
 * Defines the code-generator for producing OpenCL C kernel code
 */

#include <sstream>

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class CodeGen_OpenCL_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_OpenCL_Dev(Target target);

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args);

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    void init_module();

    std::vector<char> compile_to_src();

    std::string get_current_kernel_name();

    void dump();

    virtual std::string print_gpu_name(const std::string &name);

    std::string api_unique_name() { return "opencl"; }

protected:

    class CodeGen_OpenCL_C : public CodeGen_C {
    public:
        CodeGen_OpenCL_C(std::ostream &s, Target t) : CodeGen_C(s, t) {}
        void add_kernel(Stmt stmt,
                        const std::string &name,
                        const std::vector<DeviceArgument> &args);

    protected:
        using CodeGen_C::visit;
        std::string print_type(Type type, AppendSpaceIfNeeded append_space = DoNotAppendSpace);
        std::string print_reinterpret(Type type, Expr e);

        std::string get_memory_space(const std::string &);

        void visit(const For *);
        void visit(const Ramp *op);
        void visit(const Broadcast *op);
        void visit(const Call *op);
        void visit(const Load *op);
        void visit(const Store *op);
        void visit(const Cast *op);
        void visit(const Select *op);
        void visit(const EQ *);
        void visit(const NE *);
        void visit(const LT *);
        void visit(const LE *);
        void visit(const GT *);
        void visit(const GE *);
        void visit(const Allocate *op);
        void visit(const Free *op);
        void visit(const AssertStmt *op);
        void visit(const Shuffle *op);
    };

    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_OpenCL_C clc;
    Target target;
};

}}

#endif
