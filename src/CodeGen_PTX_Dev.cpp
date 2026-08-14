#include "CodeGen_PTX_Dev.h"

#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "CodeGen_LLVM.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "ExprUsesVar.h"
#include "ExtractWMMAOperations.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"
#include "ModulusRemainder.h"
#include "MultiRamp.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include "Target.h"

#include <fstream>
#include <set>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace Halide::ConciseCasts;

using namespace llvm;

#ifdef WITH_NVPTX

namespace {

/** Marks a read of one entry of a tensor core accumulator, for the CUDA
 * runtime to rewrite once it knows how the hardware lays fragments out. Must
 * match the string the runtime looks for in src/runtime/cuda.cpp. */
constexpr const char *wmma_get_element_marker = "halide_wmma_get";

/** A code generator that emits GPU code from a given Halide stmt. */
class CodeGen_PTX_Dev : public CodeGen_LLVM, public CodeGen_GPU_Dev {
public:
    /** Create a PTX device code generator. */
    CodeGen_PTX_Dev(const Target &host);
    ~CodeGen_PTX_Dev() override;

    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args) override;

    static void test();

    std::vector<char> compile_to_src() override;
    std::string get_current_kernel_name() override;

    void dump() override;

    std::string print_gpu_name(const std::string &name) override;

    std::string api_unique_name() override {
        return "cuda";
    }

protected:
    using CodeGen_LLVM::visit;

    /** (Re)initialize the PTX module. This is separate from compile, since
     * a PTX device module will often have many kernels compiled into it for
     * a single pipeline. */
    /* override */ void init_module() override;

    /** We hold onto the basic block at the start of the device
     * function in order to inject allocas */
    llvm::BasicBlock *entry_block;

    /** Nodes for which we need to override default behavior for the GPU runtime */
    // @{
    void visit(const Call *) override;
    void visit(const For *) override;
    void visit(const Allocate *) override;
    void visit(const Free *) override;
    void visit(const AssertStmt *) override;
    void visit(const Load *) override;
    void visit(const Store *) override;
    void visit(const Atomic *) override;
    void visit(const ProducerConsumer *) override;
    void codegen_vector_reduce(const VectorReduce *op, const Expr &init) override;
    // @}

    std::string mcpu_target() const override;
    std::string mcpu_tune() const override;
    std::string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;
    bool promote_indices() const override {
        return false;
    }

    Type upgrade_type_for_arithmetic(const Type &t) const override {
        return t;
    }
    Type upgrade_type_for_storage(const Type &t) const override;

    /** Map from simt variable names (e.g. foo.block_id_x) to the llvm ptx
     * intrinsic functions to call to get them. */
    std::string simt_intrinsic(const std::string &name);

    /** The memory type of each allocation made inside the kernel, so that
     * copies into shared memory can be recognized. */
    Scope<MemoryType> alloc_memory_type;

    /** Whether we're inside a producer node. */
    bool in_producer = false;

    /** The groups of asynchronous copies committed so far, oldest first, and
     * the group of any copies issued since the last commit. Waits are FIFO -
     * the hardware can only wait for all but the newest N groups - so a
     * group's position here is what determines the N we emit for it. */
    std::vector<int> committed_groups;
    int uncommitted_group = -1;

    /** Try to emit a store into shared memory as an asynchronous copy, which
     * moves the data straight from global memory without routing it through
     * registers. Returns false if this store isn't one we can do that for. */
    bool codegen_async_copy(const Store *op, const char **reason);

    /** Close the current group of asynchronous copies, if there is one. */
    void commit_copies();

    /** Emit a wait that leaves at most n groups of copies outstanding. */
    void emit_copy_wait(int n);

    /** Wait for the asynchronous copies in the given group to have landed. */
    void await_copies(int group);

    /** Wait for every asynchronous copy issued so far to have landed. */
    void await_all_copies();

    /** Emit calls to the nvvm warp-level matrix multiply-accumulate
     * intrinsics that drive the tensor cores. */
    // @{
    void codegen_wmma(const Call *op);
    llvm::Value *codegen_wmma_raw(const Call *op);
    void codegen_wmma_store(const Store *op);
    void split_fragment(const Expr &e, std::vector<llvm::Value *> &args);
    // @}

    /** Read one entry out of a tensor core accumulator. Which lane holds an
     * entry, and which of that lane's registers, is a property of the hardware
     * that isn't known until the module is loaded. Emit a placeholder shuffle
     * alongside a marker saying which entry was wanted and which registers hold
     * the fragment, and let the CUDA runtime rewrite the pair into a real
     * shuffle once it has measured the layout. */
    llvm::Value *codegen_wmma_get_element(const Call *fragment_to_matrix, int row, int col);
    void visit(const Shuffle *) override;

    /** A fragment is a fixed number of 32-bit registers per lane. Keeping it in
     * that form all the way to and from its allocation matters, because NVPTX
     * holds a wide float vector in pairs of registers, and packing and
     * unpacking one around every tensor core instruction costs more
     * instructions than the instructions themselves.
     */
    // @{
    bool is_fragment_alloc(const std::string &name);
    llvm::Type *fragment_reg_type(Type t);
    llvm::Value *fragment_reg_ptr(const std::string &name, Type t, int i);
    void codegen_fragment_store(const Store *op);
    llvm::Value *call_wmma_intrinsic(const std::string &name,
                                     const std::vector<llvm::Value *> &args,
                                     const std::vector<llvm::Type *> &overloads);
    // @}

    bool supports_atomic_add(const Type &t) const override;
};

CodeGen_PTX_Dev::CodeGen_PTX_Dev(const Target &host)
    : CodeGen_LLVM(host) {
    context = new llvm::LLVMContext();
}

CodeGen_PTX_Dev::~CodeGen_PTX_Dev() {
    // This is required as destroying the context before the module
    // results in a crash. Really, responsibility for destruction
    // should be entirely in the parent class.
    // TODO: Figure out how to better manage the context -- e.g. allow using
    // same one as the host.
    module.reset();
    delete context;
}

Type CodeGen_PTX_Dev::upgrade_type_for_storage(const Type &t) const {
    if (t.element_of() == Float(16)) {
        return t;
    }
    return CodeGen_LLVM::upgrade_type_for_storage(t);
}

// The largest extent of each of the GPU thread loops, if they are all
// constant. A kernel may contain several thread loops in sequence, so take the
// largest of each.
class BlockSize : public IRVisitor {
    using IRVisitor::visit;

    void visit(const For *op) override {
        for (int i = 0; i < 3; i++) {
            if (ends_with(op->name, gpu_thread_name(i))) {
                if (auto e = as_const_int(simplify(op->extent()))) {
                    extent[i] = std::max(extent[i], (int)*e);
                } else {
                    known = false;
                }
            }
        }
        IRVisitor::visit(op);
    }

public:
    int extent[3] = {1, 1, 1};
    bool known = true;
};

