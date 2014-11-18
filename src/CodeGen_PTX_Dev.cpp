#include "CodeGen_PTX_Dev.h"
#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "Target.h"
#include "LLVM_Headers.h"

// This is declared in NVPTX.h, which is not exported. Ugly, but seems better than
// hardcoding a path to the .h file.
#ifdef WITH_PTX
namespace llvm { ModulePass *createNVVMReflectPass(const StringMap<int>& Mapping); }
#endif

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_PTX_Dev::CodeGen_PTX_Dev(Target host) : CodeGen_LLVM(host) {
    #if !(WITH_PTX)
    user_error << "ptx not enabled for this build of Halide.\n";
    #endif
    user_assert(llvm_NVPTX_enabled) << "llvm build not configured with nvptx target enabled\n.";
}

void CodeGen_PTX_Dev::add_kernel(Stmt stmt,
                                 const std::string &name,
                                 const std::vector<GPU_Argument> &args) {

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
    MDNode *mdNode = MDNode::get(*context, vec<LLVMMDNodeArgumentType>(value_as_metadata_type(function),
                                                                       MDString::get(*context, "kernel"),
                                                                       value_as_metadata_type(ConstantInt::get(i32, 1))));

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

    CodeGen_LLVM::init_module();

    #ifdef WITH_PTX
    module = get_initial_module_for_ptx_device(target, context);
    #endif

    owns_module = true;
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
    }
    internal_error << "simt_intrinsic called on bad variable name\n";
    return "";
}

void CodeGen_PTX_Dev::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        Expr simt_idx = Call::make(Int(32), simt_intrinsic(loop->name), std::vector<Expr>(), Call::Extern);
        internal_assert(is_zero(loop->min));
        sym_push(loop->name, codegen(simt_idx));
        codegen(loop->body);
        sym_pop(loop->name);
    } else {
        CodeGen_LLVM::visit(loop);
    }
}

void CodeGen_PTX_Dev::visit(const Allocate *alloc) {

    if (alloc->name == "__shared") {
        // PTX uses zero in address space 3 as the base address for shared memory
        Value *shared_base = Constant::getNullValue(PointerType::get(i8, 3));
        sym_push(alloc->name + ".host", shared_base);
    } else {

        debug(2) << "Allocate " << alloc->name << " on device\n";

        string allocation_name = alloc->name + ".host";
        debug(3) << "Pushing allocation called " << allocation_name << " onto the symbol table\n";

        // Jump back to the entry and generate an alloca. Note that by
        // jumping back we're rendering any expression we carry back
        // meaningless, so we had better only be dealing with
        // constants here.
        int32_t size = 0;
        bool is_constant = constant_allocation_size(alloc->extents, allocation_name, size);
        user_assert(is_constant)
            << "Allocation " << alloc->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        BasicBlock *here = builder->GetInsertBlock();

        builder->SetInsertPoint(entry_block);
        Value *ptr = builder->CreateAlloca(llvm_type_of(alloc->type), ConstantInt::get(i32, size));
        builder->SetInsertPoint(here);
        sym_push(allocation_name, ptr);
    }
    codegen(alloc->body);
}

void CodeGen_PTX_Dev::visit(const Free *f) {
    sym_pop(f->name + ".host");
}

string CodeGen_PTX_Dev::march() const {
    return "nvptx64";
}

string CodeGen_PTX_Dev::mcpu() const {
    if (target.has_feature(Target::CUDACapability50)) {
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
    if (target.features_any_of(vec(Target::CUDACapability32,
                                   Target::CUDACapability50))) {
        // Need ptx isa 4.0. llvm < 3.5 doesn't support it.
        #if LLVM_VERSION < 35
        user_error << "This version of Halide was linked against llvm 3.4 or earlier, "
                   << "which does not support cuda compute capability 3.2 or 5.0\n";
        return "";
        #else
        return "+ptx40";
        #endif
    } else {
        // Use the default. For llvm 3.5 it's ptx 3.2.
        return "";
    }

}

bool CodeGen_PTX_Dev::use_soft_float_abi() const {
    return false;
}

llvm::Triple CodeGen_PTX_Dev::get_target_triple() const {
    return Triple(Triple::normalize(march() + "--"));
}

vector<char> CodeGen_PTX_Dev::compile_to_src() {

    #ifdef WITH_PTX

    debug(2) << "In CodeGen_PTX_Dev::compile_to_src";

    // DISABLED - hooked in here to force PrintBeforeAll option - seems to be the only way?
    /*char* argv[] = { "llc", "-print-before-all" };*/
    /*int argc = sizeof(argv)/sizeof(char*);*/
    /*cl::ParseCommandLineOptions(argc, argv, "Halide PTX internal compiler\n");*/

    // Generic llvm optimizations on the module.
    optimize_module();

    // Set up TargetTriple
    Triple TheTriple = get_target_triple();
    module->setTargetTriple(TheTriple.str());

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

    const std::string FeaturesStr = mattrs();
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

    #if LLVM_VERSION < 37
    PM.add(new TargetLibraryInfo(TheTriple));
    #else
    PM.add(new TargetLibraryInfoWrapperPass(TheTriple));
    #endif

    if (target.get()) {
        #if LLVM_VERSION < 33
        PM.add(new TargetTransformInfo(target->getScalarTargetTransformInfo(),
                                       target->getVectorTargetTransformInfo()));
        #elif LLVM_VERSION < 37
        target->addAnalysisPasses(PM);
        #endif
    }

    #if LLVM_VERSION == 36
    const DataLayout *TD = Target.getSubtargetImpl()->getDataLayout();
    #else
    const DataLayout *TD = Target.getDataLayout();
    #endif

    #if LLVM_VERSION < 35
    if (TD) {
        PM.add(new DataLayout(*TD));
    } else {
        PM.add(new DataLayout(module));
    }
    #else
    if (TD) {
        module->setDataLayout(TD);
    }
    #if LLVM_VERSION == 35
    PM.add(new DataLayoutPass(module));
    #else // llvm >= 3.6
    PM.add(new DataLayoutPass);
    #endif
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
    debug(1) << "PTX kernel:\n" << str.c_str() << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
#else // WITH_PTX
    return vector<char>();
#endif
}

int CodeGen_PTX_Dev::native_vector_bits() const {
    // PTX doesn't really do vectorization. The widest type is a double.
    return 64;
}

string CodeGen_PTX_Dev::get_current_kernel_name() {
    return function->getName();
}

void CodeGen_PTX_Dev::dump() {
    module->dump();
}

std::string CodeGen_PTX_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}}
