#include "CodeGen_PTX_Dev.h"
#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "CodeGen_LLVM.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"
#include "ModulusRemainder.h"
#include "Simplify.h"
#include "Solve.h"
#include "Target.h"

#include <fstream>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace Halide::ConciseCasts;

using namespace llvm;

#ifdef WITH_NVPTX

namespace {

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

    /** Whether we're inside a producer node, which is where the wait for any
     * asynchronous copies gets emitted, and whether any have been issued in
     * it. */
    bool in_producer = false;
    bool issued_async_copy = false;

    /** Try to emit a store into shared memory as an asynchronous copy, which
     * moves the data straight from global memory without routing it through
     * registers. Returns false if this store isn't one we can do that for. */
    bool codegen_async_copy(const Store *op, const char **reason);

    /** Wait for any asynchronous copies issued so far to have landed. */
    void wait_for_async_copies();

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
    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        // Even though we always insert a __syncthreads equivalent
        // (which has both a device and shared memory fence)
        // check to make sure the intrinsic has the right number of
        // arguments
        internal_assert(op->args.size() == 1) << "gpu_thread_barrier() intrinsic must specify memory fence type.\n";

        // A barrier tells other threads the shared memory this thread wrote is
        // ready, so any asynchronous copies must have landed by now.
        wait_for_async_copies();

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

    // TODO: It would be better if CodeGen_LLVM could handle overloaded intrin calls by default.
    value = call_overloaded_intrin(op->type, op->name, op->args);
    if (!value) {
        CodeGen_LLVM::visit(op);
    }
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

    // The destination must be shared memory that was asked for asynchronously,
    // and the source must be a plain load from something we didn't allocate in
    // here, which is to say global memory. Stores to plain GPUShared that
    // happen to match the pattern are left synchronous, so that a schedule can
    // ask for either one.
    const MemoryType *dst_memory_type = alloc_memory_type.find(op->name);
    if (!dst_memory_type || *dst_memory_type != MemoryType::GPUSharedAsync) {
        *reason = "the destination is not stored in MemoryType::GPUSharedAsync";
        return false;
    }
    const Load *src = op->value.as<Load>();
    if (!src) {
        // A load that isn't dense is broken up into a shuffle of dense loads
        // well before we get here, so say what that means for the copy rather
        // than describing it as not being a load.
        Expr value = op->value;
        while (const Let *let = value.as<Let>()) {
            value = let->body;
        }
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
    const Type t = op->value.type();
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
    builder->CreateCall(fn, {dst, src_ptr});
    issued_async_copy = true;
    return true;
}

void CodeGen_PTX_Dev::visit(const ProducerConsumer *op) {
    if (!op->is_producer) {
        CodeGen_LLVM::visit(op);
        return;
    }

    ScopedValue<bool> old_issued(issued_async_copy, false);
    ScopedValue<bool> old_in(in_producer, true);
    codegen(op->body);
    // Everything issued in here has to have landed before the values are used,
    // which is after this producer.
    wait_for_async_copies();
}

void CodeGen_PTX_Dev::wait_for_async_copies() {
    if (!issued_async_copy) {
        return;
    }
    for (const char *intrin : {"llvm.nvvm.cp.async.commit.group",
                               "llvm.nvvm.cp.async.wait.group"}) {
        llvm::Intrinsic::ID id = llvm::Intrinsic::lookupIntrinsicID(intrin);
        internal_assert(id != llvm::Intrinsic::not_intrinsic);
        llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id);
        vector<Value *> args;
        if (fn->getFunctionType()->getNumParams() == 1) {
            args.push_back(ConstantInt::get(i32_t, 0));
        }
        builder->CreateCall(fn, args);
    }
    issued_async_copy = false;
}

void CodeGen_PTX_Dev::visit(const Store *op) {
    // Issue atomic store if we are inside an Atomic node.
    if (emit_atomic_stores) {
        user_assert(is_const_one(op->predicate)) << "Atomic update does not support predicated store.\n";
        user_assert(op->value.type().bits() >= 32) << "CUDA: 8-bit or 16-bit atomics are not supported.\n";
    }

    {
        const char *reason = "";
        if (codegen_async_copy(op, &reason)) {
            return;
        }
        // Asking for this memory type is a promise that the stores to it are
        // copies the hardware can make asynchronously. If one isn't, say so
        // rather than quietly emitting a load and a store instead.
        const MemoryType *t = alloc_memory_type.find(op->name);
        if (t && *t == MemoryType::GPUSharedAsync) {
            user_error
                << op->name << " is scheduled in GPUSharedAsync memory, but this "
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
    if (target.has_feature(Target::CUDACapability86)) {
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
    if (target.has_feature(Target::CUDACapability86)) {
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