void CodeGen_PTX_Dev::add_kernel(Stmt stmt,
                                 const std::string &name,
                                 const std::vector<DeviceArgument> &args) {
    internal_assert(module != nullptr);

    debug(2) << "In CodeGen_PTX_Dev::add_kernel\n";

    // Now deduce the types of the arguments to our function
    vector<llvm::Type *> arg_types(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            arg_types[i] = ptr_t;
        } else {
            arg_types[i] = llvm_type_of(args[i].type);
        }
    }

    // Make our function
    FunctionType *func_t = FunctionType::get(void_t, arg_types, false);
    function = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module.get());
    set_function_attributes_from_halide_target_options(*function);

    // Mark the buffer args as no alias
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            function->addParamAttr(i, Attribute::NoAlias);
        }
    }

    function->setCallingConv(llvm::CallingConv::PTX_Kernel);

    // Make the initial basic block
    entry_block = BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entry_block);

    // Put the arguments in the symbol table
    vector<string> arg_sym_names;
    {
        size_t i = 0;
        for (auto &fn_arg : function->args()) {

            string arg_sym_name = args[i].name;
            sym_push(arg_sym_name, &fn_arg);
            fn_arg.setName(arg_sym_name);
            arg_sym_names.push_back(arg_sym_name);

            i++;
        }
    }

    // We won't end the entry block yet, because we'll want to add
    // some allocas to it later if there are local allocations. Start
    // a new block to put all the code.
    BasicBlock *body_block = BasicBlock::Create(*context, "body", function);
    builder->SetInsertPoint(body_block);

    debug(1) << "Generating llvm bitcode for kernel...\n";
    // Ok, we have a module, function, context, and a builder
    // pointing at a brand new basic block. We're good to go.
    stmt.accept(this);

    // Now we need to end the function
    builder->CreateRetVoid();

    // Make the entry block point to the body block
    builder->SetInsertPoint(entry_block);
    builder->CreateBr(body_block);

    // Add the nvvm annotation that it is a kernel function.
    llvm::Metadata *md_args[] = {
        llvm::ValueAsMetadata::get(function),
        MDString::get(*context, "kernel"),
        llvm::ValueAsMetadata::get(ConstantInt::get(i32_t, 1))};

    MDNode *md_node = MDNode::get(*context, md_args);

    module->getOrInsertNamedMetadata("nvvm.annotations")->addOperand(md_node);

    // Tell ptxas the most threads a block can have. Without this it assumes
    // the maximum, and budgets registers for it.
    BlockSize block_size;
    stmt.accept(&block_size);
    if (block_size.known) {
        function->addFnAttr("nvvm.maxntid",
                            std::to_string(block_size.extent[0]) + "," +
                                std::to_string(block_size.extent[1]) + "," +
                                std::to_string(block_size.extent[2]));
        debug(2) << "Kernel " << name << " has block size "
                 << block_size.extent[0] << "x" << block_size.extent[1]
                 << "x" << block_size.extent[2] << "\n";
    }

    // Now verify the function is ok
    verifyFunction(*function);

    // Finally, verify the module is ok
    verifyModule(*module);

    debug(2) << "Done generating llvm bitcode for PTX\n";

    // Clear the symbol table
    for (const auto &arg_sym_name : arg_sym_names) {
        sym_pop(arg_sym_name);
    }
}

void CodeGen_PTX_Dev::init_module() {
    // This class uses multiple inheritance. It's a GPU device code generator,
    // and also an llvm-based one. Both of these track strict_float presence,
    // but OffloadGPULoops only sets the GPU device code generator flag, so here
    // we set the CodeGen_LLVM flag to match.
    CodeGen_LLVM::any_strict_float = CodeGen_GPU_Dev::any_strict_float;

    init_context();

    module = get_initial_module_for_ptx_device(target, context);

    struct Intrinsic {
        const char *name;
        Type ret_type;
        const char *intrin_name;
        vector<Type> arg_types;
    };

    Intrinsic ptx_intrins[] = {
        {"dp4a", Int(32), "dp4a_s32_s32", {Int(8, 4), Int(8, 4), Int(32)}},
        {"dp4a", Int(32), "dp4a_s32_u32", {Int(8, 4), UInt(8, 4), Int(32)}},
        {"dp4a", Int(32), "dp4a_u32_s32", {UInt(8, 4), Int(8, 4), Int(32)}},
        {"dp4a", UInt(32), "dp4a_u32_u32", {UInt(8, 4), UInt(8, 4), UInt(32)}},
        {"dp2a", Int(32), "dp2a_s32_s32", {Int(16, 4), Int(8, 4), Int(32)}},
        {"dp2a", Int(32), "dp2a_s32_u32", {Int(16, 4), UInt(8, 4), Int(32)}},
        {"dp2a", Int(32), "dp2a_u32_s32", {UInt(16, 4), Int(8, 4), Int(32)}},
        {"dp2a", UInt(32), "dp2a_u32_u32", {UInt(16, 4), UInt(8, 4), UInt(32)}},
        {"round", Float(32), "llvm.rint.f32", {Float(32)}},
        {"round", Float(64), "llvm.rint.f64", {Float(64)}},
    };

    for (auto &&i : ptx_intrins) {
        auto *fn = declare_intrin_overload(i.name, i.ret_type, i.intrin_name, std::move(i.arg_types));
        function_does_not_access_memory(fn);
        fn->addFnAttr(llvm::Attribute::NoUnwind);
    }

    if (CodeGen_GPU_Dev::any_strict_float) {
        set_strict_fp_math();
        in_strict_float = target.has_feature(Target::StrictFloat);
    } else {
        set_fast_fp_math();
    }
}

void CodeGen_PTX_Dev::visit(const Call *op) {
    if (op->is_intrinsic(Call::cuda_await_copies)) {
        internal_assert(op->args.size() == 1);
        auto group = as_const_int(op->args[0]);
        internal_assert(group) << "cuda_await_copies group is not a constant integer\n";
        await_copies((int)*group);
        value = ConstantInt::get(i32_t, 0);
        return;
    }

    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        // Even though we always insert a __syncthreads equivalent
        // (which has both a device and shared memory fence)
        // check to make sure the intrinsic has the right number of
        // arguments
        internal_assert(op->args.size() == 1) << "gpu_thread_barrier() intrinsic must specify memory fence type.\n";

        // A barrier tells other threads the shared memory this thread wrote is
        // ready, so any asynchronous copies must have landed by now.
        await_all_copies();

        auto fence_type_ptr = as_const_int(op->args[0]);
        internal_assert(fence_type_ptr) << "gpu_thread_barrier() parameter is not a constant integer.\n";

        llvm::Function *barrier;
        if ((barrier = module->getFunction("llvm.nvvm.barrier.cta.sync.aligned.all")) && barrier->getIntrinsicID() != 0) {
            // LLVM 20.1.6 and above: https://github.com/llvm/llvm-project/pull/140615
            builder->CreateCall(barrier, builder->getInt32(0));
        } else if ((barrier = module->getFunction("llvm.nvvm.barrier0")) && barrier->getIntrinsicID() != 0) {
            // LLVM 21.1.5 and below: Testing for llvm.nvvm.barrier0 can be removed once we drop support for LLVM 20
            builder->CreateCall(barrier);
        } else {
            internal_error << "Could not find PTX barrier intrinsic llvm.nvvm.barrier0 nor llvm.nvvm.barrier.cta.sync.aligned.all\n";
        }
        value = ConstantInt::get(i32_t, 0);
        return;
    }

    if (is_wmma_intrinsic(op)) {
        codegen_wmma(op);
        return;
    }

    internal_assert(!op->is_intrinsic(Call::wmma_fragment_to_matrix_d) &&
                    !op->is_intrinsic(Call::wmma_lane_owns))
        << "A tensor core accumulator store was broken apart during lowering. "
        << op->name << " only has meaning as part of one.\n";

    // TODO: It would be better if CodeGen_LLVM could handle overloaded intrin calls by default.
    value = call_overloaded_intrin(op->type, op->name, op->args);
    if (!value) {
        CodeGen_LLVM::visit(op);
    }
}

namespace {

WMMAMatrixLayout matrix_in_memory(const string &name, const MultiRamp &mr, int rows, int cols, const Expr &access = Expr()) {
    WMMAMatrixLayout result;
    user_assert(wmma_matrix_layout(mr, rows, cols, &result))
        << "The memory a tensor core instruction moves a " << rows << "x" << cols
        << " matrix of " << name << " to or from is not a dense tile by the time it "
        << "reaches the backend. One cause is a shared memory allocation made inside "
        << "the loop over GPU threads, which gets striped across them; compute it at "
        << "a loop outside the threads instead. The addresses accessed are:\n"
        << mr.to_expr() << "\nThe access is:\n"
        << access << "\n";
    return result;
}

}  // namespace

