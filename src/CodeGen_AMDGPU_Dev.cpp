#include "CodeGen_AMDGPU_Dev.h"
#include "CodeGen_Internal.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "Debug.h"
#include "Simplify.h"
#include "Solve.h"
#include "Target.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"

#include <fstream>

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_AMDGPU_Dev::CodeGen_AMDGPU_Dev(Target host) : CodeGen_LLVM(host) {
    #if !(WITH_AMDGPU)
    user_error << "amdgpu not enabled for this build of Halide.\n";
    #endif
    user_assert(llvm_AMDGPU_enabled) << "llvm build not configured with amdgpu target enabled\n.";
    context = new llvm::LLVMContext();
}

CodeGen_AMDGPU_Dev::~CodeGen_AMDGPU_Dev() {
    // This is required as destroying the context before the module
    // results in a crash. Really, responsibility for destruction
    // should be entirely in the parent class.
    // TODO: Figure out how to better manage the context -- e.g. allow using
    // same one as the host.
    module.reset();
    delete context;
}

void CodeGen_AMDGPU_Dev::add_kernel(Stmt stmt,
                                 const std::string &name,
                                 const std::vector<DeviceArgument> &args) {
    debug(1)<<"Calling CodeGen_AMDGPU_Dev::add_kernel"<<"\n";

    internal_assert(module != nullptr);


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
    function->setCallingConv(llvm::CallingConv::AMDGPU_KERNEL);
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

    // Get the alignment of the integer arguments
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].alignment.modulus) {
            alignment_info.push(args[i].name, args[i].alignment);
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

    debug(2) << "Done generating llvm bitcode for AMDGPU\n";

    // Clear the symbol table
    for (size_t i = 0; i < arg_sym_names.size(); i++) {
        sym_pop(arg_sym_names[i]);
    }
}

void CodeGen_AMDGPU_Dev::init_module() {
    init_context();
    debug(1) << "Inside CodeGen_AMDGPU_Dev::init_module"<<"\n";
    #ifdef WITH_AMDGPU
    module = get_initial_module_for_amdgpu_device(target, context);
    #endif
}

string CodeGen_AMDGPU_Dev::simt_intrinsic(const string &name) {
    debug(1) << "Inside CodeGen_AMDGPU_Dev::simt_intrinsic"<<"\n";
    if (ends_with(name, ".__thread_id_x")) {
        return "llvm.amdgcn.workitem.id.x";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "llvm.amdgcn.workitem.id.y";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "llvm.amdgcn.workitem.id.z";
    } else if (ends_with(name, ".__thread_id_w")) {
        return "llvm.amdgcn.workitem.id.w";
    } else if (ends_with(name, ".__block_id_x")) {
        return "llvm.amdgcn.workgroup.id.x";
    } else if (ends_with(name, ".__block_id_y")) {
        return "llvm.amdgcn.workgroup.id.y";
    } else if (ends_with(name, ".__block_id_z")) {
        return "llvm.amdgcn.workgroup.id.z";
    } else if (ends_with(name, ".__block_id_w")) {
        return "llvm.amdgcn.workgroup.id.w";
    }
    internal_error << "simt_intrinsic called on bad variable name\n";
    return "";
}

