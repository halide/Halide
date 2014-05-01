#include "CodeGen_PTX_Dev.h"
#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "Target.h"
#include "LLVM_Headers.h"

// This is declared in NVPTX.h, which is not exported. Ugly, but seems better than
// hardcoding a path to the .h file.
#if WITH_PTX
namespace llvm { ModulePass *createNVVMReflectPass(const StringMap<int>& Mapping); }
#endif

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_PTX_Dev::CodeGen_PTX_Dev(Target host) : CodeGen(host) {
    #if !(WITH_PTX)
    user_error << "ptx not enabled for this build of Halide.\n";
    #endif
    user_assert(llvm_NVPTX_enabled) << "llvm build not configured with nvptx target enabled\n.";
}

void CodeGen_PTX_Dev::add_kernel(Stmt stmt, std::string name, const std::vector<Argument> &args) {

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
    function_name = name;
    FunctionType *func_t = FunctionType::get(void_t, arg_types, false);
    function = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module);

    // Mark the buffer args as no alias
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            function->setDoesNotAlias(i+1);
        }
    }


    // Make the initial basic block
    entry_block = BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entry_block);

    // Put the arguments in the symbol table
    vector<string> arg_sym_names;
    {
        size_t i = 0;
        for (llvm::Function::arg_iterator iter = function->arg_begin();
             iter != function->arg_end();
             iter++) {

            string arg_sym_name = args[i].name;
            if (args[i].is_buffer) {
                // HACK: codegen expects a load from foo to use base
                // address 'foo.host', so we store the device pointer
                // as foo.host in this scope.
                arg_sym_name += ".host";
            }
            sym_push(arg_sym_name, iter);
            iter->setName(arg_sym_name);
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
    MDNode *mdNode = MDNode::get(*context, vec<Value *>(function,
                                                        MDString::get(*context, "kernel"),
                                                        ConstantInt::get(i32, 1)));
    module->getOrInsertNamedMetadata("nvvm.annotations")->addOperand(mdNode);


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

    CodeGen::init_module();

    #if WITH_PTX
    module = get_initial_module_for_ptx_device(context);
    #endif

    owns_module = true;
}

string CodeGen_PTX_Dev::simt_intrinsic(const string &name) {
    if (ends_with(name, ".threadidx")) {
        return "llvm.nvvm.read.ptx.sreg.tid.x";
    } else if (ends_with(name, ".threadidy")) {
        return "llvm.nvvm.read.ptx.sreg.tid.y";
    } else if (ends_with(name, ".threadidz")) {
        return "llvm.nvvm.read.ptx.sreg.tid.z";
    } else if (ends_with(name, ".threadidw")) {
        return "llvm.nvvm.read.ptx.sreg.tid.w";
    } else if (ends_with(name, ".blockidx")) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.x";
    } else if (ends_with(name, ".blockidy")) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.y";
    } else if (ends_with(name, ".blockidz")) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.z";
    } else if (ends_with(name, ".blockidw")) {
        return "llvm.nvvm.read.ptx.sreg.ctaid.w";
    }
    internal_error << "simt_intrinsic called on bad variable name\n";
    return "";
}

void CodeGen_PTX_Dev::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        debug(2) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";
        internal_assert(loop->for_type == For::Parallel) << "kernel loop must be parallel\n";

        Expr simt_idx = Call::make(Int(32), simt_intrinsic(loop->name), std::vector<Expr>(), Call::Extern);
        Expr loop_var = loop->min + simt_idx;
        Expr cond = simt_idx < loop->extent;
        debug(3) << "for -> if (" << cond << ")\n";

        BasicBlock *loop_bb = BasicBlock::Create(*context, loop->name + "_loop", function);
        BasicBlock *after_bb = BasicBlock::Create(*context, loop->name + "_after_loop", function);

        builder->CreateCondBr(codegen(cond), loop_bb, after_bb);
        builder->SetInsertPoint(loop_bb);

        sym_push(loop->name, codegen(loop_var));
        codegen(loop->body);
        sym_pop(loop->name);

        builder->CreateBr(after_bb);
        builder->SetInsertPoint(after_bb);
    } else {
        CodeGen::visit(loop);
    }
}