// The nvvm intrinsics take and return a fragment as 32-bit registers. Halide
// represents half precision as llvm's half, which is what the intrinsics for
// it take directly, but bfloat and the eight-bit integers have no llvm vector
// type in the signature - those intrinsics take the lanes packed into an i32.
bool wmma_reg_is_packed_i32(Type t) {
    return t.bits() < 32 && t.element_of() != Float(16);
}

llvm::Value *CodeGen_PTX_Dev::codegen_wmma_get_element(const Call *op, int row, int col) {
    internal_assert(op->args.size() == 5);
    const Expr &fragment = op->args[3];
    Type t = fragment.type();
    user_assert(t.element_of() == Float(32))
        << "Reading an entry out of a tensor core accumulator is only "
        << "implemented for single precision accumulators, but this one holds "
        << t.element_of() << "\n";

    vector<Value *> regs;
    split_fragment(fragment, regs);

    // The marker names every register of the fragment, because which one holds
    // the entry is decided later. $0 is the result; $1 onwards are the
    // registers. The placeholder shuffle keeps the unpatched module
    // assemblable, and reads register zero from lane zero.
    std::ostringstream asm_text, constraints;
    asm_text << "// " << wmma_get_element_marker << " row=" << row << " col=" << col
             << " regs=";
    constraints << "=f";
    for (size_t i = 0; i < regs.size(); i++) {
        asm_text << (i ? "," : "") << "$" << (i + 1);
        constraints << ",f";
    }
    asm_text << "\n\tshfl.sync.idx.b32 $0, $1, 0, 31, -1;";

    vector<llvm::Type *> arg_types(regs.size(), f32_t);
    llvm::FunctionType *fn_type = llvm::FunctionType::get(f32_t, arg_types, false);
    llvm::InlineAsm *asm_call =
        llvm::InlineAsm::get(fn_type, asm_text.str(), constraints.str(),
                             /* hasSideEffects */ false);
    return builder->CreateCall(asm_call, regs);
}

void CodeGen_PTX_Dev::visit(const Shuffle *op) {
    // Taking entries out of a tensor core accumulator. Anything else is an
    // ordinary shuffle.
    const Call *frag = op->vectors.size() == 1 ? op->vectors[0].as<Call>() : nullptr;
    if (!frag || !frag->is_intrinsic(Call::wmma_fragment_to_matrix_d)) {
        CodeGen_LLVM::visit(op);
        return;
    }

    auto get_int_arg = [&](int i) {
        auto v = as_const_int(frag->args[i]);
        internal_assert(v);
        return (int)*v;
    };
    const int N = get_int_arg(1);

    // The matrix the fragment inflates to is in row-major order, so an index
    // into it says which entry of it was asked for.
    llvm::Type *result_type = llvm_type_of(op->type);
    value = UndefValue::get(result_type);
    for (int i = 0; i < (int)op->indices.size(); i++) {
        const int idx = op->indices[i];
        Value *element = codegen_wmma_get_element(frag, idx / N, idx % N);
        value = op->type.lanes() == 1 ?
                    element :
                    builder->CreateInsertElement(value, element, i);
    }
}

void CodeGen_PTX_Dev::split_fragment(const Expr &e, vector<Value *> &args) {
    // One llvm value per 32-bit register.
    const int num_regs = e.type().bits() * e.type().lanes() / 32;
    if (const Load *load = e.as<Load>()) {
        if (is_fragment_alloc(load->name)) {
            llvm::Type *reg_type = fragment_reg_type(e.type());
            for (int i = 0; i < num_regs; i++) {
                args.push_back(builder->CreateAlignedLoad(
                    reg_type, fragment_reg_ptr(load->name, e.type(), i), llvm::Align(4)));
            }
            return;
        }
    }
    Value *v = codegen(e);
    const int lanes_per_reg = 32 / e.type().bits();
    for (int i = 0; i < e.type().lanes() / lanes_per_reg; i++) {
        Value *reg = lanes_per_reg == 1 ?
                         builder->CreateExtractElement(v, i) :
                         slice_vector(v, i * lanes_per_reg, lanes_per_reg);
        if (wmma_reg_is_packed_i32(e.type())) {
            reg = builder->CreateBitCast(reg, i32_t);
        }
        args.push_back(reg);
    }
}

Value *CodeGen_PTX_Dev::call_wmma_intrinsic(const std::string &name,
                                            const vector<Value *> &args,
                                            const vector<llvm::Type *> &overloads) {
    llvm::Intrinsic::ID id = llvm::Intrinsic::lookupIntrinsicID(name);
    internal_assert(id != llvm::Intrinsic::not_intrinsic)
        << "Could not find the nvvm intrinsic " << name << "\n";
    llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id, overloads);
    return builder->CreateCall(fn, args);
}

void CodeGen_PTX_Dev::codegen_wmma(const Call *op) {
    Value *result = codegen_wmma_raw(op);

    // Reassemble the returned struct into a Halide vector.
    llvm::Type *result_type = llvm_type_of(op->type);
    const int num_regs = op->type.bits() * op->type.lanes() / 32;
    // A fragment that is a single register comes back as that register rather
    // than as a struct holding one of them.
    auto get_reg = [&](int i) {
        return result->getType()->isStructTy() ?
                   builder->CreateExtractValue(result, i) :
                   result;
    };
    if (op->type.bits() == 32) {
        value = UndefValue::get(result_type);
        for (int i = 0; i < num_regs; i++) {
            value = builder->CreateInsertElement(value, get_reg(i), i);
        }
    } else {
        vector<Value *> regs;
        regs.reserve(num_regs);
        for (int i = 0; i < num_regs; i++) {
            Value *reg = get_reg(i);
            if (wmma_reg_is_packed_i32(op->type)) {
                reg = builder->CreateBitCast(
                    reg, get_vector_type(llvm_type_of(op->type.element_of()),
                                         32 / op->type.bits()));
            }
            regs.push_back(reg);
        }
        value = concat_vectors(regs);
    }
    internal_assert(value->getType() == result_type)
        << "Unexpected result type from a tensor core instruction\n";
}

bool CodeGen_PTX_Dev::is_fragment_alloc(const std::string &name) {
    const MemoryType *t = alloc_memory_type.find(name);
    return t && *t == MemoryType::Tile;
}

llvm::Type *CodeGen_PTX_Dev::fragment_reg_type(Type t) {
    return t.bits() == 32 ? llvm_type_of(t.element_of()) :
                            get_vector_type(llvm_type_of(t.element_of()), 32 / t.bits());
}

llvm::Value *CodeGen_PTX_Dev::fragment_reg_ptr(const std::string &name, Type t, int i) {
    return codegen_buffer_pointer(name, t.element_of(), Expr(i * (32 / t.bits())));
}

void CodeGen_PTX_Dev::codegen_fragment_store(const Store *op) {
    const Type t = op->value.type();
    const int num_regs = t.bits() * t.lanes() / 32;
    llvm::Type *reg_type = fragment_reg_type(t);

    const Call *call = op->value.as<Call>();
    if (call && is_wmma_intrinsic(call)) {
        // Take the registers straight out of the struct the instruction
        // returns, without ever making a vector of them.
        Value *result = codegen_wmma_raw(call);
        for (int i = 0; i < num_regs; i++) {
            builder->CreateAlignedStore(builder->CreateExtractValue(result, i),
                                        fragment_reg_ptr(op->name, t, i), llvm::Align(4));
        }
        return;
    }

    // Anything else (a zero-initialization, say) does become a vector, but it
    // is still written a register at a time so that the allocation only ever
    // sees register-sized accesses.
    Value *v = codegen(op->value);
    const int lanes_per_reg = 32 / t.bits();
    for (int i = 0; i < num_regs; i++) {
        Value *reg = lanes_per_reg == 1 ?
                         builder->CreateExtractElement(v, i) :
                         slice_vector(v, i * lanes_per_reg, lanes_per_reg);
        internal_assert(reg->getType() == reg_type);
        builder->CreateAlignedStore(reg, fragment_reg_ptr(op->name, t, i), llvm::Align(4));
    }
}

