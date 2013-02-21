#include <iostream>
#include "IRPrinter.h"
#include "CodeGen.h"
#include "IROperator.h"
#include "Util.h"
#include "Log.h"
#include "CodeGen_C.h"
#include "Function.h"
#include "Deinterleave.h"

#include <llvm/Config/config.h>

#include <llvm/Analysis/Verifier.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetLibraryInfo.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/CodeGen/CommandFlags.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/PassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO.h>

#if LLVM_VERSION_MINOR < 3
#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/TargetTransformInfo.h>
#include <llvm/DataLayout.h>
#include <llvm/IRBuilder.h>
#else
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#endif

#include <sstream>

using namespace llvm;
using std::ostringstream;
using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::pair;
using std::map;
using std::stack;

namespace Halide { 
namespace Internal {

namespace {
LLVMContext &get_global_context() {
    static LLVMContext *c = NULL;
    if (!c) c = new LLVMContext;
    return *c;    
}
}


CodeGen::CodeGen() : 
    module(NULL), function(NULL), context(get_global_context()), 
    builder(new IRBuilder<>(context)), value(NULL), buffer_t(NULL) {
    // Define some types
    void_t = llvm::Type::getVoidTy(context);
    i1 = llvm::Type::getInt1Ty(context);
    i8 = llvm::Type::getInt8Ty(context);
    i16 = llvm::Type::getInt16Ty(context);
    i32 = llvm::Type::getInt32Ty(context);
    i64 = llvm::Type::getInt64Ty(context);
    f16 = llvm::Type::getHalfTy(context);
    f32 = llvm::Type::getFloatTy(context);
    f64 = llvm::Type::getDoubleTy(context);

    // Initialize the targets we want to generate code for
    if (!llvm_initialized) {            
        InitializeNativeTarget();
        LLVMInitializeX86Target();
        LLVMInitializeX86TargetInfo();
        LLVMInitializeX86AsmPrinter();
        LLVMInitializeX86TargetMC();
        
        LLVMInitializeARMTarget();
        LLVMInitializeARMTargetInfo();
        LLVMInitializeARMAsmPrinter();
        LLVMInitializeARMTargetMC();

        LLVMInitializeNVPTXTarget();
        LLVMInitializeNVPTXTargetInfo();
        LLVMInitializeNVPTXAsmPrinter();
        LLVMInitializeNVPTXTargetMC();
        llvm_initialized = true;
    }
}

CodeGen::~CodeGen() {
    if (module && owns_module) {
        delete module;
        module = NULL;
        owns_module = false;
    }
    delete builder;
    builder = NULL;
}

bool CodeGen::llvm_initialized = false;

void CodeGen::compile(Stmt stmt, string name, const vector<Argument> &args) {
    assert(module && "The CodeGen subclass should have made an initial module before calling CodeGen::compile");
    owns_module = true;

    // Start the module off with a definition of a buffer_t
    define_buffer_t();

    // Now deduce the types of the arguments to our function
    vector<llvm::Type *> arg_types(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            arg_types[i] = buffer_t->getPointerTo();
        } else {
            arg_types[i] = llvm_type_of(args[i].type);
        }
    }

    // Make our function
    function_name = name;
    FunctionType *func_t = FunctionType::get(void_t, arg_types, false);
    function = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module);

    // Make the initial basic block
    BasicBlock *block = BasicBlock::Create(context, "entry", function);
    builder->SetInsertPoint(block);

    // Put the arguments in the symbol table
    {
        size_t i = 0;
        for (llvm::Function::arg_iterator iter = function->arg_begin();
             iter != function->arg_end();
             iter++) {                        

            if (args[i].is_buffer) {
                unpack_buffer(args[i].name, iter);
            } else {
                sym_push(args[i].name, iter);
            }
            i++;
        }
    }

    log(1) << "Generating llvm bitcode...\n";
    // Ok, we have a module, function, context, and a builder
    // pointing at a brand new basic block. We're good to go.
    stmt.accept(this);

    // Now we need to end the function
    builder->CreateRetVoid();

    module->setModuleIdentifier("halide_" + name);

    // Now verify the function is ok
    verifyFunction(*function);

    // Now we need to make the wrapper function (useful for calling from jit)
    string wrapper_name = name + "_jit_wrapper";
    func_t = FunctionType::get(void_t, vec<llvm::Type *>(i8->getPointerTo()->getPointerTo()), false);
    llvm::Function *wrapper = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, wrapper_name, module);
    block = BasicBlock::Create(context, "entry", wrapper);
    builder->SetInsertPoint(block);
    
    Value *arg_array = wrapper->arg_begin();

    vector<Value *> wrapper_args(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        // Get the address of the nth argument
        Value *ptr = builder->CreateConstGEP1_32(arg_array, i);
        ptr = builder->CreateLoad(ptr);
        if (args[i].is_buffer) {
            // Cast the argument to a buffer_t * 
            wrapper_args[i] = builder->CreatePointerCast(ptr, buffer_t->getPointerTo());
        } else {
            // Cast to the appropriate type and load
            ptr = builder->CreatePointerCast(ptr, arg_types[i]->getPointerTo());
            wrapper_args[i] = builder->CreateLoad(ptr);
        }
    }
    builder->CreateCall(function, wrapper_args);
    builder->CreateRetVoid();
    verifyFunction(*wrapper);

    // Finally, verify the module is ok
    verifyModule(*module);
    log(2) << "Done generating llvm bitcode\n";

    if (log::debug_level >= 3) {
        module->dump();
    }
}

// Wraps an execution engine. Takes ownership of the given module and
// the memory for jit compiled code.
class JITModuleHolder {
public:
	mutable RefCount ref_count;    
    JITModuleHolder(Module *module) {
        log(2) << "Creating new execution engine\n";
        string error_string;
        EngineBuilder engine_builder(module);
        engine_builder.setErrorStr(&error_string);
        engine_builder.setEngineKind(EngineKind::JIT);
        engine_builder.setUseMCJIT(true);
        //engine_builder.setOptLevel(CodeGenOpt::Aggressive);
        execution_engine = engine_builder.create();
        if (!execution_engine) cout << error_string << endl;
        assert(execution_engine && "Couldn't create execution engine");        
    }
    ~JITModuleHolder() {
        shutdown_thread_pool();
        delete execution_engine;
    }
    void (*shutdown_thread_pool)();
    llvm::ExecutionEngine *execution_engine;    
};