void CodeGen_PTX_Dev::visit(const Pipeline *n) {
    n->produce.accept(this);

    // Grab the syncthreads intrinsic, or declare it if it doesn't exist yet
    llvm::Function *syncthreads = module->getFunction("llvm.nvvm.barrier0");
    if (!syncthreads) {
        FunctionType *func_t = FunctionType::get(llvm::Type::getVoidTy(*context), vector<llvm::Type *>(), false);
        syncthreads = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, "llvm.nvvm.barrier0", module);
        syncthreads->setCallingConv(CallingConv::C);
        debug(2) << "Declaring syncthreads intrinsic\n";
    }

    if (n->update.defined()) {
        // If we're producing into shared or global memory we need a
        // syncthreads before continuing.
        builder->CreateCall(syncthreads, std::vector<Value *>());
        n->update.accept(this);
    }

    builder->CreateCall(syncthreads, std::vector<Value *>());
    n->consume.accept(this);
}

void CodeGen_PTX_Dev::visit(const Allocate *alloc) {

    debug(1) << "Allocate " << alloc->name << " on device\n";

    llvm::Type *llvm_type = llvm_type_of(alloc->type);

    string allocation_name = alloc->name + ".host";
    debug(3) << "Pushing allocation called " << allocation_name << " onto the symbol table\n";

    // If this is a shared allocation, there should already be a
    // pointer into shared memory in the symbol table.
    Value *ptr;
    Value *offset = sym_get(alloc->name + ".shared_mem", false);

    if (offset) {
        // Bit-cast it to a shared memory pointer (address-space 3 is shared memory)
        ptr = builder->CreateIntToPtr(offset, PointerType::get(llvm_type, 3));
    } else {
        // Otherwise jump back to the entry and generate an
        // alloca. Note that by jumping back we're rendering any
        // expression we carry back meaningless, so we had better only
        // be dealing with constants here.
        int32_t size = 0;
        bool is_constant = constant_allocation_size(alloc->extents, allocation_name, size);
        user_assert(is_constant)
            << "Allocation " << alloc->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        BasicBlock *here = builder->GetInsertBlock();

        builder->SetInsertPoint(entry_block);
        ptr = builder->CreateAlloca(llvm_type_of(alloc->type), ConstantInt::get(i32, size));
        builder->SetInsertPoint(here);
    }

    sym_push(allocation_name, ptr);
    codegen(alloc->body);
}

void CodeGen_PTX_Dev::visit(const Free *f) {
    sym_pop(f->name + ".host");
}

string CodeGen_PTX_Dev::march() const {
    return "nvptx64";
}

string CodeGen_PTX_Dev::mcpu() const {
    return "sm_20";
}

string CodeGen_PTX_Dev::mattrs() const {
    return "";
}

bool CodeGen_PTX_Dev::use_soft_float_abi() const {
    return false;
}