// The element type the wmma intrinsic names use for a Halide type. The
// hardware takes 16-bit floats or 8-bit integers as multiplicands, and
// accumulates the first into 16 or 32-bit floats and the second into 32-bit
// integers.
std::string wmma_type_suffix(Type t) {
    if (t == Float(16)) {
        return "f16";
    } else if (t == BFloat(16)) {
        return "bf16";
    } else if (t == Float(32)) {
        return "f32";
    } else if (t == Int(8)) {
        return "s8";
    } else if (t == UInt(8)) {
        return "u8";
    } else if (t == Int(32)) {
        return "s32";
    }
    user_error << "There is no tensor core instruction for " << t << ".\n";
    return "";
}

llvm::Value *CodeGen_PTX_Dev::codegen_wmma_raw(const Call *op) {
    // The nvvm wmma intrinsics take and return fragments as a flat list of
    // 32-bit registers, packaged up as a literal struct. We represent them in
    // Halide IR as vectors, so most of the work here is repacking.
    auto get_int_arg = [&](int i) {
        auto v = as_const_int(op->args[i]);
        internal_assert(v) << "Expected a constant integer argument to " << op->name << "\n";
        return (int)*v;
    };

    const int M = get_int_arg(0), N = get_int_arg(1), K = get_int_arg(2);
    const char *layouts[] = {"row", "col"};

    std::ostringstream name;
    name << "llvm.nvvm.wmma.m" << M << "n" << N << "k" << K << ".";

    vector<Value *> args;
    vector<llvm::Type *> overloads;

    if (op->is_intrinsic(Call::wmma_mma)) {
        // Half precision multiplicands name the instruction after the d and c
        // operands, which for us are always the same type. Everything else
        // names it after the multiplicands, which are in args 5 and 6.
        const Type operand_type = op->args[5].type().element_of();
        std::string signature;
        if (operand_type == Float(16)) {
            const std::string acc = wmma_type_suffix(op->type.element_of());
            signature = acc + "." + acc;
        } else {
            signature = wmma_type_suffix(operand_type);
        }
        name << "mma." << layouts[get_int_arg(3)] << "." << layouts[get_int_arg(4)]
             << "." << signature;
        split_fragment(op->args[5], args);
        split_fragment(op->args[6], args);
        split_fragment(op->args[7], args);
    } else {
        // The a operand is M x K, the b operand is K x N, and the accumulator
        // is M x N.
        const bool is_a = op->is_intrinsic(Call::wmma_matrix_to_fragment_a);
        const bool is_b = op->is_intrinsic(Call::wmma_matrix_to_fragment_b);
        // The simplifier is free to have rewritten the load of the matrix into
        // a dense load followed by a transpose, which is what happens to a
        // column-major matrix. is_load_of_multiramp undoes that.
        const Expr &arg = op->args[wmma_matrix_arg(op)];
        MultiRamp mr;
        const Load *matrix = is_load_of_multiramp(arg, Scope<Expr>::empty_scope(), &mr);
        user_assert(matrix && matrix->type.element_of() == arg.type().element_of())
            << "The matrix a tensor core instruction takes a fragment out of is not a "
            << "load with an affine index by the time it reaches the backend.\n";
        WMMAMatrixLayout mem = matrix_in_memory(matrix->name, mr,
                                                is_b ? K : M, is_a ? K : N, arg);
        const std::string type_suffix = wmma_type_suffix(op->type.element_of());
        name << "load." << (is_a ? "a" : is_b ? "b" :
                                                "c")
             << "." << (mem.row_major ? "row" : "col") << ".stride." << type_suffix;

        Value *ptr = codegen_buffer_pointer(matrix->name, matrix->type.element_of(), mem.base);
        overloads.push_back(ptr->getType());
        args.push_back(ptr);
        args.push_back(codegen(cast(Int(32), mem.stride)));
    }

    return call_wmma_intrinsic(name.str(), args, overloads);
}

void CodeGen_PTX_Dev::codegen_wmma_store(const Store *op) {
    // Each lane writes the entries of the matrix that it holds, which the
    // predicate describes and which one wmma store instruction does for the
    // whole warp.
    // A store to a column-major matrix arrives as a dense store of the
    // transpose of it, because the simplifier rewrites stores to make them
    // dense. Undoing that gives back the store as the extraction pass wrote it,
    // and the column-major layout falls out of the index as usual.
    Expr index;
    const Call *inflate = peel_store_permutations(op, &index).as<Call>();
    internal_assert(inflate && inflate->args.size() == 5);
    Expr predicate = op->predicate;
    while (const Shuffle *shuffle = predicate.as<Shuffle>()) {
        predicate = shuffle->vectors[0];
    }
    internal_assert(predicate.as<Call>() &&
                    predicate.as<Call>()->is_intrinsic(Call::wmma_lane_owns))
        << "A store of a tensor core accumulator lost its predicate\n";

    auto get_int_arg = [&](int i) {
        auto v = as_const_int(inflate->args[i]);
        internal_assert(v);
        return (int)*v;
    };
    const int M = get_int_arg(0), N = get_int_arg(1), K = get_int_arg(2);
    const Expr &fragment = inflate->args[3];

    MultiRamp mr;
    internal_assert(is_multiramp(index, Scope<Expr>::empty_scope(), &mr));
    WMMAMatrixLayout mem = matrix_in_memory(op->name, mr, M, N);

    std::ostringstream name;
    name << "llvm.nvvm.wmma.m" << M << "n" << N << "k" << K << ".store.d."
         << (mem.row_major ? "row" : "col") << ".stride."
         << wmma_type_suffix(fragment.type().element_of());

    Value *ptr = codegen_buffer_pointer(op->name, op->value.type().element_of(), mem.base);
    vector<Value *> args{ptr};
    vector<llvm::Type *> overloads{ptr->getType()};
    split_fragment(fragment, args);
    args.push_back(codegen(cast(Int(32), mem.stride)));

    call_wmma_intrinsic(name.str(), args, overloads);
}

string CodeGen_PTX_Dev::simt_intrinsic(const string &name) {
    if (ends_with(name, gpu_thread_name(0))) {
        return "llvm.nvvm.read.ptx.sreg.tid.x";
    } else if (ends_with(name, gpu_thread_name(1))) {
        return "llvm.nvvm.read.ptx.sreg.tid.y";
    } else if (ends_with(name, gpu_thread_name(2))) {
        return "llvm.nvvm.read.ptx.sreg.tid.z";
    } else if (ends_with(name, gpu_block_name(0))) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.x";
    } else if (ends_with(name, gpu_block_name(1))) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.y";
    } else if (ends_with(name, gpu_block_name(2))) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.z";
    }
    internal_error << "simt_intrinsic called on bad variable name\n";
    return "";
}

void CodeGen_PTX_Dev::visit(const For *loop) {
    if (is_gpu(loop->for_type)) {
        Expr simt_idx = Call::make(Int(32), simt_intrinsic(loop->name), std::vector<Expr>(), Call::Extern);
        internal_assert(is_const_zero(loop->min));
        sym_push(loop->name, codegen(simt_idx));
        codegen(loop->body);
        sym_pop(loop->name);
    } else {
        CodeGen_LLVM::visit(loop);
    }
}