template<>
RefCount &ref_count<JITModuleHolder>(const JITModuleHolder *f) {return f->ref_count;}

template<>
void destroy<JITModuleHolder>(const JITModuleHolder *f) {delete f;}


JITCompiledModule CodeGen::compile_to_function_pointers() {
    assert(module && "No module defined. Must call compile before calling compile_to_function_pointer");
               
    FunctionPassManager function_pass_manager(module);
    PassManager module_pass_manager;

    log(1) << "JIT compiling...\n";

    IntrusivePtr<JITModuleHolder> module_holder(new JITModuleHolder(module));
    ExecutionEngine *execution_engine = module_holder.ptr->execution_engine;

    // Make sure things marked as always-inline get inlined
    module_pass_manager.add(createAlwaysInlinerPass());
        
    PassManagerBuilder b;
    b.OptLevel = 3;
    b.populateFunctionPassManager(function_pass_manager);
    b.populateModulePassManager(module_pass_manager);
                
    llvm::Function *fn = module->getFunction(function_name);
    assert(fn && "Could not find function inside llvm module");
        
    // Run optimization passes
    module_pass_manager.run(*module);        
    function_pass_manager.doInitialization();
    function_pass_manager.run(*fn);
    function_pass_manager.doFinalization();

    JITCompiledModule m;
    void *f = execution_engine->getPointerToFunction(fn);
    m.function = f;    
    assert(f && "Compiling function returned NULL");

    llvm::Function *wrapper = module->getFunction(function_name + "_jit_wrapper");
    assert(wrapper && "Could not find wrapped function inside llvm module");
    f = execution_engine->getPointerToFunction(wrapper);
    m.wrapped_function = (void (*)(const void **))f;
    assert(f && "Compiling wrapped function returned NULL");

    llvm::Function *set_error_handler = module->getFunction("set_error_handler");
    assert(set_error_handler && "Could not find set_error_handler function inside llvm module");
    f = execution_engine->getPointerToFunction(set_error_handler);
    m.set_error_handler = (void (*)(JITCompiledModule::ErrorHandler))f;
    assert(f && "Compiling set_error_handler function returned NULL");


    llvm::Function *set_custom_allocator = module->getFunction("set_custom_allocator");
    assert(set_custom_allocator && "Could not find set_custom_allocator function inside llvm module");
    f = execution_engine->getPointerToFunction(set_custom_allocator);
    m.set_custom_allocator = (void (*)(void *(*)(size_t), void (*)(void *)))f;
    assert(f && "Compiling set_custom_allocator function returned NULL");

    m.module = module_holder;
    llvm::Function *shutdown_thread_pool = module->getFunction("shutdown_thread_pool");
    assert(shutdown_thread_pool && "Could not find shutdown_thread_pool function inside llvm module");    
    f = execution_engine->getPointerToFunction(shutdown_thread_pool);
    m.module.ptr->shutdown_thread_pool = (void (*)())f;
    assert(f && "Compiling shutdown_thread_pool function returned NULL");

    // We now relinquish ownership of the module
    owns_module = false;
    
    return m;
}

void CodeGen::compile_to_bitcode(const string &filename) {
    assert(module && "No module defined. Must call compile before calling compile_to_bitcode");        
    string error_string;
    raw_fd_ostream out(filename.c_str(), error_string);
    WriteBitcodeToFile(module, out);
}

void CodeGen::compile_to_native(const string &filename, bool assembly) {
    assert(module && "No module defined. Must call compile before calling compile_to_native");
    // Get the target specific parser.
    string error_string;
    log(1) << "Compiling to native code...\n";
    log(2) << "Target triple: " << module->getTargetTriple() << "\n";

    const Target *target = TargetRegistry::lookupTarget(module->getTargetTriple(), error_string);
    if (!target) {
        cout << error_string << endl;
        TargetRegistry::printRegisteredTargetsForVersion();   
    }
    assert(target && "Could not create target");

    log(2) << "Selected target: " << target->getName() << "\n";

    TargetOptions options;
    options.LessPreciseFPMADOption = true;
    options.NoFramePointerElim = false;
    options.NoFramePointerElimNonLeaf = false;
    options.AllowFPOpFusion = FPOpFusion::Fast;
    options.UnsafeFPMath = true;
    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    options.UseSoftFloat = false;
    options.FloatABIType = FloatABI::Default;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.DisableTailCalls = false;
    options.StackAlignmentOverride = 0;
    options.RealignStack = false;
    options.TrapFuncName = "";
    options.PositionIndependentExecutable = true;
    options.EnableSegmentedStacks = false;
    options.UseInitArray = false;
    options.SSPBufferSize = 0;
        
    TargetMachine *target_machine =
        target->createTargetMachine(module->getTargetTriple(), 
                                    mcpu(), mattrs(),
                                    options, 
                                    Reloc::Default, 
                                    CodeModel::Default, 
                                    CodeGenOpt::Aggressive);
                                
    assert(target_machine && "Could not allocate target machine!");

    // Figure out where we are going to send the output.
    raw_fd_ostream raw_out(filename.c_str(), error_string);
    if (!error_string.empty()) {
        std::cerr << "Error opening output " << filename << ": " << error_string << std::endl;
        assert(false);
    }
    formatted_raw_ostream out(raw_out);

    // Build up all of the passes that we want to do to the module.
    PassManager pass_manager;

    // Add an appropriate TargetLibraryInfo pass for the module's triple.
    pass_manager.add(new TargetLibraryInfo(Triple(module->getTargetTriple())));       

    #if LLVM_VERSION_MINOR < 3
    pass_manager.add(new TargetTransformInfo(target_machine->getScalarTargetTransformInfo(),
                                             target_machine->getVectorTargetTransformInfo()));
    #else
    target_machine->addAnalysisPasses(pass_manager);
    #endif
    pass_manager.add(new DataLayout(module));

    // Make sure things marked as always-inline get inlined
    pass_manager.add(createAlwaysInlinerPass());

    // Override default to generate verbose assembly.
    target_machine->setAsmVerbosityDefault(true);

    // Ask the target to add backend passes as necessary.        
    TargetMachine::CodeGenFileType file_type =
        assembly ? TargetMachine::CGFT_AssemblyFile : 
        TargetMachine::CGFT_ObjectFile;
    target_machine->addPassesToEmitFile(pass_manager, out, file_type);

    pass_manager.run(*module);

    delete target_machine;
}

