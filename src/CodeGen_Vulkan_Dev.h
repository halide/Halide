#ifndef HALIDE_CODEGEN_VULKAN_DEV_H
#define HALIDE_CODEGEN_VULKAN_DEV_H

/** \file
 * Defines the code-generator for producing Vulkan SPIR-V kernel code
 */

#include "CodeGen_GPU_Dev.h"
#include "IRPrinter.h"
#include "Scope.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class CodeGen_LLVM;

template <typename CodeGenT, typename ValueT>
ValueT lower_int_uint_div(CodeGenT *cg, Expr a, Expr b);

template <typename CodeGenT, typename ValueT>
ValueT lower_int_uint_mod(CodeGenT *cg, Expr a, Expr b);

class CodeGen_Vulkan_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_Vulkan_Dev(Target target);

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

    std::string api_unique_name() override { return "vulkan"; }

protected:

    class SPIRVEmitter : public IRVisitor {
        
    public:
        SPIRVEmitter() { }

        using IRVisitor::visit;

        void visit(const Variable *) override;
        void visit(const IntImm *) override;
        void visit(const UIntImm *) override;
        void visit(const StringImm *) override;
        void visit(const FloatImm *) override;
        void visit(const Cast *) override;
        void visit(const Add *) override;
        void visit(const Sub *) override;
        void visit(const Mul *) override;
        void visit(const Div *) override;
        void visit(const Mod *) override;
        void visit(const Max *) override;
        void visit(const Min *) override;
        void visit(const EQ *) override;
        void visit(const NE *) override;
        void visit(const LT *) override;
        void visit(const LE *) override;
        void visit(const GT *) override;
        void visit(const GE *) override;
        void visit(const And *) override;
        void visit(const Or *) override;
        void visit(const Not *) override;
        void visit(const Call *) override;
        void visit(const Select *) override;
        void visit(const Load *) override;
        void visit(const Store *) override;
        void visit(const Let *) override;
        void visit(const LetStmt *) override;
        void visit(const AssertStmt *) override;
        void visit(const ProducerConsumer *) override;
        void visit(const For *) override;
        void visit(const Ramp *) override;
        void visit(const Broadcast *) override;
        void visit(const Provide *) override;
        void visit(const Allocate *) override;
        void visit(const Free *) override;
        void visit(const Realize *) override;
        void visit(const IfThenElse *) override;
        void visit(const Evaluate *) override;
        void visit(const Shuffle *) override;
        void visit(const Prefetch *) override;
        void visit(const Fork *) override;
        void visit(const Acquire *) override;

        void visit_binop(Type t, Expr a, Expr b, uint32_t opcode);

        // ID of last generated Expr.
        uint32_t id;
        // IDs are allocated in numerical order of use.
        uint32_t next_id{0};

        // The void type does not map to a Halide type, but must be unique
        uint32_t void_id;

        // SPIR-V instructions in a module must be in a specific
        // order. This order doesn't correspond to the order in which they
        // are created. Hence we generate into a set of blocks, each of
        // which is added to at its end. In compile_to_src, these are
        // concatenated to form a complete SPIR-V module.  We also
        // represent the temporaries as vectors of uint32_t rather than
        // char for ease of adding words to them.
        std::vector<uint32_t> spir_v_header;
        std::vector<uint32_t> spir_v_entrypoints;
        std::vector<uint32_t> spir_v_execution_modes;
        std::vector<uint32_t> spir_v_annotations;
        std::vector<uint32_t> spir_v_types;
        std::vector<uint32_t> spir_v_kernels;
        // The next one is cleared in between kernels, and tracks the allocations
        std::vector<uint32_t> spir_v_kernel_allocations;

        // Top-level function for adding kernels
        void add_kernel(Stmt s, const std::string &name, const std::vector<DeviceArgument> &args);

        // Function for allocating variables in function scope, with optional initializer.
        // These will appear at the beginning of the function, as required by SPIR-V
        void add_allocation(uint32_t result_type_id, uint32_t result_id, uint32_t storage_class, uint32_t initializer=0);

        std::map<Type, uint32_t> type_map;
        std::map<std::pair<Type, uint32_t>, uint32_t> pointer_type_map;
        std::map<Type, uint32_t> pair_type_map;
        std::map<std::string, uint32_t> constant_map;

        void add_instruction(std::vector<uint32_t> &region, uint32_t opcode,
                             std::initializer_list<uint32_t> words);
        void add_instruction(uint32_t opcode, std::initializer_list<uint32_t> words);
        void add_instruction(std::vector<uint32_t> &region, uint32_t opcode,
                            std::vector<uint32_t> words);
        void add_instruction(uint32_t opcode, std::vector<uint32_t> words);
        uint32_t map_type(const Type &type);
        // This takes a regular type, but makes pointer to a local variable.
        // TODO: remove the next two functions
        uint32_t map_pointer_type_local(const Type &type);
        uint32_t map_pointer_type_input(const Type &type);
        uint32_t map_pointer_type(const Type &type, const uint32_t storage_class);
        uint32_t map_type_to_pair(const Type &t);
        uint32_t emit_constant(const Type &t, const void *data);
        void scalarize(Expr e);

        Scope<uint32_t> symbol_table;

        struct PhiNodeInputs {
            uint32_t ids[4];
        };
        // Returns Phi node inputs.
        template <typename StmtOrExpr>
        PhiNodeInputs emit_if_then_else(Expr condition, StmtOrExpr then_case, StmtOrExpr else_case);

        /** Helpers for implementing fast integer division. */
        Expr mulhi_shr(Expr a, Expr b, int shr);
        // Compute (a+b)/2, assuming a < b.
        Expr sorted_avg(Expr a, Expr b);

        friend uint32_t lower_int_uint_div<CodeGen_Vulkan_Dev::SPIRVEmitter, uint32_t>(CodeGen_Vulkan_Dev::SPIRVEmitter *, Expr a, Expr b);
        friend uint32_t lower_int_uint_mod<CodeGen_Vulkan_Dev::SPIRVEmitter, uint32_t>(CodeGen_Vulkan_Dev::SPIRVEmitter *cg, Expr a, Expr b);
    } emitter;

    std::string current_kernel_name;

};

}  // namespace Internal
}  // namespace Halide

#endif