void CodeGen_PTX_Dev::visit(const Allocate *alloc) {
    user_assert(!alloc->new_expr.defined()) << "Allocate node inside PTX kernel has custom new expression.\n"
                                            << "(Memoization is not supported inside GPU kernels at present.)\n";
    ScopedBinding<MemoryType> bind(alloc_memory_type, alloc->name, alloc->memory_type);
    if (is_gpu_shared(alloc->memory_type)) {
        // PTX uses zero in address space 3 as the base address for shared memory
        Value *shared_base = Constant::getNullValue(PointerType::get(*context, 3));
        sym_push(alloc->name, shared_base);
    } else {
        debug(2) << "Allocate " << alloc->name << " on device\n";

        string allocation_name = alloc->name;
        debug(3) << "Pushing allocation called " << allocation_name << " onto the symbol table\n";

        // Jump back to the entry and generate an alloca. Note that by
        // jumping back we're rendering any expression we carry back
        // meaningless, so we had better only be dealing with
        // constants here.
        int32_t size = alloc->constant_allocation_size();
        internal_assert(size > 0)
            << "Allocation " << alloc->name << " has a dynamic size. "
            << "This should have been moved to the heap by the "
            << "fuse_gpu_thread_loops lowering pass.\n";

        BasicBlock *here = builder->GetInsertBlock();

        builder->SetInsertPoint(entry_block);
        Value *ptr = builder->CreateAlloca(llvm_type_of(alloc->type), ConstantInt::get(i32_t, size));
        builder->SetInsertPoint(here);
        sym_push(allocation_name, ptr);
    }
    codegen(alloc->body);
}

void CodeGen_PTX_Dev::visit(const Free *f) {
    sym_pop(f->name);
}

void CodeGen_PTX_Dev::visit(const AssertStmt *op) {
    // Discard the error message for now.
    Expr trap = Call::make(Int(32), "halide_ptx_trap", {}, Call::Extern);
    codegen(IfThenElse::make(!op->condition, Evaluate::make(trap)));
}

void CodeGen_PTX_Dev::visit(const Load *op) {
    // Do aligned 4-wide 32-bit loads as a single i128 load.
    const Ramp *r = op->index.as<Ramp>();
    // TODO: lanes >= 4, not lanes == 4
    if (is_const_one(op->predicate) && r && is_const_one(r->stride) && r->lanes == 4 && op->type.bits() == 32) {
        ModulusRemainder align = op->alignment;
        if (align.modulus % 4 == 0 && align.remainder % 4 == 0) {
            Expr index = simplify(r->base / 4);
            Expr equiv = Load::make(UInt(128), op->name, index,
                                    op->image, op->param, const_true(), align / 4, op->is_streaming);
            equiv = reinterpret(op->type, equiv);
            codegen(equiv);
            return;
        }
    }

    CodeGen_LLVM::visit(op);
}

// The name of the Func a marked store belonged to, for error messages. The
// store itself is named after the packed allocation it ended up in.
std::string async_copy_func_name(const Call *marker) {
    internal_assert(marker->args.size() == 3);
    const StringImm *name = marker->args[2].as<StringImm>();
    internal_assert(name) << "cuda_bypass_registers name is not a string\n";
    return name->value;
}

// A copy from global memory into shared memory can be done by the hardware
// without going through registers, which saves the load, the store, and the
// registers in between. The copy is asynchronous, so it has to be waited for
// before the data is used; that happens at the end of the producer.
bool CodeGen_PTX_Dev::codegen_async_copy(const Store *op, const char **reason) {
    if (target.get_cuda_capability_lower_bound() < 80) {
        *reason = "asynchronous copies require CUDA compute capability 8.0 or above";
        return false;
    }
    if (emit_atomic_stores) {
        *reason = "the store is atomic";
        return false;
    }
    // Asynchronous copies need something to wait for them, which only happens
    // inside a producer.
    if (!in_producer) {
        *reason = "the store is not inside a produce node";
        return false;
    }

    // The value must be one the schedule asked to have moved without passing
    // through registers, and it must be a plain load from something we didn't
    // allocate in here, which is to say global memory. An ordinary store that
    // happens to match the pattern is left synchronous.
    // CSE and LICM lift common subexpressions of a stored value into Lets
    // around it, which would hide the marker. Substituting them back in leaves
    // an expression that stands alone, which is what the analysis below and
    // codegen_buffer_pointer both need. It has to be held in a local, because
    // everything below points into it.
    const Expr stored = substitute_in_all_lets(op->value);
    const Call *marker = stored.as<Call>();
    if (!(marker && marker->is_intrinsic(Call::cuda_bypass_registers))) {
        *reason = "the store was not marked as an asynchronous copy";
        return false;
    }
    internal_assert(marker->args.size() == 3);
    const Expr &copied = marker->args[0];
    auto group = as_const_int(marker->args[1]);
    internal_assert(group) << "cuda_bypass_registers group is not a constant integer\n";

    const Load *src = copied.as<Load>();
    if (!src) {
        // A load that isn't dense is broken up into a shuffle of dense loads
        // well before we get here, so say what that means for the copy rather
        // than describing it as not being a load.
        Expr value = copied;
        if (const Shuffle *s = value.as<Shuffle>();
            s && !s->vectors.empty()) {
            *reason = "the source is not read densely. Each copy moves one run of "
                      "bytes, so the Func must read its source with a stride of one";
            return false;
        }
        *reason = "the value stored is not a load from a buffer outside the kernel. "
                  "An asynchronous copy moves bytes untouched, so the Func must be a "
                  "plain copy - no cast, no arithmetic, and no boundary condition";
        return false;
    }
    if (alloc_memory_type.contains(src->name)) {
        *reason = "the value stored is loaded from another allocation inside the "
                  "kernel. An asynchronous copy reads from global memory";
        return false;
    }
    if (!is_const_one(op->predicate) || !is_const_one(src->predicate)) {
        *reason = "the load or the store is predicated";
        return false;
    }

    // The hardware copies 4, 8 or 16 bytes at a time, from and to consecutive
    // addresses.
    const Type t = copied.type();
    const int bytes = t.bytes() * t.lanes();
    if (!(bytes == 4 || bytes == 8 || bytes == 16)) {
        *reason = "each thread must copy 4, 8 or 16 bytes at a time. Vectorize the "
                  "copy along its dense dimension by that many bytes' worth";
        return false;
    }
    Expr dst_base = op->index, src_base = src->index;
    if (t.lanes() > 1) {
        // Shared allocations are given an offset into one big block after the
        // last simplification pass, so the indices need simplifying here.
        // strided_ramp_base returns undefined unless the stride is exactly
        // one, so this checks the density as well as finding the address.
        dst_base = strided_ramp_base(simplify(op->index));
        src_base = strided_ramp_base(simplify(src->index));
        if (!dst_base.defined() || !src_base.defined()) {
            *reason = "the source and the destination are not both indexed densely";
            return false;
        }
    }
    // The hardware needs both addresses aligned to the width of the copy. A
    // stride that isn't a multiple of it - which is what an odd align_storage
    // produces - shows up here as an unprovable alignment.
    if (t.lanes() > 1) {
        // Use the alignment lowering worked out, which knows what the loop
        // variables in the index are multiples of.
        // Only the destination is checked. Its alignment is what align_storage
        // controls, so it is the one a schedule can get wrong. A source in a
        // buffer whose strides are not known until runtime has no provable
        // alignment either way, and rejecting those would fail schedules that
        // are fine.
        auto aligned = [&](const ModulusRemainder &a, const Expr &base) {
            auto ok = [&](const ModulusRemainder &m) {
                return m.modulus % t.lanes() == 0 && m.remainder % t.lanes() == 0;
            };
            return ok(a) || ok(modulus_remainder(base));
        };
        if (!aligned(op->alignment, dst_base)) {
            *reason = "the destination is not known to be aligned to the width of "
                      "the copy. Any align_storage on this Func has to be a multiple "
                      "of the number of elements each thread copies";
            return false;
        }
    }

    Value *dst = codegen_buffer_pointer(op->name, t.element_of(), dst_base);
    Value *src_ptr = codegen_buffer_pointer(src->name, t.element_of(), src_base);

    // Shared allocations are already in the shared address space. The source is
    // a kernel argument, so it's global, but it comes in as a generic pointer.
    llvm::Type *shared_ptr_t = PointerType::get(*context, 3);
    llvm::Type *global_ptr_t = PointerType::get(*context, 1);
    if (dst->getType() != shared_ptr_t) {
        *reason = "the destination did not end up in the shared address space";
        return false;
    }
    src_ptr = builder->CreateAddrSpaceCast(src_ptr, global_ptr_t);

    std::ostringstream name;
    name << "llvm.nvvm.cp.async.ca.shared.global." << bytes;
    llvm::Intrinsic::ID id = llvm::Intrinsic::lookupIntrinsicID(name.str());
    internal_assert(id != llvm::Intrinsic::not_intrinsic)
        << "Could not find the nvvm intrinsic " << name.str() << "\n";
    llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id);
    // Copies are committed in groups, so close the previous group before
    // starting one for a different batch.
    if (uncommitted_group != -1 && uncommitted_group != (int)*group) {
        commit_copies();
    }
    builder->CreateCall(fn, {dst, src_ptr});
    uncommitted_group = (int)*group;
    return true;
}