void CodeGen::sym_push(const string &name, llvm::Value *value) {
    value->setName(name);
    symbol_table.push(name, value);
}

void CodeGen::sym_pop(const string &name) {
    symbol_table.pop(name);
}

// Take an llvm Value representing a pointer to a buffer_t,
// and populate the symbol table with its constituent parts
void CodeGen::unpack_buffer(string name, llvm::Value *buffer) {
    sym_push(name + ".host", buffer_host(buffer));
    sym_push(name + ".dev", buffer_dev(buffer));
    sym_push(name + ".host_dirty", buffer_host_dirty(buffer));
    sym_push(name + ".dev_dirty", buffer_dev_dirty(buffer));
    sym_push(name + ".extent.0", buffer_extent(buffer, 0));
    sym_push(name + ".extent.1", buffer_extent(buffer, 1));
    sym_push(name + ".extent.2", buffer_extent(buffer, 2));
    sym_push(name + ".extent.3", buffer_extent(buffer, 3));
    sym_push(name + ".stride.0", buffer_stride(buffer, 0));
    sym_push(name + ".stride.1", buffer_stride(buffer, 1));
    sym_push(name + ".stride.2", buffer_stride(buffer, 2));
    sym_push(name + ".stride.3", buffer_stride(buffer, 3));
    sym_push(name + ".min.0", buffer_min(buffer, 0));
    sym_push(name + ".min.1", buffer_min(buffer, 1));
    sym_push(name + ".min.2", buffer_min(buffer, 2));
    sym_push(name + ".min.3", buffer_min(buffer, 3));
    sym_push(name + ".elem_size", buffer_elem_size(buffer));
}

// Add a definition of buffer_t to the module if it isn't already there
void CodeGen::define_buffer_t() {
    buffer_t = module->getTypeByName("struct.buffer_t");
    if (!buffer_t) {
        buffer_t = StructType::create(context, "struct.buffer_t");
    }

    vector<llvm::Type *> fields;
    fields.push_back(i8->getPointerTo());
    fields.push_back(i64);
    fields.push_back(i8);
    fields.push_back(i8);

    ArrayType* i32x4 = ArrayType::get(i32, 4);        
    fields.push_back(i32x4); // extent
    fields.push_back(i32x4); // stride
    fields.push_back(i32x4); // min
    fields.push_back(i32); // elem_size

    if (buffer_t->isOpaque()) {
        buffer_t->setBody(fields, false);
    }
}
       
// Given an llvm value representing a pointer to a buffer_t, extract various subfields
Value *CodeGen::buffer_host(Value *buffer) {
    Value *ptr = builder->CreateConstGEP2_32(buffer, 0, 0);
    return builder->CreateLoad(ptr);
}

Value *CodeGen::buffer_dev(Value *buffer) {
    Value *ptr = builder->CreateConstGEP2_32(buffer, 0, 1);
    return builder->CreateLoad(ptr);
}

Value *CodeGen::buffer_host_dirty(Value *buffer) {
    Value *ptr = builder->CreateConstGEP2_32(buffer, 0, 2);
    return builder->CreateLoad(ptr);
}

Value *CodeGen::buffer_dev_dirty(Value *buffer) {
    Value *ptr = builder->CreateConstGEP2_32(buffer, 0, 3);
    return builder->CreateLoad(ptr);
}

Value *CodeGen::buffer_extent(Value *buffer, int i) {
    llvm::Value *zero = ConstantInt::get(i32, 0);
    llvm::Value *field = ConstantInt::get(i32, 4);
    llvm::Value *idx = ConstantInt::get(i32, i);
    vector<llvm::Value *> args = vec(zero, field, idx);
    Value *ptr = builder->CreateGEP(buffer, args);
    return builder->CreateLoad(ptr);
}

Value *CodeGen::buffer_stride(Value *buffer, int i) {
    llvm::Value *zero = ConstantInt::get(i32, 0);
    llvm::Value *field = ConstantInt::get(i32, 5);
    llvm::Value *idx = ConstantInt::get(i32, i);
    vector<llvm::Value *> args = vec(zero, field, idx);
    Value *ptr = builder->CreateGEP(buffer, args);
    return builder->CreateLoad(ptr);
}

Value *CodeGen::buffer_min(Value *buffer, int i) {
    llvm::Value *zero = ConstantInt::get(i32, 0);
    llvm::Value *field = ConstantInt::get(i32, 6);
    llvm::Value *idx = ConstantInt::get(i32, i);
    vector<llvm::Value *> args = vec(zero, field, idx);
    Value *ptr = builder->CreateGEP(buffer, args);
    return builder->CreateLoad(ptr);
}

Value *CodeGen::buffer_elem_size(Value *buffer) {
    Value *ptr = builder->CreateConstGEP2_32(buffer, 0, 7);
    return builder->CreateLoad(ptr);
}

llvm::Type *CodeGen::llvm_type_of(Halide::Type t) {
    if (t.width == 1) {
        if (t.is_float()) {
            switch (t.bits) {
            case 16:
                return f16;
            case 32:
                return f32;
            case 64:
                return f64;
            default:
                assert(false && "There is no llvm type matching this floating-point bit width");
                return NULL;
            }
        } else {
            return llvm::Type::getIntNTy(context, t.bits);
        }
    } else {
        llvm::Type *element_type = llvm_type_of(t.element_of());
        return VectorType::get(element_type, t.width);
    }
}

Value *CodeGen::codegen(Expr e) {
    assert(e.defined());
    log(4) << "Codegen: " << e.type() << ", " << e << "\n";
    value = NULL;
    e.accept(this);
    assert(value && "Codegen of an expr did not produce an llvm value");
    return value;
}

