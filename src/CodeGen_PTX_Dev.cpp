#include "CodeGen_PTX_Dev.h"
#include "CSE.h"
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
#include "Simplify.h"
#include "Solve.h"
#include "Target.h"
#include "UnrollLoops.h"

#include <fstream>

// This is declared in NVPTX.h, which is not exported. Ugly, but seems better than
// hardcoding a path to the .h file.
#ifdef WITH_NVPTX
namespace llvm {
FunctionPass *createNVVMReflectPass(const StringMap<int> &Mapping);
}
#endif

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

    /** Map from simt variable names (e.g. foo.__block_id_x) to the llvm
     * ptx intrinsic functions to call to get them. */
    static std::string simt_intrinsic(const std::string &name);

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
    void codegen_vector_reduce(const VectorReduce *op, const Expr &init) override;
    // @}

    std::string march() const;
    std::string mcpu() const override;
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
            arg_types[i] = llvm_type_of(UInt(8))->getPointerTo();
        } else {
            arg_types[i] = llvm_type_of(args[i].type);
        }
    }

    // Make our function
    FunctionType *func_t = FunctionType::get(void_t, arg_types, false);
    function = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module.get());
    set_function_attributes_for_target(function, target);

    // Mark the buffer args as no alias
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            function->addParamAttr(i, Attribute::NoAlias);
        }
    }

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
    for (size_t i = 0; i < arg_sym_names.size(); i++) {
        sym_pop(arg_sym_names[i]);
    }
}

void CodeGen_PTX_Dev::init_module() {
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
        {"wmma.m16n16k16.load.a.row", Float(16, 16), "wmma.m16n16k16.load.a.row.f16.p0i8", {Handle(), Int(32), Int(32)}},
        {"wmma.m16n16k16.load.b.row", Float(16, 16), "wmma.m16n16k16.load.b.row.f16.p0i8", {Handle(), Int(32), Int(32)}},
        {"wmma.m16n16k16.load.c.row", Float(32, 8), "wmma.m16n16k16.load.c.row.f32.p0i8", {Handle(), Int(32), Int(32)}},
        {"wmma.m16n16k16.mma.row.row", Float(32, 8), "wmma.m16n16k16.mma.row.row.f32.f32", {Float(16, 16), Float(16, 16), Float(32, 8)}},
        {"wmma.m16n16k16.store.d.row", Handle(), "wmma.m16n16k16.store.d.row.f32", {Handle(), Int(32), Int(32), Float(32, 8)}}};

    for (auto &&i : ptx_intrins) {
        auto *fn = declare_intrin_overload(i.name, i.ret_type, i.intrin_name, std::move(i.arg_types));
        fn->addFnAttr(llvm::Attribute::ReadNone);
        fn->addFnAttr(llvm::Attribute::NoUnwind);
    }
}

void CodeGen_PTX_Dev::visit(const Call *op) {
    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        // Even though we always insert a __syncthreads equivalent
        // (which has both a device and shared memory fence)
        // check to make sure the intrinsic has the right number of
        // arguments
        internal_assert(op->args.size() == 1) << "gpu_thread_barrier() intrinsic must specify memory fence type.\n";

        const auto *fence_type_ptr = as_const_int(op->args[0]);
        internal_assert(fence_type_ptr) << "gpu_thread_barrier() parameter is not a constant integer.\n";

        llvm::Function *barrier0 = module->getFunction("llvm.nvvm.barrier0");
        internal_assert(barrier0) << "Could not find PTX barrier intrinsic (llvm.nvvm.barrier0)\n";
        builder->CreateCall(barrier0);
        value = ConstantInt::get(i32_t, 0);
    } else if (op->name == "dp2a" || op->name == "dp4a" || starts_with(op->name, "wmma.")) {
        // TODO: It would be better if CodeGen_LLVM could handle overloaded intrin calls by default.
        value = call_overloaded_intrin(op->type, op->name, op->args);
        internal_assert(value) << Expr(op) << "\n";
    } else {
        CodeGen_LLVM::visit(op);
    }
}