void CodeGen_PTX_Dev::visit(const ProducerConsumer *op) {
    if (!op->is_producer) {
        CodeGen_LLVM::visit(op);
        return;
    }

    ScopedValue<bool> old_in(in_producer, true);
    codegen(op->body);
}

void CodeGen_PTX_Dev::commit_copies() {
    if (uncommitted_group == -1) {
        return;
    }
    llvm::Intrinsic::ID id =
        llvm::Intrinsic::lookupIntrinsicID("llvm.nvvm.cp.async.commit.group");
    internal_assert(id != llvm::Intrinsic::not_intrinsic);
    builder->CreateCall(llvm::Intrinsic::getOrInsertDeclaration(module.get(), id), {});
    committed_groups.push_back(uncommitted_group);
    uncommitted_group = -1;
}

void CodeGen_PTX_Dev::emit_copy_wait(int n) {
    llvm::Intrinsic::ID id =
        llvm::Intrinsic::lookupIntrinsicID("llvm.nvvm.cp.async.wait.group");
    internal_assert(id != llvm::Intrinsic::not_intrinsic);
    llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id);
    vector<Value *> args;
    if (fn->getFunctionType()->getNumParams() == 1) {
        args.push_back(ConstantInt::get(i32_t, n));
    }
    builder->CreateCall(fn, args);
}

void CodeGen_PTX_Dev::await_copies(int group) {
    commit_copies();
    // The wait is FIFO, so waiting for this group means letting everything
    // committed after it stay outstanding. Searching from the newest end finds
    // the most recent batch with this group, which is the one just issued.
    for (size_t i = committed_groups.size(); i > 0; i--) {
        if (committed_groups[i - 1] != group) {
            continue;
        }
        emit_copy_wait((int)(committed_groups.size() - i));
        committed_groups.erase(committed_groups.begin(),
                               committed_groups.begin() + i);
        return;
    }
    // Nothing from that group is outstanding, so there is nothing to wait for.
}

void CodeGen_PTX_Dev::await_all_copies() {
    commit_copies();
    if (committed_groups.empty()) {
        return;
    }
    emit_copy_wait(0);
    committed_groups.clear();
}

void CodeGen_PTX_Dev::visit(const Store *op) {
    if (is_wmma_matrix_store(op)) {
        codegen_wmma_store(op);
        return;
    }

    // Issue atomic store if we are inside an Atomic node.
    if (emit_atomic_stores) {
        user_assert(is_const_one(op->predicate)) << "Atomic update does not support predicated store.\n";
        user_assert(op->value.type().bits() >= 32) << "CUDA: 8-bit or 16-bit atomics are not supported.\n";
    }

    if (is_fragment_alloc(op->name)) {
        codegen_fragment_store(op);
        return;
    }

    {
        const char *reason = "";
        if (codegen_async_copy(op, &reason)) {
            return;
        }
        // Asking for that memory type is a promise that the stores to it are
        // copies the hardware can make asynchronously. If one isn't, say so
        // rather than quietly emitting a load and a store instead.
        const Expr stored = substitute_in_all_lets(op->value);
        const Call *marker = stored.as<Call>();
        if (marker && marker->is_intrinsic(Call::cuda_bypass_registers)) {
            user_error
                << async_copy_func_name(marker) << " is scheduled in GPUSharedAsync memory, but this "
                << "store to it cannot be done with an asynchronous copy, because "
                << reason << ".\n\n"
                << "An asynchronous copy moves bytes from global memory into shared "
                << "memory without routing them through registers. It requires that "
                << "the Func is a plain copy of a buffer or another Func - no cast, "
                << "arithmetic, or boundary condition, because the bytes move "
                << "untouched - and that each thread stores a dense vector of 4, 8 or "
                << "16 bytes, aligned to its own size at both ends. It needs CUDA "
                << "compute capability 8.0 or above.\n\n"
                << "The alignment of the destination is set by align_storage, which "
                << "fixes the stride of the staged Func. Padding it to avoid bank "
                << "conflicts is usually a good idea, but the padded stride has to "
                << "stay a multiple of the vector width or the rows stop being "
                << "aligned enough to copy into.\n\n"
                << "The usual way to get a Func that is a plain copy is Func::in, "
                << "which makes a wrapper that does nothing but hold a staged copy of "
                << "something. Vectorizing its dense dimension by a whole number of "
                << "bytes gives each thread one copy to issue, and its other "
                << "dimensions are spread over the threads of the block as usual:\n\n"
                << "    A.in()\n"
                << "     .compute_at(consumer, r)\n"
                << "     .store_in(MemoryType::GPUSharedAsync)\n"
                << "     .tile(x, y, xi, yi, 256, 8)  // 256 = 8 elements x 32 threads\n"
                << "     .vectorize(xi, 8)            // 8 halves is 16 bytes\n"
                << "     .gpu_threads(xi, yi);\n\n"
                << "The tile has to divide into the thread counts the block already "
                << "has: its width over the vector width is the number of threads in "
                << "x, and its height the number in y. Anything left over is covered "
                << "by the serial loops the tile leaves outside.\n\n"
                << "The store that could not be made asynchronous was:\n"
                << Stmt(op);
        }
    }

    // Do aligned 4-wide 32-bit stores as a single i128 store.
    const Ramp *r = op->index.as<Ramp>();
    // TODO: lanes >= 4, not lanes == 4
    if (is_const_one(op->predicate) && r && is_const_one(r->stride) && r->lanes == 4 && op->value.type().bits() == 32) {
        ModulusRemainder align = op->alignment;
        if (align.modulus % 4 == 0 && align.remainder % 4 == 0) {
            Expr index = simplify(r->base / 4);
            Expr value = reinterpret(UInt(128), op->value);
            Stmt equiv = op->with(value, index, const_true(), align / 4);
            codegen(equiv);
            return;
        }
    }

    CodeGen_LLVM::visit(op);
}

void CodeGen_PTX_Dev::visit(const Atomic *op) {
    // CUDA requires all the threads in a warp to perform the same operations,
    // which means our mutex will lead to deadlock.
    user_assert(op->mutex_name.empty())
        << "The atomic update requires a mutex lock, which is not supported in CUDA.\n";

    // Issue atomic stores.
    ScopedValue<bool> old_emit_atomic_stores(emit_atomic_stores, true);
    CodeGen_LLVM::visit(op);
}

// The NVPTX backend generates really terrible code if loads aren't 32-bit. This
// mutator replaces 8- or 16-bit loads aligned to 32-bits with 32-bit loads of fewer
// lanes instead.
class RewriteLoadsAs32Bit : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Load *op) override {
        if (op->type.is_scalar() || op->type.bits() * op->type.lanes() < 32) {
            return IRMutator::visit(op);
        }

        Expr index = mutate(op->index);
        int sub_lanes = 32 / op->type.bits();
        const Ramp *idx = index.as<Ramp>();
        if (idx &&
            is_const_one(op->predicate) &&
            is_const_one(idx->stride) &&
            op->alignment.modulus % sub_lanes == 0 &&
            op->alignment.remainder % sub_lanes == 0) {
            Expr new_idx = simplify(idx->base / sub_lanes);
            int load_lanes = op->type.lanes() / sub_lanes;
            if (op->type.lanes() > sub_lanes) {
                new_idx = Ramp::make(new_idx, 1, load_lanes);
            }
            Expr new_load = Load::make(Int(32, load_lanes), op->name, new_idx, op->image, op->param, const_true(load_lanes), op->alignment / sub_lanes, op->is_streaming);
            return reinterpret(op->type, new_load);
        } else {
            return op->with(index, op->predicate, op->alignment);
        }
    }
};