void CodeGen::codegen(Stmt s) {
    assert(s.defined());
    log(3) << "Codegen: " << s << "\n";
    value = NULL;
    s.accept(this);
}

void CodeGen::visit(const IntImm *op) {
    value = ConstantInt::getSigned(i32, op->value);
}

void CodeGen::visit(const FloatImm *op) {
    value = ConstantFP::get(context, APFloat(op->value));
}

void CodeGen::visit(const Cast *op) {
    value = codegen(op->value);

    Halide::Type src = op->value.type();
    Halide::Type dst = op->type;
    llvm::Type *llvm_dst = llvm_type_of(dst);

    if (!src.is_float() && !dst.is_float()) {
        // This has the same semantics as the longer code in
        // cg_llvm.ml.  Widening integer casts either zero extend
        // or sign extend, depending on the source type. Narrowing
        // integer casts always truncate.
        value = builder->CreateIntCast(value, llvm_dst, src.is_int());
    } else if (src.is_float() && dst.is_int()) {
        value = builder->CreateFPToSI(value, llvm_dst);
    } else if (src.is_float() && dst.is_uint()) {
        value = builder->CreateFPToUI(value, llvm_dst);
    } else if (src.is_int() && dst.is_float()) {
        value = builder->CreateSIToFP(value, llvm_dst);
    } else if (src.is_uint() && dst.is_float()) {
        value = builder->CreateUIToFP(value, llvm_dst);
    } else {
        assert(src.is_float() && dst.is_float());
        // Float widening or narrowing
        value = builder->CreateFPCast(value, llvm_dst);
    }
}

void CodeGen::visit(const Variable *op) {
    // look in the symbol table
    if (!symbol_table.contains(op->name)) {
        std::cerr << "Symbol not found: " << op->name << std::endl;

        if (log::debug_level > 0) {
            std::cerr << "The following names are in scope:\n";
            const map<string, stack<pair<Value *, int> > > &table = symbol_table.get_table();
            map<string, stack<pair<Value *, int> > >::const_iterator iter;
            for (iter = table.begin(); iter != table.end(); iter++) {
                std::cerr << iter->first << "\n";
            }
        }

        assert(false);
    }
    value = symbol_table.get(op->name);
}

void CodeGen::visit(const Add *op) {
    if (op->type.is_float()) {
        value = builder->CreateFAdd(codegen(op->a), codegen(op->b));
    } else {
        value = builder->CreateAdd(codegen(op->a), codegen(op->b));
    }
}

void CodeGen::visit(const Sub *op) {
    if (op->type.is_float()) {
        value = builder->CreateFSub(codegen(op->a), codegen(op->b));
    } else {
        value = builder->CreateSub(codegen(op->a), codegen(op->b));
    }
}

void CodeGen::visit(const Mul *op) {
    if (op->type.is_float()) {
        value = builder->CreateFMul(codegen(op->a), codegen(op->b));
    } else {
        value = builder->CreateMul(codegen(op->a), codegen(op->b));
    }
}

void CodeGen::visit(const Div *op) {
    if (op->type.is_float()) {
        value = builder->CreateFDiv(codegen(op->a), codegen(op->b));
    } else if (op->type.is_uint()) {
        value = builder->CreateUDiv(codegen(op->a), codegen(op->b));
    } else {
        value = builder->CreateSDiv(codegen(op->a), codegen(op->b));
    }
}

void CodeGen::visit(const Mod *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);

    if (op->type.is_float()) {
        value = builder->CreateFRem(a, b);
    } else if (op->type.is_uint()) {
        value = builder->CreateURem(a, b);
    } else {
        Expr modulus = op->b;
        const Broadcast *broadcast = modulus.as<Broadcast>();
        const IntImm *int_imm = broadcast ? broadcast->value.as<IntImm>() : modulus.as<IntImm>();
        if (int_imm) {
            // if we're modding by a power of two, we can use the unsigned version
            bool is_power_of_two = true;
            for (int v = int_imm->value; v > 1; v >>= 1) {
                if (v & 1) is_power_of_two = false;
            }
            if (is_power_of_two) {
                value = builder->CreateURem(a, b);
                return;
            }
        }

        // to ensure the result of a signed mod is positive, we have to mod, add the modulus, then mod again
        value = builder->CreateSRem(a, b);
        value = builder->CreateAdd(value, b);
        value = builder->CreateSRem(value, b);
    }
}

void CodeGen::visit(const Min *op) {
    Expr a = new Variable(op->a.type(), "a");
    Expr b = new Variable(op->a.type(), "b");
    Expr equiv = new Let("a", op->a, new Let("b", op->b, new Select(a < b, a, b)));
    value = codegen(equiv);
}

void CodeGen::visit(const Max *op) {
    Expr a = new Variable(op->a.type(), "a");
    Expr b = new Variable(op->a.type(), "b");
    Expr equiv = new Let("a", op->a, new Let("b", op->b, new Select(a > b, a, b)));
    value = codegen(equiv);
}

void CodeGen::visit(const EQ *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    Halide::Type t = op->a.type();
    if (t.is_float()) {
        value = builder->CreateFCmpOEQ(a, b);
    } else {
        value = builder->CreateICmpEQ(a, b);
    }
}

void CodeGen::visit(const NE *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    Halide::Type t = op->a.type();
    if (t.is_float()) {
        value = builder->CreateFCmpONE(a, b);
    } else {
        value = builder->CreateICmpNE(a, b);
    }
}

void CodeGen::visit(const LT *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    Halide::Type t = op->a.type();
    if (t.is_float()) {
        value = builder->CreateFCmpOLT(a, b);
    } else if (t.is_int()) {
        value = builder->CreateICmpSLT(a, b);
    } else {
        value = builder->CreateICmpULT(a, b);
    }
}

void CodeGen::visit(const LE *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    Halide::Type t = op->a.type();
    if (t.is_float()) {
        value = builder->CreateFCmpOLE(a, b);
    } else if (t.is_int()) {
        value = builder->CreateICmpSLE(a, b);
    } else {
        value = builder->CreateICmpULE(a, b);
    }
}

