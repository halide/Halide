#include "CodeGen_PTX_Dev.h"
#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "Target.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"

// This is declared in NVPTX.h, which is not exported. Ugly, but seems better than
// hardcoding a path to the .h file.
#ifdef WITH_PTX
namespace llvm { FunctionPass *createNVVMReflectPass(const StringMap<int>& Mapping); }
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

    context = new llvm::LLVMContext();
}

CodeGen_PTX_Dev::~CodeGen_PTX_Dev() {
    // This is required as destroying the context before the module
    // results in a crash. Really, reponsbility for destruction
    // should be entirely in the parent class.
    // TODO: Figure out how to better manage the context -- e.g. allow using
    // same one as the host.
    module.reset();
    delete context;
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
            #if LLVM_VERSION < 50
            function->setDoesNotAlias(i+1);
            #else
            function->addParamAttr(i, Attribute::NoAlias);
            #endif
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
        llvm::ValueAsMetadata::get(ConstantInt::get(i32_t, 1))
    };

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

    #ifdef WITH_PTX
    module = get_initial_module_for_ptx_device(target, context);
    #endif
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
    user_assert(!alloc->new_expr.defined()) << "Allocate node inside PTX kernel has custom new expression.\n" <<
        "(Memoization is not supported inside GPU kernels at present.)\n";

    if (alloc->name == "__shared") {
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
        int32_t size = CodeGen_GPU_Dev::get_constant_bound_allocation_size(alloc);
        user_assert(size > 0)
            << "Allocation " << alloc->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

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

string CodeGen_PTX_Dev::march() const {
    return "nvptx64";
}

string CodeGen_PTX_Dev::mcpu() const {
    if (target.has_feature(Target::CUDACapability61)) {
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
    if (target.has_feature(Target::CUDACapability61)) {
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

    #ifdef WITH_PTX

    debug(2) << "In CodeGen_PTX_Dev::compile_to_src";

    // DISABLED - hooked in here to force PrintBeforeAll option - seems to be the only way?
    /*char* argv[] = { "llc", "-print-before-all" };*/
    /*int argc = sizeof(argv)/sizeof(char*);*/
    /*cl::ParseCommandLineOptions(argc, argv, "Halide PTX internal compiler\n");*/

    llvm::Triple triple(module->getTargetTriple());

    // Allocate target machine

    std::string err_str;
    const llvm::Target *target = TargetRegistry::lookupTarget(triple.str(), err_str);
    internal_assert(target) << err_str << "\n";

    TargetOptions options;
    #if LLVM_VERSION < 50
    options.LessPreciseFPMADOption = true;
    #endif
    options.PrintMachineCode = false;
    options.AllowFPOpFusion = FPOpFusion::Fast;
    options.UnsafeFPMath = true;
    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.StackAlignmentOverride = 0;

    std::unique_ptr<TargetMachine>
        target_machine(target->createTargetMachine(triple.str(),
                                                   mcpu(), mattrs(), options,
                                                   llvm::Reloc::PIC_,
#if LLVM_VERSION < 60
                                                   llvm::CodeModel::Default,
#else
                                                   llvm::CodeModel::Small,
#endif
                                                   CodeGenOpt::Aggressive));

    internal_assert(target_machine.get()) << "Could not allocate target machine!";

    #if LLVM_VERSION >= 60
    module->setDataLayout(target_machine->createDataLayout());
    #endif

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
    #define kDefaultDenorms 0
    #define kFTZDenorms     1

    #if LLVM_VERSION <= 40
    StringMap<int> reflect_mapping;
    reflect_mapping[StringRef("__CUDA_FTZ")] = kFTZDenorms;
    module_pass_manager.add(createNVVMReflectPass(reflect_mapping));
    #else
    // Insert a module flag for the FTZ handling.
    module->addModuleFlag(llvm::Module::Override, "nvvm-reflect-ftz",
                          kFTZDenorms);

    if (kFTZDenorms) {
        for (llvm::Function &fn : *module) {
            fn.addFnAttr("nvptx-f32ftz", "true");
        }
    }
    #endif

    PassManagerBuilder b;
    b.OptLevel = 3;
#if LLVM_VERSION >= 50
    b.Inliner = createFunctionInliningPass(b.OptLevel, 0, false);
#else
    b.Inliner = createFunctionInliningPass(b.OptLevel, 0);
#endif
    b.LoopVectorize = true;
    b.SLPVectorize = true;

    #if LLVM_VERSION > 40
    target_machine->adjustPassManager(b);
    #endif

    b.populateFunctionPassManager(function_pass_manager);
    b.populateModulePassManager(module_pass_manager);

    // Override default to generate verbose assembly.
    target_machine->Options.MCOptions.AsmVerbose = true;

    // Output string stream

    // Ask the target to add backend passes as necessary.
    bool fail = target_machine->addPassesToEmitFile(module_pass_manager, ostream,
                                                    TargetMachine::CGFT_AssemblyFile,
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

    debug(1) << "PTX kernel:\n" << outstr.c_str() << "\n";
    vector<char> buffer(outstr.begin(), outstr.end());
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
    #if LLVM_VERSION >= 50
    module->print(dbgs(), nullptr, false, true);
    #else
    module->dump();
    #endif
}

std::string CodeGen_PTX_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}}