void CodeGen_PTX_Dev::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
    // Pattern match 8/16-bit dot products
    struct Pattern {
        VectorReduce::Operator op;
        int factor;
        Expr pattern;
        const char *name;
        int flags;
        enum {
            SwapOps = 1 << 0,  // This happens before narrowing op 1 below.
            NarrowOp1 = 1 << 1,
        };
    };
    static Expr wild_i8x = Variable::make(Int(8, 0), "*");
    static Expr wild_u8x = Variable::make(UInt(8, 0), "*");
    static Expr wild_i16x = Variable::make(Int(16, 0), "*");
    static Expr wild_u16x = Variable::make(UInt(16, 0), "*");
    // TODO: Support rewriting to arbitrary calls in IRMatch and use that instead
    // of expr_match here. That would probably allow avoiding the redundant swapping
    // operands logic.
    static const Pattern patterns[] = {
        {VectorReduce::Add, 4, i32(widening_mul(wild_i8x, wild_i8x)), "dp4a"},
        {VectorReduce::Add, 4, i32(widening_mul(wild_i8x, wild_u8x)), "dp4a"},
        {VectorReduce::Add, 4, i32(widening_mul(wild_u8x, wild_i8x)), "dp4a"},
        {VectorReduce::Add, 4, u32(widening_mul(wild_u8x, wild_u8x)), "dp4a"},
        {VectorReduce::Add, 4, widening_mul(wild_i16x, wild_i16x), "dp2a", Pattern::NarrowOp1},
        {VectorReduce::Add, 4, widening_mul(wild_i16x, wild_u16x), "dp2a", Pattern::NarrowOp1},
        {VectorReduce::Add, 4, widening_mul(wild_u16x, wild_i16x), "dp2a", Pattern::NarrowOp1},
        {VectorReduce::Add, 4, widening_mul(wild_u16x, wild_u16x), "dp2a", Pattern::NarrowOp1},
        {VectorReduce::Add, 4, widening_mul(wild_i16x, wild_i16x), "dp2a", Pattern::SwapOps | Pattern::NarrowOp1},
        {VectorReduce::Add, 4, widening_mul(wild_u16x, wild_i16x), "dp2a", Pattern::SwapOps | Pattern::NarrowOp1},
        {VectorReduce::Add, 4, widening_mul(wild_i16x, wild_u16x), "dp2a", Pattern::SwapOps | Pattern::NarrowOp1},
        {VectorReduce::Add, 4, widening_mul(wild_u16x, wild_u16x), "dp2a", Pattern::SwapOps | Pattern::NarrowOp1},
    };

    const int input_lanes = op->value.type().lanes();
    const int factor = input_lanes / op->type.lanes();

    std::vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (p.op != op->op || factor % p.factor != 0) {
            continue;
        }
        if (!expr_match(p.pattern, op->value, matches)) {
            continue;
        }
        Expr a = matches[0];
        Expr b = matches[1];
        if (p.flags & Pattern::SwapOps) {
            std::swap(a, b);
        }
        if (p.flags & Pattern::NarrowOp1) {
            // This pattern needs the second operand to be narrowed further.
            Expr b_narrow = lossless_cast(b.type().narrow(), b);
            if (!b_narrow.defined()) {
                b_narrow = lossless_cast(b.type().narrow().with_code(halide_type_uint), b);
                if (!b_narrow.defined()) {
                    continue;
                }
            }
            b = b_narrow;
        }
        Expr i = init;
        if (!i.defined()) {
            i = cast(op->value.type(), 0);
        }

        vector<Expr> result;
        for (int l = 0; l < op->type.lanes(); l++) {
            // To compute a single lane of the output, we'll
            // extract the appropriate slice of the args, which
            // have been reinterpreted as 32-bit vectors, then
            // call either dp4a or dp2a the appropriate number of
            // times, and finally sum the result.
            Expr i_slice = Shuffle::make_extract_element(i, l);
            for (int i = 0; i < factor; i += p.factor) {
                Expr a_slice = Shuffle::make_slice(a, i + l * factor, 1, p.factor);
                Expr b_slice = Shuffle::make_slice(b, i + l * factor, 1, p.factor);
                i_slice = Call::make(i_slice.type(), p.name, {a_slice, b_slice, i_slice}, Call::PureExtern);
            }
            i_slice = RewriteLoadsAs32Bit()(i_slice);
            i_slice = simplify(i_slice);
            i_slice = common_subexpression_elimination(i_slice);
            result.push_back(i_slice);
        }
        // Concatenate the per-lane results to get the full vector result
        Expr equiv = Shuffle::make_concat(result);
        equiv.accept(this);
        return;
    }
    CodeGen_LLVM::codegen_vector_reduce(op, init);
}

string CodeGen_PTX_Dev::mcpu_target() const {
    if (target.has_feature(Target::CUDACapability120)) {
        return "sm_120";
    } else if (target.has_feature(Target::CUDACapability100)) {
        return "sm_100";
    } else if (target.has_feature(Target::CUDACapability90)) {
        return "sm_90";
    } else if (target.has_feature(Target::CUDACapability89)) {
        return "sm_89";
    } else if (target.has_feature(Target::CUDACapability86)) {
        return "sm_86";
    } else if (target.has_feature(Target::CUDACapability80)) {
        return "sm_80";
    } else if (target.has_feature(Target::CUDACapability75)) {
        return "sm_75";
    } else if (target.has_feature(Target::CUDACapability70)) {
        return "sm_70";
    } else if (target.has_feature(Target::CUDACapability61)) {
        return "sm_61";
    } else if (target.has_feature(Target::CUDACapability50)) {
        return "sm_50";
    } else if (target.has_feature(Target::CUDACapability35)) {
        return "sm_35";
    } else if (target.has_feature(Target::CUDACapability32)) {
        return "sm_32";
    } else if (target.has_feature(Target::CUDACapability30)) {
        return "sm_30";
    } else {
        return "sm_20";
    }
}

string CodeGen_PTX_Dev::mcpu_tune() const {
    return mcpu_target();
}

string CodeGen_PTX_Dev::mattrs() const {
    if (target.has_feature(Target::CUDACapability120)) {
        return "+ptx87";
    } else if (target.has_feature(Target::CUDACapability100)) {
        return "+ptx86";
    } else if (target.has_feature(Target::CUDACapability90)) {
        return "+ptx78";
    } else if (target.has_feature(Target::CUDACapability89)) {
        return "+ptx78";
    } else if (target.has_feature(Target::CUDACapability86)) {
        return "+ptx71";
    } else if (target.has_feature(Target::CUDACapability80)) {
        return "+ptx70";
    } else if (target.has_feature(Target::CUDACapability75)) {
        return "+ptx63";
    } else if (target.has_feature(Target::CUDACapability70)) {
        return "+ptx60";
    } else if (target.has_feature(Target::CUDACapability61)) {
        return "+ptx50";
    } else if (target.features_any_of({Target::CUDACapability32,
                                       Target::CUDACapability50})) {
        // sm_32 needs ptx isa 4.0 even though it seems to break the ordering
        return "+ptx40";
    } else if (target.features_any_of({Target::CUDACapability35,
                                       Target::CUDACapability30})) {
        return "+ptx32";
    }
    // Let LLVM pick
    return "";
}

bool CodeGen_PTX_Dev::use_soft_float_abi() const {
    return false;
}

