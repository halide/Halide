#include "CodeGen_PTX_Dev.h"
#include "IROperator.h"
#include <iostream>
#include "buffer_t.h"
#include "IRPrinter.h"
#include "IRMatch.h"
#include "Log.h"
#include "Util.h"
#include "Var.h"
#include "Param.h"
#include "integer_division_table.h"
#include "LLVM_Headers.h"

#if WITH_PTX
extern "C" unsigned char halide_internal_initmod_ptx_dev[];
extern "C" int halide_internal_initmod_ptx_dev_length;
#else
static void *halide_internal_initmod_ptx_dev = 0;
static int halide_internal_initmod_ptx_dev_length = 0;
#endif

namespace Halide { 
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_PTX_Dev::CodeGen_PTX_Dev()
    : CodeGen()
{
    assert(llvm_NVPTX_enabled && "llvm build not configured with nvptx target enabled.");
}

void CodeGen_PTX_Dev::compile(Stmt stmt, std::string name, const std::vector<Argument> &args) {
    owns_module = true;

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
    {
        size_t i = 0;
        for (llvm::Function::arg_iterator iter = function->arg_begin();
             iter != function->arg_end();
             iter++) {                        

            if (args[i].is_buffer) {
                // HACK: codegen expects a load from foo to use base
                // address 'foo.host', so we store the device pointer
                // as foo.host in this scope.
                sym_push(args[i].name + ".host", iter); 
            } else {
                sym_push(args[i].name, iter);
            }
            iter->setName(args[i].name);

            i++;
        }
    }

    // We won't end the entry block yet, because we'll want to add
    // some allocas to it later if there are local allocations. Start
    // a new block to put all the code.
    BasicBlock *body_block = BasicBlock::Create(*context, "body", function);
    builder->SetInsertPoint(body_block);

    log(1) << "Generating llvm bitcode...\n";
    // Ok, we have a module, function, context, and a builder
    // pointing at a brand new basic block. We're good to go.
    stmt.accept(this);

    // Now we need to end the function
    builder->CreateRetVoid();

    // Make the entry block point to the body block
    builder->SetInsertPoint(entry_block);
    builder->CreateBr(body_block);

    // Add the nvvm annotation that it is a kernel function. 
    MDNode *mdNode = MDNode::get(*context, vec((Value*)function,
                                              (Value*)MDString::get(*context, "kernel"),
                                              (Value*)ConstantInt::get(i32, 1)));
    module->getOrInsertNamedMetadata("nvvm.annotations")->addOperand(mdNode);


    // Now verify the function is ok
    verifyFunction(*function);

    // Finally, verify the module is ok
    verifyModule(*module);
    log(2) << "Done generating llvm bitcode\n";

    // Optimize it - this really only optimizes the current function
    optimize_module();
}

void CodeGen_PTX_Dev::init_module() {

    CodeGen::init_module();

    StringRef sb;

    sb = StringRef((char *)halide_internal_initmod_ptx_dev,
                           halide_internal_initmod_ptx_dev_length);
    MemoryBuffer *bitcode_buffer = MemoryBuffer::getMemBuffer(sb);

    // Parse it
    std::string errstr;
    module = ParseBitcodeFile(bitcode_buffer, *context, &errstr);
    if (!module) {
        std::cerr << "Error parsing initial module: " << errstr << "\n";
    }
    assert(module && "llvm encountered an error in parsing a bitcode file.");
    module->setTargetTriple(Triple::normalize(march()+"--"));

    // Fix the target triple
    log(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    module->setModuleIdentifier("<halide_ptx>");

    delete bitcode_buffer;
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
    assert(false && "simt_intrinsic called on bad variable name");
}

bool CodeGen_PTX_Dev::is_simt_var(const string &name) {
    string n = base_name(name);

    log(2) << "is_simt_var " << name << " (" << n << ")? ";

    bool result = (n == "threadidx" ||
                   n == "threadidy" ||
                   n == "threadidz" ||
                   n == "threadidw" ||
                   n == "blockidx" ||
                   n == "blockidy" ||
                   n == "blockidz" ||
                   n == "blockidw");

    log(2) << result << "\n";

    return result;
}

void CodeGen_PTX_Dev::visit(const For *loop) {
    if (is_simt_var(loop->name)) {
        log(2) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";
        assert(loop->for_type == For::Parallel && "kernel loop must be parallel");

        Expr simt_idx = new Call(Int(32), simt_intrinsic(loop->name), std::vector<Expr>());
        Expr loop_var = loop->min + simt_idx;
        Expr cond = new LT(simt_idx, loop->extent);
        log(3) << "for -> if (" << cond << ")\n";

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
        log(2) << "Declaring syncthreads intrinsic\n";
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

    log(1) << "Allocate " << alloc->name << " on device\n";

    llvm::Type *llvm_type = llvm_type_of(alloc->type);

    string allocation_name = alloc->name + ".host";
    log(3) << "Pushing allocation called " << allocation_name << " onto the symbol table\n";

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
        const IntImm *size = alloc->size.as<IntImm>();
        assert(size && "Only fixed-size allocations are supported on the gpu. Try storing into shared memory instead.");

        BasicBlock *here = builder->GetInsertBlock();

        builder->SetInsertPoint(entry_block);
        ptr = builder->CreateAlloca(llvm_type_of(alloc->type), ConstantInt::get(i32, size->value));
        builder->SetInsertPoint(here);
    }


    sym_push(allocation_name, ptr);
    codegen(alloc->body);
    sym_pop(allocation_name);

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

string CodeGen_PTX_Dev::compile_to_ptx() {

    // DISABLED - hooked in here to force PrintBeforeAll option - seems to be the only way?
    /*char* argv[] = { "llc", "-print-before-all" };*/
    /*int argc = sizeof(argv)/sizeof(char*);*/
    /*cl::ParseCommandLineOptions(argc, argv, "Halide PTX internal compiler\n");*/

    // Set up TargetTriple
    module->setTargetTriple(Triple::normalize(march()+"--"));
    Triple TheTriple(module->getTargetTriple());

    // Allocate target machine
    const std::string MArch = march();
    const std::string MCPU = mcpu();
    const Target* TheTarget = 0;
    
    std::string errStr;
    TheTarget = TargetRegistry::lookupTarget(TheTriple.getTriple(), errStr);
    assert(TheTarget);

    TargetOptions Options;
    Options.LessPreciseFPMADOption = true;
    Options.PrintMachineCode = false;
    Options.NoFramePointerElim = false;
    Options.NoFramePointerElimNonLeaf = false;
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
    #if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
    Options.JITExceptionHandling = false;
    #endif
    Options.JITEmitDebugInfo = false;
    Options.JITEmitDebugInfoToDisk = false;
    Options.GuaranteedTailCallOpt = false;
    Options.StackAlignmentOverride = 0;
    Options.RealignStack = true;
    // Options.DisableJumpTables = false;
    Options.TrapFuncName = "";
    Options.EnableSegmentedStacks = false;

    CodeGenOpt::Level OLvl = CodeGenOpt::Default;

    const std::string FeaturesStr = "";
    std::auto_ptr<TargetMachine>
        target(TheTarget->createTargetMachine(TheTriple.getTriple(),
                                              MCPU, FeaturesStr, Options,
                                              llvm::Reloc::Default,
                                              llvm::CodeModel::Default,
                                              OLvl));
    assert(target.get() && "Could not allocate target machine!");
    TargetMachine &Target = *target.get();

    // Set up passes
    PassManager PM;

    TargetLibraryInfo *TLI = new TargetLibraryInfo(TheTriple);
    PM.add(TLI);

    if (target.get()) {
        #if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
        PM.add(new TargetTransformInfo(target->getScalarTargetTransformInfo(),
                                       target->getVectorTargetTransformInfo()));
        #else
        target->addAnalysisPasses(PM);
        #endif
    }

    // Add the target data from the target machine, if it exists, or the module.
    if (const DataLayout *TD = Target.getDataLayout())
        PM.add(new DataLayout(*TD));
    else
        PM.add(new DataLayout(module));

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
        log(0) << "Failed to set up passes to emit PTX source\n";
        assert(false);
    }

    PM.run(*module);

    ostream.flush();
    return outs.str();
}

}}