void CodeGen::visit(const GT *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    Halide::Type t = op->a.type();
    if (t.is_float()) {
        value = builder->CreateFCmpOGT(a, b);
    } else if (t.is_int()) {
        value = builder->CreateICmpSGT(a, b);
    } else {
        value = builder->CreateICmpUGT(a, b);
    }
}

void CodeGen::visit(const GE *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    Halide::Type t = op->a.type();
    if (t.is_float()) {
        value = builder->CreateFCmpOGE(a, b);
    } else if (t.is_int()) {
        value = builder->CreateICmpSGE(a, b);
    } else {
        value = builder->CreateICmpUGE(a, b);
    }
}

void CodeGen::visit(const And *op) {
    value = builder->CreateAnd(codegen(op->a), codegen(op->b));
}

void CodeGen::visit(const Or *op) {
    value = builder->CreateOr(codegen(op->a), codegen(op->b));
}

void CodeGen::visit(const Not *op) {
    value = builder->CreateNot(codegen(op->a));
}

void CodeGen::visit(const Select *op) {
    // Interleaving two shorter vectors should be done using shufflevector, not select:
    // select(ramp % 2 == 0, a, b)
    const EQ *eq = op->condition.as<EQ>();
    const Mod *mod = eq ? eq->a.as<Mod>() : NULL;
    const Ramp *ramp = mod ? mod->a.as<Ramp>() : NULL;
    if (false && ramp && is_one(ramp->stride) && is_const(eq->b) && is_two(mod->b)) {
        log(3) << "Detected interleave vector pattern. Deinterleaving.\n";
        // TODO: modulus remainder analysis can make this test much more powerful:
        const Mul *base_mul = ramp->base.as<Mul>();
        const Add *base_add = ramp->base.as<Add>();
        const Mul *base_add_mul = base_add ? base_add->a.as<Mul>() : NULL;
        bool base_is_even = base_mul && is_two(base_mul->b);
        bool base_is_odd = base_add_mul && is_one(base_add->b) && is_two(base_add_mul->b);
        Expr a, b;
        if ((is_zero(eq->b) && base_is_even) || 
            (is_one(eq->b) && base_is_odd)) {
            a = extract_even_lanes(op->true_value);
            b = extract_odd_lanes(op->false_value);
        } else if ((is_one(eq->b) && base_is_even) || 
                   (is_zero(eq->b) && base_is_odd)) {
            a = extract_even_lanes(op->false_value);
            b = extract_odd_lanes(op->true_value);
        }        

        if (a.defined() && b.defined()) {
            log(3) << "Resulting vectors to interleave: " << a << ", " << b << "\n";
            
            vector<Constant *> indices(ramp->width);
            for (int i = 0; i < ramp->width; i++) {
                int idx = i/2;
                if (i % 2 == 1) idx += a.type().width;
                indices[i] = ConstantInt::get(i32, idx);
            }

            value = builder->CreateShuffleVector(codegen(a), codegen(b), ConstantVector::get(indices));
            return;
        }
    }

    value = builder->CreateSelect(codegen(op->condition), 
                                 codegen(op->true_value), 
                                 codegen(op->false_value));

}

Value *CodeGen::codegen_buffer_pointer(string buffer, Halide::Type type, Value *index) {
    // Find the base address from the symbol table
    Value *base_address = symbol_table.get(buffer + ".host");
    llvm::Type *base_address_type = base_address->getType();
    unsigned address_space = base_address_type->getPointerAddressSpace();

    llvm::Type *load_type = llvm_type_of(type)->getPointerTo(address_space);

    // If the type doesn't match the expected type, we need to pointer cast
    if (load_type != base_address_type) {
        base_address = builder->CreatePointerCast(base_address, load_type);            
    }

    return builder->CreateGEP(base_address, index);
}

void CodeGen::visit(const Load *op) {
    // There are several cases. Different architectures may wish to override some

    if (op->type.is_scalar()) {
        // 1) Scalar loads
        Value *index = codegen(op->index);
        Value *ptr = codegen_buffer_pointer(op->name, op->type, index);
        value = builder->CreateLoad(ptr);
    } else {            
        const Ramp *ramp = op->index.as<Ramp>();
        const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
        if (ramp && stride && stride->value == 1) {
            /* TODO:
               ModulusRemainder mod_rem(op);
               mod_rem.modulus;
               mod_rem.remainder;
               // 2) Aligned dense vector loads 
                   
               // 3) Unaligned dense vector loads with known alignment
               */
                
            // 4) Unaligned dense vector loads with unknown alignment
            Value *base = codegen(ramp->base);
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), base);
            ptr = builder->CreatePointerCast(ptr, llvm_type_of(op->type)->getPointerTo());
            value = builder->CreateAlignedLoad(ptr, 1);                
        } else if (ramp && stride && stride->value == 2) {
            // Load two vectors worth and then shuffle
            Value *base = codegen(ramp->base);
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), base);
            ptr = builder->CreatePointerCast(ptr, llvm_type_of(op->type)->getPointerTo());
            Value *a = builder->CreateAlignedLoad(ptr, 1);
            ptr = builder->CreateConstGEP1_32(ptr, 1);
            Value *b = builder->CreateAlignedLoad(ptr, 1);
            vector<Constant *> indices(ramp->width);
            for (int i = 0; i < ramp->width; i++) {
                indices[i] = ConstantInt::get(i32, i*2);
            }
            value = builder->CreateShuffleVector(a, b, ConstantVector::get(indices));
        } else if (ramp && stride && stride->value == -1) {
            // Load the vector and then flip it in-place
            Value *base = codegen(ramp->base - ramp->width + 1);
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), base);
            ptr = builder->CreatePointerCast(ptr, llvm_type_of(op->type)->getPointerTo());
            Value *vec = builder->CreateAlignedLoad(ptr, 1);            
            Value *undef = UndefValue::get(vec->getType());

            vector<Constant *> indices(ramp->width);
            for (int i = 0; i < ramp->width; i++) {
                indices[i] = ConstantInt::get(i32, ramp->width-1-i);
            }
            value = builder->CreateShuffleVector(vec, undef, ConstantVector::get(indices));
        } else {                
            // 5) General gathers
            Value *index = codegen(op->index);
            value = UndefValue::get(llvm_type_of(op->type));
            for (int i = 0; i < op->type.width; i++) {
                Value *idx = builder->CreateExtractElement(index, ConstantInt::get(i32, i));
                Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), idx);
                Value *val = builder->CreateLoad(ptr);
                value = builder->CreateInsertElement(value, val, ConstantInt::get(i32, i));
            }
        }            
    }
}

