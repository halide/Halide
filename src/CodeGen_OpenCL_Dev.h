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
                    const std::vector<DeviceArgument> &args) override;

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    void init_module() override;

    std::vector<char> compile_to_src() override;

    std::string get_current_kernel_name() override;

    void dump() override;

    std::string print_gpu_name(const std::string &name) override;

    std::string api_unique_name() override {
        return "opencl";
    }

protected:
    class CodeGen_OpenCL_C : public CodeGen_C {
    public:
        CodeGen_OpenCL_C(std::ostream &s, Target t)
            : CodeGen_C(s, t) {
        }
        void add_kernel(Stmt stmt,
                        const std::string &name,
                        const std::vector<DeviceArgument> &args);

    protected:
        using CodeGen_C::visit;
        std::string print_type(Type type, AppendSpaceIfNeeded append_space = DoNotAppendSpace) override;
        std::string print_reinterpret(Type type, Expr e) override;
        std::string print_extern_call(const Call *op) override;
        void add_vector_typedefs(const std::set<Type> &vector_types) override;

        std::string get_memory_space(const std::string &);

        void visit(const For *) override;
        void visit(const Ramp *op) override;
        void visit(const Broadcast *op) override;
        void visit(const Call *op) override;
        void visit(const Load *op) override;
        void visit(const Store *op) override;
        void visit(const Cast *op) override;
        void visit(const Select *op) override;
        void visit(const EQ *) override;
        void visit(const NE *) override;
        void visit(const LT *) override;
        void visit(const LE *) override;
        void visit(const GT *) override;
        void visit(const GE *) override;
        void visit(const Allocate *op) override;
        void visit(const Free *op) override;
        void visit(const AssertStmt *op) override;
        void visit(const Shuffle *op) override;
        void visit(const Min *op) override;
        void visit(const Max *op) override;
        void visit(const Atomic *op) override;

        /** Emit atomic operations if we encounter a Producer node that matches these names. */
        std::set<std::string> emit_atomic_stores_for;

        /** Use for checking emit_atomic_stores_for. */
        std::string current_producer;
    };

    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_OpenCL_C clc;
};

}  // namespace Internal
}  // namespace Halide

#endif