string CodeGen_PTX_Dev::simt_intrinsic(const string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return "llvm.nvvm.read.ptx.sreg.tid.x";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "llvm.nvvm.read.ptx.sreg.tid.y";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "llvm.nvvm.read.ptx.sreg.tid.z";
    } else if (ends_with(name, ".__thread_id_w")) {
        return "llvm.nvvm.read.ptx.sreg.tid.w";
    } else if (ends_with(name, ".__block_id_x")) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.x";
    } else if (ends_with(name, ".__block_id_y")) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.y";
    } else if (ends_with(name, ".__block_id_z")) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.z";
    } else if (ends_with(name, ".__block_id_w")) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.w";
    } else if (ends_with(name, ".__block_dim_x")) {
        return "llvm.nvvm.read.ptx.sreg.ntid.x";
    } else if (ends_with(name, ".__block_dim_y")) {
        return "llvm.nvvm.read.ptx.sreg.ntid.y";
    }
    internal_error << "simt_intrinsic called on bad variable name\n";
    return "";
}

void CodeGen_PTX_Dev::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
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
    if (alloc->memory_type == MemoryType::GPUShared) {
        // PTX uses zero in address space 3 as the base address for shared memory
        Value *shared_base = Constant::getNullValue(PointerType::get(i8_t, 3));
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
                                    op->image, op->param, const_true(), align / 4);
            equiv = reinterpret(op->type, equiv);
            codegen(equiv);
            return;
        }
    }

    CodeGen_LLVM::visit(op);
}