void CodeGen::visit(const Ramp *op) {        
    if (is_const(op->stride) && !is_const(op->base)) {
        // If the stride is const and the base is not (e.g. ramp(x, 1,
        // 4)), we can lift out the stride and broadcast the base so
        // we can do a single vector broadcast and add instead of
        // repeated insertion
        Expr broadcast = new Broadcast(op->base, op->width);
        Expr ramp = new Ramp(make_zero(op->base.type()), op->stride, op->width);
        value = codegen(broadcast + ramp);
    } else {
        // Otherwise we generate element by element by adding the stride to the base repeatedly        

        Value *base = codegen(op->base);
        Value *stride = codegen(op->stride);

        value = UndefValue::get(llvm_type_of(op->type));
        for (int i = 0; i < op->type.width; i++) {
            if (i > 0) {
                if (op->type.is_float()) {
                    base = builder->CreateFAdd(base, stride);
                } else {
                    base = builder->CreateAdd(base, stride);
                }
            }
            value = builder->CreateInsertElement(value, base, ConstantInt::get(i32, i));
        }
    }
}

void CodeGen::visit(const Broadcast *op) {
    value = codegen(op->value);        
    Constant *undef = UndefValue::get(VectorType::get(value->getType(), 1));
    Constant *zero = ConstantInt::get(i32, 0);
    value = builder->CreateInsertElement(undef, value, zero);
    Constant *zeros = ConstantVector::getSplat(op->width, zero);
    value = builder->CreateShuffleVector(value, undef, zeros);
}

void CodeGen::visit(const Call *op) {
    assert(op->call_type == Call::Extern && "Can only codegen extern calls");

    // First, codegen the args
    vector<Value *> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = codegen(op->args[i]);
    }

    // Some call nodes are actually injected at various stages as a
    // cue for llvm to generate particular ops. In general these are
    // handled in the standard library, but ones with e.g. varying
    // types are handled here.
    if (op->name == "extract odd lanes" || op->name == "extract even lanes") {
        bool even = (op->name == "extract even lanes");
        assert(args.size() == 1);
        vector<Constant *> indices(op->type.width);
        for (size_t i = 0; i < indices.size(); i++) {
            indices[i] = ConstantInt::get(i32, even ? (i*2) : (i*2 + 1));
        }
        value = builder->CreateShuffleVector(args[0], args[0], ConstantVector::get(indices));
        return;
    }

    llvm::Function *fn = module->getFunction(op->name);
        
    llvm::Type *result_type = llvm_type_of(op->type);

    // If we can't find it, declare it extern "C"
    if (!fn) {
        // cout << "Didn't find " << op->name << " in initial module. Assuming it's extern." << endl;
        vector<llvm::Type *> arg_types(args.size());
        for (size_t i = 0; i < args.size(); i++) {
            arg_types[i] = args[i]->getType();
        }
        FunctionType *func_t = FunctionType::get(result_type, arg_types, false);
            
        fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, op->name, module);
        fn->setCallingConv(CallingConv::C);            
    }

    if (op->type.is_scalar()) {
        value = builder->CreateCall(fn, args);
    } else {
        // Check if a vector version of the function already
        // exists. We use the naming convention that a N-wide
        // version of a function foo is called fooxN.
        ostringstream ss;
        ss << op->name << 'x' << op->type.width;
        llvm::Function *vec_fn = module->getFunction(ss.str());
        if (vec_fn) {
            value = builder->CreateCall(vec_fn, args);
            fn = vec_fn;
        } else {
            // Scalarize. Extract each simd lane in turn and do
            // one scalar call to the function.
            value = UndefValue::get(result_type);
            for (int i = 0; i < op->type.width; i++) {
                Value *idx = ConstantInt::get(i32, i);
                vector<Value *> arg_lane(args.size());
                for (size_t j = 0; j < args.size(); j++) {
                    arg_lane[j] = builder->CreateExtractElement(args[j], idx);
                }
                Value *result_lane = builder->CreateCall(fn, arg_lane);
                value = builder->CreateInsertElement(value, result_lane, idx);
            }
        }            
    }
}

void CodeGen::visit(const Let *op) {
    sym_push(op->name, codegen(op->value));
    value = codegen(op->body);
    sym_pop(op->name);
}

void CodeGen::visit(const LetStmt *op) {
    sym_push(op->name, codegen(op->value));
    codegen(op->body);
    sym_pop(op->name);
}

void CodeGen::visit(const PrintStmt *op) {
    // Codegen the args, and flatten them into an array of
    // scalars. Also generate a format string
    ostringstream format_string;        
    format_string << op->prefix;

    string fmt_of_type[3];
    fmt_of_type[Halide::Type::UInt] = "%u";
    fmt_of_type[Halide::Type::Int] = "%d";
    fmt_of_type[Halide::Type::Float] = "%3.3f";

    vector<Value *> args;
    vector<Halide::Type> dst_types;
    for (size_t i = 0; i < op->args.size(); i++) {
        format_string << ' ';
        Expr arg = op->args[i];
        Value *ll_arg = codegen(arg);
        if (arg.type().is_vector()) {
            format_string << '[';
            for (int j = 0; j < arg.type().width; j++) {
                if (j > 0) format_string << ' ';
                Value *idx = ConstantInt::get(i32, j);
                Value *ll_arg_lane = builder->CreateExtractElement(ll_arg, idx);
                args.push_back(ll_arg_lane);
                dst_types.push_back(arg.type().element_of());
                format_string << fmt_of_type[arg.type().t];
            }
            format_string << ']';
        } else {
            args.push_back(ll_arg);
            dst_types.push_back(arg.type());
            format_string << fmt_of_type[arg.type().t];
        }
    }

    format_string << endl;

    // Now cast all the args to the appropriate types
    for (size_t i = 0; i < args.size(); i++) {
        if (dst_types[i].is_int()) {
            args[i] = builder->CreateIntCast(args[i], i32, true);                
        } else if (dst_types[i].is_uint()) {
            args[i] = builder->CreateIntCast(args[i], i32, false);
        } else {
            args[i] = builder->CreateFPCast(args[i], f64);
        }            
    }

    // Make the format string a global constant
    string fmt_string = format_string.str();
    llvm::Type *fmt_type = ArrayType::get(i8, fmt_string.size()+1);
    GlobalVariable *fmt_global = new GlobalVariable(*module, fmt_type, true, GlobalValue::PrivateLinkage, 0);
    fmt_global->setInitializer(ConstantDataArray::getString(context, fmt_string));
    Value *char_ptr = builder->CreateConstGEP2_32(fmt_global, 0, 0);

    // The format string is the first argument
    args.insert(args.begin(), char_ptr);

    // Grab the print function from the initial module
    llvm::Function *hlprintf = module->getFunction("hlprintf");
    assert(hlprintf && "Could not find hlprintf in initial module");

    // Call it
    builder->CreateCall(hlprintf, args);
}