vector<char> CodeGen_PTX_Dev::compile_to_src() {

    #if WITH_PTX

    debug(2) << "In CodeGen_PTX_Dev::compile_to_src";

    optimize_module();

    // DISABLED - hooked in here to force PrintBeforeAll option - seems to be the only way?
    /*char* argv[] = { "llc", "-print-before-all" };*/
    /*int argc = sizeof(argv)/sizeof(char*);*/
    /*cl::ParseCommandLineOptions(argc, argv, "Halide PTX internal compiler\n");*/

    // Generic llvm optimizations on the module.
    optimize_module();

    // Set up TargetTriple
    module->setTargetTriple(Triple::normalize(march()+"--"));
    Triple TheTriple(module->getTargetTriple());

    // Allocate target machine
    const std::string MArch = march();
    const std::string MCPU = mcpu();
    const llvm::Target* TheTarget = 0;

    std::string errStr;
    TheTarget = TargetRegistry::lookupTarget(TheTriple.getTriple(), errStr);
    internal_assert(TheTarget);

    TargetOptions Options;
    Options.LessPreciseFPMADOption = true;
    Options.PrintMachineCode = false;
    Options.NoFramePointerElim = false;
    //Options.NoExcessFPPrecision = false;
    Options.AllowFPOpFusion = FPOpFusion::Fast;
    Options.UnsafeFPMath = true;
    Options.NoInfsFPMath = false;
    Options.NoNaNsFPMath = false;
    Options.HonorSignDependentRoundingFPMathOption = false;
    Options.UseSoftFloat = false;
    /* if (FloatABIForCalls != FloatABI::Default) */
        /* Options.FloatABIType = FloatABIForCalls; */
    Options.NoZerosInBSS = false;
    #if LLVM_VERSION < 33
    Options.JITExceptionHandling = false;
    #endif
    Options.JITEmitDebugInfo = false;
    Options.JITEmitDebugInfoToDisk = false;
    Options.GuaranteedTailCallOpt = false;
    Options.StackAlignmentOverride = 0;
    // Options.DisableJumpTables = false;
    Options.TrapFuncName = "";

    CodeGenOpt::Level OLvl = CodeGenOpt::Aggressive;

    const std::string FeaturesStr = "";
    std::auto_ptr<TargetMachine>
        target(TheTarget->createTargetMachine(TheTriple.getTriple(),
                                              MCPU, FeaturesStr, Options,
                                              llvm::Reloc::Default,
                                              llvm::CodeModel::Default,
                                              OLvl));
    internal_assert(target.get()) << "Could not allocate target machine!";
    TargetMachine &Target = *target.get();

    // Set up passes
    PassManager PM;

    TargetLibraryInfo *TLI = new TargetLibraryInfo(TheTriple);
    PM.add(TLI);

    if (target.get()) {
        #if LLVM_VERSION < 33
        PM.add(new TargetTransformInfo(target->getScalarTargetTransformInfo(),
                                       target->getVectorTargetTransformInfo()));
        #else
        target->addAnalysisPasses(PM);
        #endif
    }

    // Add the target data from the target machine, if it exists, or the module.
    #if LLVM_VERSION < 35
    if (const DataLayout *TD = Target.getDataLayout()) {
        PM.add(new DataLayout(*TD));
    } else {
        PM.add(new DataLayout(module));
    }
    #else
    // FIXME: This doesn't actually do the job. Now that
    // DataLayoutPass is gone, I have no idea how to get this to work.
    if (const DataLayout *TD = Target.getDataLayout()) {
        module->setDataLayout(TD);
    }
    PM.add(new DataLayoutPass(module));
    #endif

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
    #define kDefaultDenorms 0
    #define kFTZDenorms     1

    StringMap<int> reflect_mapping;
    reflect_mapping[StringRef("__CUDA_FTZ")] = kFTZDenorms;
    PM.add(createNVVMReflectPass(reflect_mapping));

    // Inlining functions is essential to PTX
    PM.add(createAlwaysInlinerPass());

    // Override default to generate verbose assembly.
    Target.setAsmVerbosityDefault(true);

    // Output string stream
    std::string outstr;
    raw_string_ostream outs(outstr);
    formatted_raw_ostream ostream(outs);

    // Ask the target to add backend passes as necessary.
    bool fail = Target.addPassesToEmitFile(PM, ostream,
                                           TargetMachine::CGFT_AssemblyFile,
                                           true);
    if (fail) {
        internal_error << "Failed to set up passes to emit PTX source\n";
    }

    PM.run(*module);

    ostream.flush();

    if (debug::debug_level >= 2) {
        module->dump();
    }
    debug(2) << "Done with CodeGen_PTX_Dev::compile_to_src";


    string str = outs.str();
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
#else // WITH_PTX
    return vector<char>();
#endif
}


string CodeGen_PTX_Dev::get_current_kernel_name() {
    return function->getName();
}

void CodeGen_PTX_Dev::dump() {
    module->dump();
}

}}
