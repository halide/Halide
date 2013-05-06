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

// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(push, 0)
#endif

#include <iostream>
#include <sstream>

#include <llvm/Config/config.h>

#include <llvm/Analysis/Verifier.h>
#include <llvm/ADT/Triple.h>

#include <llvm/PassManager.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetLibraryInfo.h>
#include <llvm/DataLayout.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Support/FormattedStream.h>

// Temporary affordance to compile with both llvm 3.2 and 3.3.
// Protected as at least one installation of llvm elides version macros.
#if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/TargetTransformInfo.h>
#include <llvm/DataLayout.h>
#include <llvm/IRBuilder.h>
#include <llvm/Support/IRReader.h>
typedef llvm::Attributes Attribute;
typedef llvm::Attributes::AttrVal AttrKind;
#else
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Bitcode/ReaderWriter.h>
typedef llvm::Attribute::AttrKind AttrKind;
#endif

#include <llvm/Support/MemoryBuffer.h>



// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(pop)
#endif

extern "C" unsigned char halide_internal_initmod_ptx_dev[];
extern "C" int halide_internal_initmod_ptx_dev_length;

namespace Halide { 
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_PTX_Dev::CodeGen_PTX_Dev()
    : CodeGen()
{
    assert(llvm_X86_enabled && "llvm build not configured with X86 target enabled.");
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
    BasicBlock *block = BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(block);

    // Put the arguments in the symbol table
    {
        size_t i = 0;
        for (llvm::Function::arg_iterator iter = function->arg_begin();
             iter != function->arg_end();
             iter++) {                        

            if (args[i].is_buffer) {
                sym_push(args[i].name + ".host", iter); // HACK
            } else {
                sym_push(args[i].name, iter);
            }
            iter->setName(args[i].name);

            i++;
        }
    }

    log(1) << "Generating llvm bitcode...\n";
    // Ok, we have a module, function, context, and a builder
    // pointing at a brand new basic block. We're good to go.
    stmt.accept(this);

    // Now we need to end the function
    builder->CreateRetVoid();

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

    #if 0
    if (log::debug_level >= 2) {
        module->dump();
    }
    #endif
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

bool CodeGen_PTX_Dev::is_simt_var(std::string name) {
    string n = base_name(name);

    log(0) << "is_simt_var " << name << " (" << n << ")? ";

    const string vars[] = {
        "threadidx",
        "threadidy",
        "threadidz",
        "threadidw",
        "blockidx",
        "blockidy",
        "blockidz",
        "blockidw"
    };
    const int num_vars = sizeof(vars)/sizeof(string);

    log(0) << (std::find(vars, vars+sizeof(vars), n) < vars+num_vars)  << "\n";

    return (std::find(vars, vars+sizeof(vars), n) < vars+num_vars);
}

void CodeGen_PTX_Dev::visit(const For *loop) {
    if (CodeGen_PTX_Dev::is_simt_var(loop->name)) {
        log(2) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";
        assert(loop->for_type == For::Parallel && "kernel loop must be parallel");

        sym_push(loop->name, codegen(loop->min));
        codegen(loop->body);
        sym_pop(loop->name);
    } else {
        CodeGen::visit(loop);
    }
}

void CodeGen_PTX_Dev::visit(const Pipeline *n) {
    CodeGen::visit(n);
}

void CodeGen_PTX_Dev::visit(const Allocate *alloc) {

    int bytes_per_element = alloc->type.bits / 8;
    int stack_size = 0;
    bool on_stack = false;
    if (const IntImm *size = alloc->size.as<IntImm>()) {            
        stack_size = size->value;
        on_stack = true;
    }

    log(1) << "Allocate " << alloc->name << " on device\n";
    assert(on_stack && "PTX device malloc with non-const size");
    if (!on_stack) return; // HACK: Linux build is unhappy if on_stack is unused outside assert

    llvm::Type *llvm_type = llvm_type_of(alloc->type);
    Value *ptr;

    // In the future, we may want to construct an entire buffer_t here
    string allocation_name = alloc->name + ".host";
    log(3) << "Pushing allocation called " << allocation_name << " onto the symbol table\n";

    assert(false && "Device allocation not yet implemented");

    // Do a 32-byte aligned alloca
    int total_bytes = stack_size * bytes_per_element;
    ptr = builder->CreateAlloca(i32, ConstantInt::get(i32, total_bytes)); 
    ptr = builder->CreatePointerCast(ptr, llvm_type->getPointerTo());

    sym_push(allocation_name, ptr);
    codegen(alloc->body);
    sym_pop(allocation_name);
}

string CodeGen_PTX_Dev::march() const {
    return "nvptx";
}

string CodeGen_PTX_Dev::mcpu() const {
    return "sm_10";
}

string CodeGen_PTX_Dev::mattrs() const {
    return "";
}

bool CodeGen_PTX_Dev::use_soft_float_abi() const {
    return false;
}

string CodeGen_PTX_Dev::compile_to_ptx() {
#ifndef __arm__
    // TODO: streamline this - don't initialize anything but PTX
    LLVMInitializeNVPTXTargetInfo();
    LLVMInitializeNVPTXTarget();
    LLVMInitializeNVPTXTargetMC();
    LLVMInitializeNVPTXAsmPrinter();

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
    Options.JITExceptionHandling = false;
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
    PM.add(new TargetTransformInfo(target->getScalarTargetTransformInfo(),
                                   target->getVectorTargetTransformInfo()));
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
#else
    return "NOT IMPLEMENTED ON ARM: CodeGen_PTX_Dev::compile_to_ptx()";
#endif //disable on ARM
}

}}