void CodeGen::visit(const AssertStmt *op) {
    Value *cond = codegen(op->condition);

    // Make a new basic block for the assert
    BasicBlock *assert_fails_bb = BasicBlock::Create(context, "assert_failed", function);
    BasicBlock *assert_succeeds_bb = BasicBlock::Create(context, "after_assert", function);

    // If the condition fails, enter the assert body, otherwise, enter the block after
    builder->CreateCondBr(cond, assert_succeeds_bb, assert_fails_bb);

    // Build the failure case
    builder->SetInsertPoint(assert_fails_bb);

    // Make the error message string a global constant
    llvm::Type *msg_type = ArrayType::get(i8, op->message.size()+1);
    GlobalVariable *msg_global = new GlobalVariable(*module, msg_type, true, GlobalValue::PrivateLinkage, 0);
    msg_global->setInitializer(ConstantDataArray::getString(context, op->message));
    Value *char_ptr = builder->CreateConstGEP2_32(msg_global, 0, 0);

    // Call the error handler
    llvm::Function *error_handler = module->getFunction("halide_error");
    assert(error_handler && "Could not find halide_error in initial module");
    builder->CreateCall(error_handler, vec(char_ptr));

    // Do any architecture-specific cleanup necessary
    prepare_for_early_exit();

    // Bail out
    builder->CreateRetVoid();

    // Continue on using the success case
    builder->SetInsertPoint(assert_succeeds_bb);
}

void CodeGen::visit(const Pipeline *op) {
    codegen(op->produce);
    if (op->update.defined()) codegen(op->update);
    codegen(op->consume);
}

/* A helper class to manage closures - used for parallel for loops */
class CodeGen::Closure : public IRVisitor {
private:
    map<string, llvm::Type *> result;
    Scope<int> ignore;
    CodeGen *gen;

    void visit(const Let *op) {
        op->value.accept(this);
        ignore.push(op->name, 0);
        op->body.accept(this);
        ignore.pop(op->name);
    }

    void visit(const LetStmt *op) {
        op->value.accept(this);
        ignore.push(op->name, 0);
        op->body.accept(this);
        ignore.pop(op->name);
    }

    void visit(const For *op) {
        ignore.push(op->name, 0);
        op->min.accept(this);
        op->extent.accept(this);
        op->body.accept(this);
        ignore.pop(op->name);
    }

    void visit(const Load *op) {
        op->index.accept(this);
        if (!ignore.contains(op->name)) {
            log(3) << "Adding " << op->name << " to closure\n";
            result[op->name + ".host"] = gen->llvm_type_of(op->type)->getPointerTo();
        } else {
            log(3) << "Not adding " << op->name << " to closure\n";
        }
    }

    void visit(const Store *op) {
        op->index.accept(this);
        op->value.accept(this);
        if (!ignore.contains(op->name)) {
            log(3) << "Adding " << op->name << " to closure\n";
            result[op->name + ".host"] = gen->llvm_type_of(op->value.type())->getPointerTo();
        } else {
            log(3) << "Not adding " << op->name << " to closure\n";
        }
    }

    void visit(const Allocate *op) {
        ignore.push(op->name, 0);
        op->size.accept(this);
        op->body.accept(this);
        ignore.pop(op->name);
    }

    void visit(const Variable *op) {            
        if (ignore.contains(op->name)) {
            log(3) << "Not adding " << op->name << " to closure\n";
        } else {
            log(3) << "Adding " << op->name << " to closure\n";
            result[op->name] = gen->llvm_type_of(op->type);
        }
    }

public:
    Closure(Stmt s, CodeGen *g, const string &loop_variable) : gen(g) {
        ignore.push(loop_variable, 0);
        s.accept(this);
    }

    StructType *build_type() {
        StructType *struct_t = StructType::create(gen->context, "closure_t");
        vector<llvm::Type *> fields;
        for (map<string, llvm::Type *>::const_iterator iter = result.begin(); 
             iter != result.end(); ++iter) {
            fields.push_back(iter->second);
        }
        struct_t->setBody(fields, false);
        return struct_t;
    }

    void pack_struct(Value *dst, const Scope<Value *> &src, IRBuilder<> *builder) {
        // dst should be a pointer to a struct of the type returned by build_type
        int idx = 0;
        for (map<string, llvm::Type *>::const_iterator iter = result.begin(); 
             iter != result.end(); ++iter) {
            // cout << "Putting " << iter->first << " in closure" << endl;
            Value *val = src.get(iter->first);
            Value *ptr = builder->CreateConstGEP2_32(dst, 0, idx++);
            if (val->getType() != iter->second) {
                val = builder->CreateBitCast(val, iter->second);
            }
            builder->CreateStore(val, ptr);
        }
    }

    void unpack_struct(Scope<Value *> &dst, Value *src, IRBuilder<> *builder) {
        // src should be a pointer to a struct of the type returned by build_type
        int idx = 0;
        for (map<string, llvm::Type *>::const_iterator iter = result.begin(); 
             iter != result.end(); ++iter) {
            Value *ptr = builder->CreateConstGEP2_32(src, 0, idx++);
            Value *val = builder->CreateLoad(ptr);
            dst.push(iter->first, val);
            val->setName(iter->first);
        }
    }
};