vector<char> CodeGen_PTX_Dev::compile_to_src() {
    debug(2) << "In CodeGen_PTX_Dev::compile_to_src";

    // DISABLED - hooked in here to force PrintBeforeAll option - seems to be the only way?
    /*char* argv[] = { "llc", "-print-before-all" };*/
    /*int argc = sizeof(argv)/sizeof(char*);*/
    /*cl::ParseCommandLineOptions(argc, argv, "Halide PTX internal compiler\n");*/

    // Allocate target machine (similar to code in CodeGen_Internal.cpp make_target_machine)
    std::string err_str;
    const llvm::Target *llvm_target = TargetRegistry::lookupTarget(
        module->getTargetTriple(),
        err_str);
    auto triple = llvm::Triple(module->getTargetTriple());
    internal_assert(llvm_target) << "Could not create LLVM target for " << triple.str() << "\n";

    TargetOptions options;
    options.AllowFPOpFusion = CodeGen_GPU_Dev::any_strict_float ? llvm::FPOpFusion::Strict : llvm::FPOpFusion::Fast;
#if LLVM_VERSION < 230
    options.NoInfsFPMath = !CodeGen_GPU_Dev::any_strict_float;
    options.NoNaNsFPMath = !CodeGen_GPU_Dev::any_strict_float;
#endif
    options.HonorSignDependentRoundingFPMathOption = !CodeGen_GPU_Dev::any_strict_float;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;

    std::unique_ptr<TargetMachine>
        target_machine(llvm_target->createTargetMachine(
            triple,
            mcpu_target(), mattrs(), options,
            llvm::Reloc::PIC_,
            llvm::CodeModel::Small,
            CodeGenOptLevel::Aggressive));

    internal_assert(target_machine.get()) << "Could not allocate target machine!";

    module->setDataLayout(target_machine->createDataLayout());

    // Set up passes
    llvm::SmallString<8> outstr;
    raw_svector_ostream ostream(outstr);
    ostream.SetUnbuffered();

    // NVidia's libdevice library uses a __nvvm_reflect to choose
    // how to handle denormalized numbers. (The pass replaces calls
    // to __nvvm_reflect with a constant via a map lookup. The inliner
    // pass then resolves these situations to fast code, often a single
    // instruction per decision point.)
    //
    // The default is (more) IEEE like handling. FTZ mode flushes them
    // to zero. (This may only apply to single-precision.)
    //
    // The libdevice documentation covers other options for math accuracy
    // such as replacing division with multiply by the reciprocal and
    // use of fused-multiply-add, but they do not seem to be controlled
    // by this __nvvvm_reflect mechanism and may be flags to earlier compiler
    // passes.
    const int kFTZDenorms = 1;

    // Insert a module flag for the FTZ handling.
    module->addModuleFlag(llvm::Module::Override, "nvvm-reflect-ftz",
                          kFTZDenorms);

    if (kFTZDenorms) {
        for (llvm::Function &fn : *module) {
            fn.addFnAttr("nvptx-f32ftz", "true");
        }
    }

    const bool do_loop_opt = get_target().has_feature(Target::EnableLLVMLoopOpt);

    // Define and run optimization pipeline with new pass manager
    PipelineTuningOptions pto;
    pto.LoopInterleaving = do_loop_opt;
    pto.LoopVectorization = do_loop_opt;
    pto.SLPVectorization = true;  // Note: SLP vectorization has no analogue in the Halide scheduling model
    pto.LoopUnrolling = do_loop_opt;
    pto.ForgetAllSCEVInLoopUnroll = true;

    llvm::PassBuilder pb(target_machine.get(), pto);

    // These analysis managers have to be declared in this order.
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    // Register all the basic analyses with the managers.
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    ModulePassManager mpm;

    using OptimizationLevel = llvm::OptimizationLevel;
    OptimizationLevel level = OptimizationLevel::O3;

    target_machine->registerPassBuilderCallbacks(pb);

    mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(*module, mam);

    if (llvm::verifyModule(*module, &errs())) {
        report_fatal_error("Transformation resulted in an invalid module\n");
    }

    // Optimization pipeline completed; run codegen pipeline

    // NOTE: use of the "legacy" PassManager here is still required; it is deprecated
    // for optimization, but is still the only complete API for codegen as of work-in-progress
    // LLVM14. At the time of this comment (Dec 2021), there is no firm plan as to when codegen will
    // be fully available in the new PassManager, so don't worry about this 'legacy'
    // tag until there's any indication that the old APIs start breaking.
    //
    // See:
    // https://lists.llvm.org/pipermail/llvm-dev/2021-April/150100.html
    // https://releases.llvm.org/13.0.0/docs/ReleaseNotes.html#changes-to-the-llvm-ir
    // https://groups.google.com/g/llvm-dev/c/HoS07gXx0p8
    legacy::PassManager module_pass_manager;
    module_pass_manager.add(createTargetTransformInfoWrapperPass(target_machine->getTargetIRAnalysis()));

    // Override default to generate verbose assembly.
    target_machine->Options.MCOptions.AsmVerbose = true;

    // Output string stream

    // Ask the target to add backend passes as necessary.
    bool fail = target_machine->addPassesToEmitFile(module_pass_manager, ostream, nullptr,
                                                    CodeGenFileType::AssemblyFile, true);
    internal_assert(!fail) << "Failed to set up passes to emit PTX source\n";
    module_pass_manager.run(*module);

    // Codegen pipeline completed.
    debug(2) << [&] {
        dump();
        return "Done with CodeGen_PTX_Dev::compile_to_src";
    }();

    debug(1) << "PTX kernel:\n"
             << outstr.c_str() << "\n";

    vector<char> buffer(outstr.begin(), outstr.end());

    // Dump the SASS too if the cuda SDK is in the path
    debug(2) << "Compiling PTX to SASS. Will fail if CUDA SDK is not installed (and in the path).\n";
    debug(2) << [&] {
        TemporaryFile ptx(get_current_kernel_name(), ".ptx");
        TemporaryFile sass(get_current_kernel_name(), ".sass");

        std::ofstream f(ptx.pathname());
        f.write(buffer.data(), buffer.size());
        f.close();

        if (run_process({"ptxas", "--gpu-name", mcpu_target(), ptx.pathname(), "-o", sass.pathname()}) == 0) {
            (void)run_process({"nvdisasm", sass.pathname()});  // Don't care if it fails
        }

        // Note: It works to embed the contents of the .sass file in
        // the buffer instead of the ptx source, and this could help
        // with app startup times. Expose via the target?
        /*
        {
            std::ifstream f(sass.pathname());
            buffer.clear();
            f.seekg(0, std::ios_base::end);
            std::streampos sz = f.tellg();
            buffer.resize(sz);
            f.seekg(0, std::ios_base::beg);
            f.read(buffer.data(), sz);
        }
        */
        return "";
    }();

    // Null-terminate the ptx source
    buffer.push_back(0);
    return buffer;
}

int CodeGen_PTX_Dev::native_vector_bits() const {
    // PTX doesn't really do vectorization. The widest type is a double.
    return 64;
}

string CodeGen_PTX_Dev::get_current_kernel_name() {
    return get_llvm_function_name(function);
}

void CodeGen_PTX_Dev::dump() {
    module->print(dbgs(), nullptr, false, true);
}

std::string CodeGen_PTX_Dev::print_gpu_name(const std::string &name) {
    return name;
}

bool CodeGen_PTX_Dev::supports_atomic_add(const Type &t) const {
    if (t.bits() < 32) {
        // TODO: Half atomics are supported by compute capability 7.x or higher.
        return false;
    }
    if (t.is_int_or_uint()) {
        return true;
    }
    if (t.is_float() && t.bits() == 32) {
        return true;
    }
    if (t.is_float() && t.bits() == 64) {
        // double atomics are supported since CC6.1
        return target.get_cuda_capability_lower_bound() >= 61;
    }
    return false;
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_PTX_Dev(const Target &target) {
    return std::make_unique<CodeGen_PTX_Dev>(target);
}

#else  // WITH_PTX

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_PTX_Dev(const Target &target) {
    user_error << "PTX not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_PTX

}  // namespace Internal
}  // namespace Halide