void CodeGen_AMDGPU_Dev::visit(const For *loop) {
    debug(1) << "Inside CodeGen_AMDGPU_Dev::visit"<<"\n";
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

void CodeGen_AMDGPU_Dev::visit(const Allocate *alloc) {
    debug(1) << "Inside CodeGen_AMDGPU_Dev::visit"<<"\n";
    user_assert(!alloc->new_expr.defined()) << "Allocate node inside AMDGPU kernel has custom new expression.\n" <<
        "(Memoization is not supported inside GPU kernels at present.)\n";
    if (alloc->name == "__shared") {
        // AMDGPU uses zero in address space 3 as the base address for shared memory
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

void CodeGen_AMDGPU_Dev::visit(const Free *f) {
    sym_pop(f->name);
}

void CodeGen_AMDGPU_Dev::visit(const AssertStmt *op) {
    // Discard the error message for now.
    Expr trap = Call::make(Int(32), "halide_ptx_trap", {}, Call::Extern);
    codegen(IfThenElse::make(!op->condition, Evaluate::make(trap)));
}

void CodeGen_AMDGPU_Dev::visit(const Load *op) {

    // Do aligned 4-wide 32-bit loads as a single i128 load.
    const Ramp *r = op->index.as<Ramp>();
    // TODO: lanes >= 4, not lanes == 4
    if (is_one(op->predicate) && r && is_one(r->stride) && r->lanes == 4 && op->type.bits() == 32) {
        ModulusRemainder align = modulus_remainder(r->base, alignment_info);
        if (align.modulus % 4 == 0 && align.remainder % 4 == 0) {
            Expr index = simplify(r->base / 4);
            Expr equiv = Load::make(UInt(128), op->name, index,
                                    op->image, op->param, const_true());
            equiv = reinterpret(op->type, equiv);
            codegen(equiv);
            return;
        }
    }

    CodeGen_LLVM::visit(op);
}

void CodeGen_AMDGPU_Dev::visit(const Store *op) {
    CodeGen_LLVM::visit(op);
}

string CodeGen_AMDGPU_Dev::march() const {
    return "amdgcn";
}

string CodeGen_AMDGPU_Dev::mcpu() const {
    if (target.has_feature(Target::AMDGPUGFX900)) {
        return "gfx900";
    }
    return "gfx803";
}

string CodeGen_AMDGPU_Dev::mattrs() const {
    // TODO: Revisit adityaatluri
    return "";
}

bool CodeGen_AMDGPU_Dev::use_soft_float_abi() const {
    return false;
}

vector<char> CodeGen_AMDGPU_Dev::compile_to_src() {

    #ifdef WITH_AMDGPU

    debug(2) << "In CodeGen_AMDGPU_Dev::compile_to_src";

    // DISABLED - hooked in here to force PrintBeforeAll option - seems to be the only way?
    /*char* argv[] = { "llc", "-print-before-all" };*/
    /*int argc = sizeof(argv)/sizeof(char*);*/
    /*cl::ParseCommandLineOptions(argc, argv, "Halide AMDGPU internal compiler\n");*/

    llvm::Triple triple(module->getTargetTriple());

/* TODO adityaatluri, remove after debugging
    llvm::SmallString<8> datall;
    llvm::raw_svector_ostream destll(datall);
    destll.SetUnbuffered();
    module->print(destll, nullptr);
    std::string ll(datall.begin(), datall.end());
*/
    // Allocate target machine

    std::cout<<triple.str()<<std::endl;
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
    // TODO: Revisit adityaatluri
/*
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
*/


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
        internal_error << "Failed to set up passes to emit AMDGPU source\n";
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
    debug(2) << "Done with CodeGen_AMDGPU_Dev::compile_to_src";

    debug(1) << "AMDGPU kernel:\n" << outstr.c_str() << "\n";

    vector<char> buffer(outstr.begin(), outstr.end());

    // Dump the SASS too if the cuda SDK is in the path
    if (debug::debug_level() >= 2) {
    // TODO: Revisit adityaatluri
/*
        debug(2) << "Compiling AMDGPU to SASS. Will fail if CUDA SDK is not installed (and in the path).\n";

        TemporaryFile ptx(get_current_kernel_name(), ".ptx");
        TemporaryFile sass(get_current_kernel_name(), ".sass");

        std::ofstream f(ptx.pathname());
        f.write(buffer.data(), buffer.size());
        f.close();

        string cmd = "ptxas --gpu-name " + mcpu() + " " + ptx.pathname() + " -o " + sass.pathname();
        if (system(cmd.c_str()) == 0) {
            cmd = "nvdisasm " + sass.pathname();
            int ret = system(cmd.c_str());
            (void)ret; // Don't care if it fails
        }
*/
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

    // Null-terminate the amdgpu asm source
    buffer.push_back(0);
    return buffer;
#else // WITH_AMDGPU
    return vector<char>();
#endif
}

int CodeGen_AMDGPU_Dev::native_vector_bits() const {
    // TODO: Revisit adityaatluri
    return 64;
}

string CodeGen_AMDGPU_Dev::get_current_kernel_name() {
    return function->getName();
}

void CodeGen_AMDGPU_Dev::dump() {
    #if LLVM_VERSION >= 50
    module->print(dbgs(), nullptr, false, true);
    #else
    module->dump();
    #endif
}

std::string CodeGen_AMDGPU_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}}