void CodeGen::visit(const For *op) {
    Value *min = codegen(op->min);
    Value *extent = codegen(op->extent);
        
    if (op->for_type == For::Serial) {
        Value *max = builder->CreateAdd(min, extent);
            
        BasicBlock *preheader_bb = builder->GetInsertBlock();

        // Make a new basic block for the loop
        BasicBlock *loop_bb = BasicBlock::Create(context, op->name + "_loop", function);
        // Create the block that comes after the loop
        BasicBlock *after_bb = BasicBlock::Create(context, op->name + "_after_loop", function);

        // If min < max, fall through to the loop bb
        Value *enter_condition = builder->CreateICmpSLT(min, max);
        builder->CreateCondBr(enter_condition, loop_bb, after_bb);
        builder->SetInsertPoint(loop_bb);

        // Make our phi node
        PHINode *phi = builder->CreatePHI(i32, 2);
        phi->addIncoming(min, preheader_bb);

        // Within the loop, the variable is equal to the phi value
        sym_push(op->name, phi);

        // Emit the loop body
        codegen(op->body);

        // Update the counter
        Value *next_var = builder->CreateAdd(phi, ConstantInt::get(i32, 1));

        // Add the back-edge to the phi node
        phi->addIncoming(next_var, builder->GetInsertBlock());

        // Maybe exit the loop
        Value *end_condition = builder->CreateICmpNE(next_var, max);
        builder->CreateCondBr(end_condition, loop_bb, after_bb);
        builder->SetInsertPoint(after_bb);

        // Pop the loop variable from the scope
        sym_pop(op->name);
    } else if (op->for_type == For::Parallel) {

        log(3) << "Entering parallel for loop over " << op->name << "\n";

        // Find every symbol that the body of this loop refers to
        // and dump it into a closure
        Closure closure(op->body, this, op->name);

        // Allocate a closure
        StructType *closure_t = closure.build_type();
        Value *ptr = builder->CreateAlloca(closure_t, ConstantInt::get(i32, 1)); 

        // Fill in the closure
        closure.pack_struct(ptr, symbol_table, builder);

        // Make a new function that does one iteration of the body of the loop
        FunctionType *func_t = FunctionType::get(void_t, vec(i32, (llvm::Type *)(i8->getPointerTo())), false);
        llvm::Function *containing_function = function;
        function = llvm::Function::Create(func_t, llvm::Function::InternalLinkage, "par_for_" + op->name, module);

        // Make the initial basic block and jump the builder into the new function
        BasicBlock *call_site = builder->GetInsertBlock();
        BasicBlock *block = BasicBlock::Create(context, "entry", function);
        builder->SetInsertPoint(block);

        // Make a new scope to use
        Scope<Value *> saved_symbol_table;
        std::swap(symbol_table, saved_symbol_table);

        // Get the function arguments

        // The loop variable is first argument of the function
        llvm::Function::arg_iterator iter = function->arg_begin();
        sym_push(op->name, iter);

        // The closure pointer is the second argument. 
        ++iter;
        iter->setName("closure");
        Value *closure_handle = builder->CreatePointerCast(iter, closure_t->getPointerTo());
        // Load everything from the closure into the new scope
        closure.unpack_struct(symbol_table, closure_handle, builder);
            
        // Generate the new function body
        codegen(op->body);
        builder->CreateRetVoid();

        // Move the builder back to the main function and call do_par_for
        builder->SetInsertPoint(call_site);
        llvm::Function *do_par_for = module->getFunction("do_par_for");
        assert(do_par_for && "Could not find do_par_for in initial module");
        ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());
        vector<Value *> args = vec((Value *)function, min, extent, ptr);
        builder->CreateCall(do_par_for, args);

        log(3) << "Leaving parallel for loop over " << op->name << "\n";

        // Now restore the scope
        std::swap(symbol_table, saved_symbol_table);
        function = containing_function;
    } else {
        assert(false && "Unknown type of For node. Only Serial and Parallel For nodes should survive down to codegen");
    }
}

void CodeGen::visit(const Store *op) {
    Value *val = codegen(op->value);
    Halide::Type value_type = op->value.type();
    // Scalar
    if (value_type.is_scalar()) {
        Value *index = codegen(op->index);
        Value *ptr = codegen_buffer_pointer(op->name, value_type, index);
        builder->CreateStore(val, ptr);
    } else {
        const Ramp *ramp;
        const IntImm *stride;
        if ((ramp = op->index.as<Ramp>()) &&
            (stride = ramp->stride.as<IntImm>()) &&               
            (stride->value == 1)) {
            bool aligned = false;
            if (aligned) {
                // Aligned dense vector store
                // TODO
            } else {
                // Unaligned dense store
                Value *base = codegen(ramp->base);
                Value *ptr = codegen_buffer_pointer(op->name, value_type.element_of(), base);
                ptr = builder->CreatePointerCast(ptr, llvm_type_of(value_type)->getPointerTo());
                builder->CreateAlignedStore(val, ptr, 1);                    
            }
        } else {
            // Scatter
            Value *index = codegen(op->index);
            for (int i = 0; i < value_type.width; i++) {
                Value *lane = ConstantInt::get(i32, i);
                Value *idx = builder->CreateExtractElement(index, lane);
                Value *v = builder->CreateExtractElement(val, lane);
                Value *ptr = codegen_buffer_pointer(op->name, value_type.element_of(), idx);
                builder->CreateStore(v, ptr);
            }
        }
        


    }
}

void CodeGen::visit(const Block *op) {
    codegen(op->first);
    if (op->rest.defined()) codegen(op->rest);
}        

void CodeGen::visit(const Realize *op) {
    assert(false && "Realize encountered during codegen");
}

void CodeGen::visit(const Provide *op) {
    assert(false && "Provide encountered during codegen");
}

template<>
RefCount &ref_count<CodeGen>(const CodeGen *p) {return p->ref_count;}

template<>
void destroy<CodeGen>(const CodeGen *p) {delete p;}

}}