void CodeGen_PTX_Dev::visit(const Store *op) {
    // Issue atomic store if we are inside an Atomic node.
    if (emit_atomic_stores) {
        user_assert(is_const_one(op->predicate)) << "Atomic update does not support predicated store.\n";
        user_assert(op->value.type().bits() >= 32) << "CUDA: 8-bit or 16-bit atomics are not supported.\n";
    }

    // Do aligned 4-wide 32-bit stores as a single i128 store.
    const Ramp *r = op->index.as<Ramp>();
    // TODO: lanes >= 4, not lanes == 4
    if (is_const_one(op->predicate) && r && is_const_one(r->stride) && r->lanes == 4 && op->value.type().bits() == 32) {
        ModulusRemainder align = op->alignment;
        if (align.modulus % 4 == 0 && align.remainder % 4 == 0) {
            Expr index = simplify(r->base / 4);
            Expr value = reinterpret(UInt(128), op->value);
            Stmt equiv = Store::make(op->name, value, index, op->param, const_true(), align / 4);
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
            Expr new_load = Load::make(Int(32, load_lanes), op->name, new_idx, op->image, op->param, const_true(load_lanes), op->alignment / sub_lanes);
            return reinterpret(op->type, new_load);
        } else if (index.same_as(op->index)) {
            return op;
        } else {
            return Load::make(op->type, op->name, op->index, op->image, op->param, op->predicate, op->alignment);
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
    // clang-format off
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
    // clang-format on

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
            i_slice = RewriteLoadsAs32Bit().mutate(i_slice);
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

string CodeGen_PTX_Dev::march() const {
    return "nvptx64";
}

string CodeGen_PTX_Dev::mcpu() const {
    if (target.has_feature(Target::CUDACapability80)) {
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

string CodeGen_PTX_Dev::mattrs() const {
    if (target.has_feature(Target::CUDACapability80)) {
        return "+ptx70";
    } else if (target.has_feature(Target::CUDACapability70) ||
               target.has_feature(Target::CUDACapability75)) {
        return "+ptx60";
    } else if (target.has_feature(Target::CUDACapability61)) {
        return "+ptx50";
    } else if (target.features_any_of({Target::CUDACapability32,
                                       Target::CUDACapability50})) {
        // Need ptx isa 4.0.
        return "+ptx40";
    } else {
        // Use the default. For llvm 3.5 it's ptx 3.2.
        return "";
    }
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

    llvm::Triple triple(module->getTargetTriple());

    // Allocate target machine

    std::string err_str;
    const llvm::Target *llvm_target = TargetRegistry::lookupTarget(triple.str(), err_str);
    internal_assert(llvm_target) << err_str << "\n";

    TargetOptions options;
#if LLVM_VERSION < 120
    options.PrintMachineCode = false;
#endif
    options.AllowFPOpFusion = FPOpFusion::Fast;
    options.UnsafeFPMath = true;
    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.StackAlignmentOverride = 0;

    std::unique_ptr<TargetMachine>
        target_machine(llvm_target->createTargetMachine(triple.str(),
                                                        mcpu(), mattrs(), options,
                                                        llvm::Reloc::PIC_,
                                                        llvm::CodeModel::Small,
                                                        CodeGenOpt::Aggressive));

    internal_assert(target_machine.get()) << "Could not allocate target machine!";

    module->setDataLayout(target_machine->createDataLayout());

    // Set up passes
    llvm::SmallString<8> outstr;
    raw_svector_ostream ostream(outstr);
    ostream.SetUnbuffered();

    legacy::FunctionPassManager function_pass_manager(module.get());
    legacy::PassManager module_pass_manager;

    module_pass_manager.add(createTargetTransformInfoWrapperPass(target_machine->getTargetIRAnalysis()));
    function_pass_manager.add(createTargetTransformInfoWrapperPass(target_machine->getTargetIRAnalysis()));

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

    // At present, we default to *enabling* LLVM loop optimization,
    // unless DisableLLVMLoopOpt is set; we're going to flip this to defaulting
    // to *not* enabling these optimizations (and removing the DisableLLVMLoopOpt feature).
    // See https://github.com/halide/Halide/issues/4113 for more info.
    // (Note that setting EnableLLVMLoopOpt always enables loop opt, regardless
    // of the setting of DisableLLVMLoopOpt.)
    const bool do_loop_opt = !target.has_feature(Target::DisableLLVMLoopOpt) ||
                             target.has_feature(Target::EnableLLVMLoopOpt);

    PassManagerBuilder b;
    b.OptLevel = 3;
    b.Inliner = createFunctionInliningPass(b.OptLevel, 0, false);
    b.LoopVectorize = do_loop_opt;
    b.SLPVectorize = true;
    b.DisableUnrollLoops = !do_loop_opt;

    target_machine->adjustPassManager(b);

    b.populateFunctionPassManager(function_pass_manager);
    b.populateModulePassManager(module_pass_manager);

    // Override default to generate verbose assembly.
    target_machine->Options.MCOptions.AsmVerbose = true;

    // Output string stream

    // Ask the target to add backend passes as necessary.
    bool fail = target_machine->addPassesToEmitFile(module_pass_manager, ostream, nullptr,
                                                    ::llvm::CGFT_AssemblyFile,
                                                    true);
    if (fail) {
        internal_error << "Failed to set up passes to emit PTX source\n";
    }

    // Run optimization passes
    function_pass_manager.doInitialization();
    for (llvm::Module::iterator i = module->begin(); i != module->end(); i++) {
        function_pass_manager.run(*i);
    }
    function_pass_manager.doFinalization();
    module_pass_manager.run(*module);

    if (debug::debug_level() >= 2) {
        dump();
    }
    debug(2) << "Done with CodeGen_PTX_Dev::compile_to_src";

#define DEBUG_PTX 0
#if DEBUG_PTX
    // Adding this in the PTX allows the use of Nsight to debug the PTX
    // TODO: Could this an official Halide feature?
    std::string ptx_src(outstr.begin(), outstr.end());
    ptx_src = replace_all(ptx_src, ".target sm_70", ".target sm_70, debug");
    ptx_src.append("\n.section  .debug_abbrev\n{\n\n}\n\n");
    vector<char> buffer(ptx_src.begin(), ptx_src.end());
#else
    std::vector<char> buffer(outstr.begin(), outstr.end());
#endif

    debug(1) << "PTX kernel:\n"
             << buffer.data() << "\n";

    // Dump the SASS too if the cuda SDK is in the path
    if (debug::debug_level() >= 2) {
        debug(2) << "Compiling PTX to SASS. Will fail if CUDA SDK is not installed (and in the path).\n";

        TemporaryFile ptx(get_current_kernel_name(), ".ptx");
        TemporaryFile sass(get_current_kernel_name(), ".sass");

        std::ofstream f(ptx.pathname());
        f.write(buffer.data(), buffer.size());
        f.close();

        string cmd = "ptxas --gpu-name " + mcpu() + " " + ptx.pathname() + " -o " + sass.pathname();
        if (system(cmd.c_str()) == 0) {
            cmd = "nvdisasm " + sass.pathname();
            int ret = system(cmd.c_str());
            (void)ret;  // Don't care if it fails
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
    }

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

struct MatrixMultiplyInfo {
    Expr A;
    Expr B;
    Expr C;

    bool is_valid() const {
        return A.defined() && B.defined() && C.defined();
    }
};

MatrixMultiplyInfo is_matrix_multiply(const For *loop) {
    if ((loop->for_type != ForType::Serial) || !is_const_zero(loop->min) || !is_const(loop->extent)) {
        return MatrixMultiplyInfo{};
    }

    // The body of the loop can be a LetStmt or a Store
    const LetStmt *let = loop->body.as<LetStmt>();
    const Store *store = loop->body.as<Store>();

    // If it is a let and the body is a store, use it
    if (let && let->body.as<Store>()) {
        store = let->body.as<Store>();
    }

    if (!store) {
        return MatrixMultiplyInfo{};
    }

    //  matmul$1[t26] = (float32)matmul$1[t26]
    //      + (float32((float16)A[((A.stride.1*t34) - t33) + matmul$1.s1.k$x])
    //      * *float32((float16)B[(B.stride.1*matmul$1.s1.k$x) + t35]))
    const Expr wild_f32x = Variable::make(Float(32), "*");
    const Expr wild_f16x = Variable::make(Float(16), "*");
    const Expr acc_pattern = wild_f32x + f32(wild_f16x) * f32(wild_f16x);
    vector<Expr> matches;
    if (expr_match(acc_pattern, store->value, matches)) {
        // (float32)matmul$1[t26]
        const Load *load_c = matches[0].as<Load>();
        // (float16) A[((A.stride.1 * t34) - t33) + matmul$1.s1.k$x]
        const Load *load_a = matches[1].as<Load>();
        // (float16)B[(B.stride.1*matmul$1.s1.k$x) + t35]
        const Load *load_b = matches[2].as<Load>();

        if (!load_a || !load_b || !load_c) {
            return MatrixMultiplyInfo{};
        }

        // Check if the load is loading from the same place where the store is storing
        // i.e. if this is an update operation
        if (load_c->name != store->name || !equal(load_c->index, store->index)) {
            return MatrixMultiplyInfo{};
        }

        // Yes, this is an update operation, now let's check cast to see if it is a matmul that can be converted
        // to wmma intrinsic
        const Expr wild_i32x = Variable::make(Int(32), "*");
        // Check if the reduction domain is being used in both A and B to
        // validate the matrix multiply
        const Expr load_a_pattern = wild_i32x + wild_i32x;
        const Expr load_b_pattern = wild_i32x * wild_i32x + wild_i32x;

        vector<Expr> matches_a, matches_b;
        const bool match_a = expr_match(load_a_pattern, load_a->index, matches_a);
        const bool match_b = expr_match(load_b_pattern, load_b->index, matches_b);
        if (!match_a || !match_b) {
            return MatrixMultiplyInfo{};
        }

        // Check if the k_var_name is present in the expressions for load_a and load_b
        const Variable *load_a_var_1 = matches_a[0].as<Variable>();
        const Variable *load_a_var_2 = matches_a[1].as<Variable>();
        const Variable *load_b_var_1 = matches_b[0].as<Variable>();
        const Variable *load_b_var_2 = matches_b[1].as<Variable>();

        const std::string &k_var_name = loop->name;

        // TODO: Can this checks be improved?
        const bool load_a_ok = (load_a_var_1 && load_a_var_1->name == k_var_name) || (load_a_var_2 && load_a_var_2->name == k_var_name);
        const bool load_b_ok = (load_b_var_1 && load_b_var_1->name == k_var_name) || (load_b_var_2 && load_b_var_2->name == k_var_name);

        if (!load_a_ok || !load_b_ok) {
            return MatrixMultiplyInfo{};
        }

        // This matches A(k, y) * B(x, k) where both A and B
        MatrixMultiplyInfo matMulInfo;
        matMulInfo.A = load_a;
        matMulInfo.B = load_b;
        matMulInfo.C = load_c;
        return matMulInfo;
    }

    return MatrixMultiplyInfo{};
}

ExtractTensorCoreOperations::ExtractTensorCoreOperations() {
    thread_id_y = Call::make(Int(32), CodeGen_PTX_Dev::simt_intrinsic(".__thread_id_y"), std::vector<Expr>(), Call::Extern);
    thread_id_x = Call::make(Int(32), CodeGen_PTX_Dev::simt_intrinsic(".__thread_id_x"), std::vector<Expr>(), Call::Extern);
    block_id_y = Call::make(Int(32), CodeGen_PTX_Dev::simt_intrinsic(".__block_id_y"), std::vector<Expr>(), Call::Extern);
    block_id_x = Call::make(Int(32), CodeGen_PTX_Dev::simt_intrinsic(".__block_id_x"), std::vector<Expr>(), Call::Extern);
    block_dim_y = Call::make(Int(32), CodeGen_PTX_Dev::simt_intrinsic(".__block_dim_y"), std::vector<Expr>(), Call::Extern);
    block_dim_x = Call::make(Int(32), CodeGen_PTX_Dev::simt_intrinsic(".__block_dim_x"), std::vector<Expr>(), Call::Extern);
    block_size = block_dim_x * block_dim_y;
}

Stmt ExtractTensorCoreOperations::visit(const For *loop) {
    const bool is_gpu_thread_var = CodeGen_GPU_Dev::is_gpu_thread_var(loop->name);
    if (is_const_zero(loop->min) && is_const(loop->extent)) {
        const int32_t loop_extent_value = loop->extent.as<IntImm>()->value;

        if (is_gpu_thread_var) {
            if (ends_with(loop->name, ".__thread_id_y")) {
                wmma_M = loop_extent_value;
            } else if (ends_with(loop->name, ".__thread_id_x")) {
                wmma_N = loop_extent_value;
            }
        } else {
            wmma_K = loop_extent_value;

            // Shape m16n16k16
            // TODO: Note this won't work unless the extent of the inner loop is constant
            //       How can the shape be detected if the k is not constant, i.e, if k
            //       is based on the dimentions of matrix A for example?
            if (wmma_M == 16 && wmma_N == 16 && wmma_K % 16 == 0) {
                const int32_t num_tiles_k = wmma_K / 16;
                wmma_K = 16;

                // Now check the loop body to confirm this is a matrix multiply expression
                MatrixMultiplyInfo matMulInfo = is_matrix_multiply(loop);
                if (matMulInfo.is_valid()) {
                    const Load *load_a = matMulInfo.A.as<Load>();
                    const Load *load_b = matMulInfo.B.as<Load>();
                    const Load *load_c = matMulInfo.C.as<Load>();

                    // Check the possible types for A, B and C
                    if (load_c->type == Float(32) && load_a->type == Float(16) && load_b->type == Float(16)) {
                        global_M = Variable::make(Int(32), load_c->name + ".extent.1");
                        global_N = Variable::make(Int(32), load_c->name + ".extent.0");
                        global_K = num_tiles_k * wmma_K;

                        Expr stride_a = global_K;
                        Expr stride_b = global_N;
                        Expr stride_c = global_N;

                        Expr var_a = Variable::make(Handle(), load_a->name);
                        Expr var_b = Variable::make(Handle(), load_b->name);
                        Expr var_c = Variable::make(Handle(), load_c->name);

                        // Calculates the global warp indices used to iterate over the k
                        // tiles of the matrices
                        Expr warp_x = (block_id_x * block_dim_x + thread_id_x) / warp_size;
                        Expr warp_y = block_id_y * block_dim_y + thread_id_y;

                        Expr offset_c_value = i32(global_N * wmma_M * warp_y + wmma_N * warp_x);

#define INLINE_TILE_LOOP 0
#if INLINE_TILE_LOOP
                        Expr offset_c = offset_c_value;

                        Expr frag_accumulator = Call::make(Float(32, 8), "wmma.m16n16k16.load.c.row", {var_c, offset_c, stride_c}, Call::Intrinsic);

                        std::vector<Stmt> wmma_ops;
                        for (int32_t tile_k = 0; tile_k < num_tiles_k; ++tile_k) {
                            // Calculates the offsets to access tiles of matrices A, B and C
                            // Note that the offsets are based on the global warp indices
                            Expr offset_a = i64(global_K * wmma_M * warp_y + wmma_K * tile_k);
                            Expr offset_b = i64(global_N * wmma_K * tile_k + wmma_N * warp_x);

                            Expr frag_a = Call::make(Float(16, 16), "wmma.m16n16k16.load.a.row", {var_a, offset_a, stride_a}, Call::Intrinsic);
                            Expr frag_b = Call::make(Float(16, 16), "wmma.m16n16k16.load.b.row", {var_b, offset_b, stride_b}, Call::Intrinsic);
                            frag_accumulator = Call::make(Float(32, 8), "wmma.m16n16k16.mma.row.row", {frag_a, frag_b, frag_accumulator}, Call::Intrinsic);

                            wmma_ops.push_back(Evaluate::make(frag_accumulator));
                        }
                        Expr store_frag = Call::make(Handle(), "wmma.m16n16k16.store.d.row", {var_c, offset_c, stride_c, frag_accumulator}, Call::Intrinsic);
                        wmma_ops.push_back(Evaluate::make(store_frag));

                        Stmt tiled_for = Block::make(wmma_ops);
#else
                        // WIP Code: Trying to create a Halide For loop to do the computation

                        // Creates a for to loop over the k tiles to perform the matrix multiply accumulate
                        Expr tile_k = Variable::make(Int(32), "tile_k");

                        // Calculates the offsets to access tiles of matrices A, B and C
                        // Note that the offsets are based on the global warp indices
                        Expr col_a_value = i32(wmma_K * tile_k);
                        Expr row_a_value = i32(wmma_M * warp_y);

                        Expr col_b_value = i32(wmma_N * warp_x);
                        Expr row_b_value = i32(wmma_K * tile_k);

                        Expr col_c_value = i32(wmma_N * warp_x);
                        Expr row_c_value = i32(wmma_M * warp_y);

                        Expr col_a_var = Variable::make(Int(32), "col_a");
                        Expr row_a_var = Variable::make(Int(32), "row_a");
                        Expr col_b_var = Variable::make(Int(32), "col_b");
                        Expr row_b_var = Variable::make(Int(32), "row_b");
                        Expr col_c_var = Variable::make(Int(32), "col_c");
                        Expr row_c_var = Variable::make(Int(32), "row_c");

                        Expr offset_a_value = i32(global_K * wmma_M * warp_y + wmma_K * tile_k);
                        Expr offset_b_value = i32(global_N * wmma_K * tile_k + wmma_N * warp_x);

                        Expr offset_a_var = Variable::make(Int(32), "offset_a");
                        Expr offset_b_var = Variable::make(Int(32), "offset_b");
                        Expr offset_c_var = Variable::make(Int(32), "offset_c");

                        Expr load_frag_a_call = Call::make(Float(16, 16), "wmma.m16n16k16.load.a.row", {var_a, offset_a_var, stride_a}, Call::Intrinsic);
                        Expr load_frag_b_call = Call::make(Float(16, 16), "wmma.m16n16k16.load.b.row", {var_b, offset_b_var, stride_b}, Call::Intrinsic);
                        Expr load_frag_c_call = Call::make(Float(32, 8), "wmma.m16n16k16.load.c.row", {var_c, offset_c_var, stride_c}, Call::Intrinsic);

                        Expr var_frag_a = Variable::make(Float(16, 16), "frag_a");
                        Expr var_frag_b = Variable::make(Float(16, 16), "frag_b");
                        Expr var_frag_c = Variable::make(Float(32, 8), "frag_c");

                        Expr frac_c_index = Ramp::make(make_zero(Int(32)), 1, 8);
                        Expr load_frag_c = Load::make(Float(32, 8), "frag_c", frac_c_index, Buffer<>(), Parameter(), const_true(8), ModulusRemainder{});

                        Expr mma = Call::make(Float(32, 8), "wmma.m16n16k16.mma.row.row", {var_frag_a, var_frag_b, load_frag_c}, Call::Intrinsic);
                        Expr store_frag_c = Call::make(Handle(), "wmma.m16n16k16.store.d.row", {var_c, offset_c_var, stride_c, load_frag_c}, Call::Intrinsic);

                        Expr bounds_checking_tile = row_a_var < global_M &&
                                                    col_a_var < global_K &&
                                                    row_b_var < global_K &&
                                                    col_b_var < global_N;

                        // clang-format off
                        Stmt mma_for =
                            For::make("tile_k", 0, num_tiles_k, ForType::Unrolled, loop->device_api,
                                LetStmt::make("row_a", row_a_value,
                                    LetStmt::make("col_a", col_a_value,
                                        LetStmt::make("row_b", row_b_value,
                                            LetStmt::make("col_b", col_b_value,
                                                IfThenElse::make(bounds_checking_tile,
                                                    LetStmt::make("offset_a", offset_a_value,
                                                        LetStmt::make("offset_b", offset_b_value,
                                                            LetStmt::make("frag_a", load_frag_a_call,
                                                                LetStmt::make("frag_b", load_frag_b_call,
                                                                    Store::make("frag_c", mma, frac_c_index, Parameter{}, const_true(8), ModulusRemainder{})
                                                                )
                                                            )
                                                        )
                                                    )
                                               )
                                            )
                                       )
                                   )
                                )
                            );

                        Expr bounds_checking_store = col_c_var < global_N &&
                                                     row_c_var < global_M;

                        Stmt wmma_op =
                            LetStmt::make("offset_c", offset_c_value,
                                Allocate::make("frag_c", Float(32, 8), MemoryType::Stack, {make_one(Int(32))}, const_true(8),
                                    Block::make({
                                        Store::make("frag_c", load_frag_c_call, frac_c_index, Parameter{}, const_true(8), ModulusRemainder{}),
                                        mma_for,
                                        LetStmt::make("row_c", row_c_value,
                                            LetStmt::make("col_c", col_c_value,
                                                IfThenElse::make(bounds_checking_store,
                                                    Evaluate::make(store_frag_c)
                                                )
                                            )
                                        )
                                    })
                                )
                            );
                        // clang-format on
                        Stmt tiled_for = unroll_loops(wmma_op);

#endif  // INLINE_TILE_LOOP

                        tensorcore_op_found = true;

                        return tiled_for;
                    }
                }
            }
        }
    }

    Stmt s = IRMutator::visit(loop);

    if (tensorcore_op_found) {
        // We have a tensorcore loop, now calculate the correct number of blocks/threads
        // required to perform the matrix multiplies

        const bool is_gpu_var = CodeGen_GPU_Dev::is_gpu_var(loop->name);
        if (is_gpu_var) {
            Expr num_tiles_x = global_N / wmma_N;
            Expr num_tiles_y = global_M / wmma_M;

            // TODO: This will effectively launch 1 block for each 16x16 tile of the input matrix.
            //       This works but its probably not very efficient.
            //       Need to find a way to calculate the maximum possible block size to maximize
            //       stream multiprocessors load.
            //       Doing gives almost 5x speedup compared to a regular CUDA matrix multiply, but
            //       it can be improved even further
            Expr max_threads_x = i32(4);
            Expr max_threads_y = i32(4);

            Expr num_threads_x = min(max_threads_x, num_tiles_x) * warp_size;
            Expr num_threads_y = min(max_threads_y, num_tiles_y);

            Expr num_blocks_x = (global_N + (wmma_N * num_threads_x / warp_size - 1)) / (wmma_N * num_threads_x / warp_size);
            Expr num_blocks_y = (global_M + wmma_M * num_threads_y - 1) / (wmma_N * num_threads_y);

            const For *for_loop = s.as<For>();

            Expr new_extent;
            if (ends_with(for_loop->name, ".__block_id_y")) {
                new_extent = num_blocks_y;
            } else if (ends_with(for_loop->name, ".__block_id_x")) {
                new_extent = num_blocks_x;
            } else if (ends_with(for_loop->name, ".__thread_id_y")) {
                new_extent = num_threads_y;
            } else if (ends_with(for_loop->name, ".__thread_id_x")) {
                new_extent = num_threads_x;
            }

            s = For::make(for_loop->name, for_loop->min, new_extent, for_loop->for_type, for_loop->device_api, for_loop->body);
        }
    }

    return s;
}

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
