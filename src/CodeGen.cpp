#include <iostream>
#include <sstream>

#include "IRPrinter.h"
#include "CodeGen.h"
#include "IROperator.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "Simplify.h"
#include "JITCompiledModule.h"
#include "JITModule.h"
#include "CodeGen_Internal.h"
#include "Lerp.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using namespace llvm;
using std::ostringstream;
using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::pair;
using std::map;
using std::stack;

// Define a local empty inline function for each target
// to disable initialization.
#define LLVM_TARGET(target) \
    inline void Initialize##target##Target() {}
#include <llvm/Config/Targets.def>
#undef LLVM_TARGET

#define LLVM_ASM_PARSER(target)     \
    inline void Initialize##target##AsmParser() {}
#include <llvm/Config/AsmParsers.def>
#undef LLVM_ASM_PARSER

#define LLVM_ASM_PRINTER(target)    \
    inline void Initialize##target##AsmPrinter() {}
#include <llvm/Config/AsmPrinters.def>
#undef LLVM_ASM_PRINTER

#define InitializeTarget(target)              \
        LLVMInitialize##target##Target();     \
        LLVMInitialize##target##TargetInfo(); \
        LLVMInitialize##target##TargetMC();   \
        llvm_##target##_enabled = true;

#define InitializeAsmParser(target)           \
        LLVMInitialize##target##AsmParser();  \

#define InitializeAsmPrinter(target)          \
        LLVMInitialize##target##AsmPrinter(); \

// Override above empty init function with macro for supported targets.
#ifdef WITH_X86
#define InitializeX86Target()       InitializeTarget(X86)
#define InitializeX86AsmParser()    InitializeAsmParser(X86)
#define InitializeX86AsmPrinter()   InitializeAsmPrinter(X86)
#endif

#ifdef WITH_ARM
#define InitializeARMTarget()       InitializeTarget(ARM)
#define InitializeARMAsmParser()    InitializeAsmParser(ARM)
#define InitializeARMAsmPrinter()   InitializeAsmPrinter(ARM)
#endif

#ifdef WITH_PTX
#define InitializeNVPTXTarget()       InitializeTarget(NVPTX)
#define InitializeNVPTXAsmParser()    InitializeAsmParser(NVPTX)
#define InitializeNVPTXAsmPrinter()   InitializeAsmPrinter(NVPTX)
#endif

#ifdef WITH_AARCH64
#define InitializeAArch64Target()       InitializeTarget(AArch64)
#define InitializeAArch64AsmParser()    InitializeAsmParser(AArch64)
#define InitializeAArch64AsmPrinter()   InitializeAsmPrinter(AArch64)
#endif

#ifdef WITH_MIPS
#define InitializeMipsTarget()       InitializeTarget(Mips)
#define InitializeMipsAsmParser()    InitializeAsmParser(Mips)
#define InitializeMipsAsmPrinter()   InitializeAsmPrinter(Mips)
#endif

CodeGen::CodeGen(Target t) :
    module(NULL), owns_module(false),
    function(NULL), context(NULL),
    builder(NULL),
    value(NULL),
    very_likely_branch(NULL),
    target(t),
    void_t(NULL), i1(NULL), i8(NULL), i16(NULL), i32(NULL), i64(NULL),
    f16(NULL), f32(NULL), f64(NULL),
    buffer_t_type(NULL) {
    initialize_llvm();
}

void CodeGen::jit_finalize(llvm::ExecutionEngine *ee, llvm::Module *module,
                           std::vector<JITCompiledModule::CleanupRoutine> *cleanup_routines) {
    if (cleanup_routines) {
        // If the module contains a memoization cache cleanup function, run it when the module dies.
        llvm::Function *fn = module->getFunction("halide_memoization_cache_cleanup");
        if (fn) {
            void *f = ee->getPointerToFunction(fn);
            internal_assert(f) << "Could not find compiled form of halide_memoization_cache_release in module\n";
            void (*cleanup_routine)(void *) =
                reinterpret_bits<void (*)(void *)>(f);
            cleanup_routines->push_back(JITCompiledModule::CleanupRoutine(cleanup_routine, NULL));
        }
    }
}

void CodeGen::initialize_llvm() {
    // Initialize the targets we want to generate code for which are enabled
    // in llvm configuration
    if (!llvm_initialized) {
        InitializeNativeTarget();
        InitializeNativeTargetAsmPrinter();
        InitializeNativeTargetAsmParser();

        #define LLVM_TARGET(target)         \
            Initialize##target##Target();
        #include <llvm/Config/Targets.def>
        #undef LLVM_TARGET

        #define LLVM_ASM_PARSER(target)     \
            Initialize##target##AsmParser();
        #include <llvm/Config/AsmParsers.def>
        #undef LLVM_ASM_PARSER

        #define LLVM_ASM_PRINTER(target)    \
            Initialize##target##AsmPrinter();
        #include <llvm/Config/AsmPrinters.def>
        #undef LLVM_ASM_PRINTER

        llvm_initialized = true;
    }
}

void CodeGen::init_module() {
    if (module && owns_module) {
        delete module;
        delete context;
    }
    delete builder;

    context = new LLVMContext();
    builder = new IRBuilder<>(*context);

    // Branch weights for very likely branches
    llvm::MDBuilder md_builder(*context);
    very_likely_branch = md_builder.createBranchWeights(1 << 30, 0);

    // Define some types
    void_t = llvm::Type::getVoidTy(*context);
    i1 = llvm::Type::getInt1Ty(*context);
    i8 = llvm::Type::getInt8Ty(*context);
    i16 = llvm::Type::getInt16Ty(*context);
    i32 = llvm::Type::getInt32Ty(*context);
    i64 = llvm::Type::getInt64Ty(*context);
    f16 = llvm::Type::getHalfTy(*context);
    f32 = llvm::Type::getFloatTy(*context);
    f64 = llvm::Type::getDoubleTy(*context);
}

CodeGen::~CodeGen() {
    if (module && owns_module) {
        delete module;
        module = NULL;
        delete context;
        context = NULL;
        owns_module = false;
    }
    delete builder;
    builder = NULL;
}

bool CodeGen::llvm_initialized = false;
bool CodeGen::llvm_X86_enabled = false;
bool CodeGen::llvm_ARM_enabled = false;
bool CodeGen::llvm_AArch64_enabled = false;
bool CodeGen::llvm_NVPTX_enabled = false;
bool CodeGen::llvm_Mips_enabled = false;

void CodeGen::compile(Stmt stmt, string name,
                      const vector<Argument> &args,
                      const vector<Buffer> &images_to_embed) {
    internal_assert(module && context && builder)
        << "The CodeGen subclass should have made an initial module before calling CodeGen::compile\n";
    owns_module = true;

    // Start the module off with a definition of a buffer_t
    define_buffer_t();

    // Now deduce the types of the arguments to our function
    vector<llvm::Type *> arg_types(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            arg_types[i] = buffer_t_type->getPointerTo();
        } else {
            arg_types[i] = llvm_type_of(args[i].type);
        }
    }

    // Make our function
    function_name = name;
    FunctionType *func_t = FunctionType::get(i32, arg_types, false);
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
                unpack_buffer(args[i].name, iter);
            } else {
                sym_push(args[i].name, iter);
            }

            i++;
        }
    }

    // Embed the constant images as globals.
    for (size_t i = 0; i < images_to_embed.size(); i++) {
        Buffer buffer = images_to_embed[i];
        internal_assert(buffer.defined());
        buffer_t b = *(buffer.raw_buffer());
        user_assert(b.host)
            << "Can't embed buffer " << buffer.name() << " because it has a null host pointer.\n";

        // Figure out the offset of the last pixel.
        size_t num_elems = 1;
        for (int d = 0; b.extent[d]; d++) {
            num_elems += b.stride[d] * (b.extent[d] - 1);
        }

        vector<char> array;
        array.insert(array.end(), b.host, b.host + num_elems * b.elem_size);

        Constant *host = create_constant_binary_blob(array, buffer.name() + ".data");

        // Embed the buffer_t and make it point to the data array.
        vector<Constant *> fields;

        llvm::ArrayType *i32_array = ArrayType::get(i32, 4);

        fields.push_back(ConstantInt::get(i64, 0)); // dev
        fields.push_back(host);
        fields.push_back(ConstantArray::get(i32_array, vec(
                                            ConstantInt::get(i32, b.extent[0]),
                                            ConstantInt::get(i32, b.extent[1]),
                                            ConstantInt::get(i32, b.extent[2]),
                                            ConstantInt::get(i32, b.extent[3]))));
        fields.push_back(ConstantArray::get(i32_array, vec(
                                            ConstantInt::get(i32, b.stride[0]),
                                            ConstantInt::get(i32, b.stride[1]),
                                            ConstantInt::get(i32, b.stride[2]),
                                            ConstantInt::get(i32, b.stride[3]))));
        fields.push_back(ConstantArray::get(i32_array, vec(
                                            ConstantInt::get(i32, b.min[0]),
                                            ConstantInt::get(i32, b.min[1]),
                                            ConstantInt::get(i32, b.min[2]),
                                            ConstantInt::get(i32, b.min[3]))));
        fields.push_back(ConstantInt::get(i32, b.elem_size));
        user_assert(!b.dev_dirty)
            << "Can't embed Image \"" << buffer.name() << "\""
            << " because it has a dirty device pointer\n";
        fields.push_back(ConstantInt::get(i8, 1));
        fields.push_back(ConstantInt::get(i8, 0));

        Constant *buffer_struct = ConstantStruct::get(buffer_t_type, fields);

        GlobalVariable *global = new GlobalVariable(*module, buffer_t_type,
                                                    true, GlobalValue::PrivateLinkage,
                                                    0, buffer.name() + ".buffer");
        global->setInitializer(buffer_struct);

        // Finally, dump it in the symbol table
        Constant *zero = ConstantInt::get(i32, 0);
        Constant *global_ptr = ConstantExpr::getInBoundsGetElementPtr(global, vec(zero));
        unpack_buffer(buffer.name(), global_ptr);

    }

    debug(1) << "Generating llvm bitcode...\n";
    // Ok, we have a module, function, context, and a builder
    // pointing at a brand new basic block. We're good to go.
    stmt.accept(this);

    // Now we need to end the function
    builder->CreateRet(ConstantInt::get(i32, 0));

    module->setModuleIdentifier("halide_module_" + name);
    debug(2) << module << "\n";

    // Now verify the function is ok
    verifyFunction(*function);

    // Now we need to make the wrapper function (useful for calling from jit)
    string wrapper_name = name + "_jit_wrapper";
    func_t = FunctionType::get(i32, vec<llvm::Type *>(i8->getPointerTo()->getPointerTo()), false);
    llvm::Function *wrapper = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, wrapper_name, module);
    block = BasicBlock::Create(*context, "entry", wrapper);
    builder->SetInsertPoint(block);

    Value *arg_array = wrapper->arg_begin();

    vector<Value *> wrapper_args(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        // Get the address of the nth argument
        Value *ptr = builder->CreateConstGEP1_32(arg_array, (int)i);
        ptr = builder->CreateLoad(ptr);
        if (args[i].is_buffer) {
            // Cast the argument to a buffer_t *
            wrapper_args[i] = builder->CreatePointerCast(ptr, buffer_t_type->getPointerTo());
        } else {
            // Cast to the appropriate type and load
            ptr = builder->CreatePointerCast(ptr, arg_types[i]->getPointerTo());
            wrapper_args[i] = builder->CreateLoad(ptr);
        }
    }
    debug(4) << "Creating call from wrapper to actual function\n";
    Value *result = builder->CreateCall(function, wrapper_args);
    builder->CreateRet(result);
    verifyFunction(*wrapper);

    // Finally, verify the module is ok
    verifyModule(*module);
    debug(2) << "Done generating llvm bitcode\n";

    // Optimize it
    // optimize_module();
}

llvm::Type *CodeGen::llvm_type_of(Type t) {
    return Internal::llvm_type_of(context, t);
}

JITModule CodeGen::compile_to_function_pointers() {
    internal_assert(module) << "No module defined. Must call compile before calling compile_to_function_pointer.\n";

    JITModule m;

    JITModule shared_runtime = JITSharedRuntime::get(this, target);
    m.compile_module(this, module, function_name, vec(shared_runtime), std::vector<std::string>());

    // We now relinquish ownership of the module, and give it to the
    // JITModule object that we're returning.
    owns_module = false;

    return m;
}

void CodeGen::optimize_module() {

    debug(3) << "Optimizing module\n";

    FunctionPassManager function_pass_manager(module);
    PassManager module_pass_manager;

    #if LLVM_VERSION >= 36
    internal_assert(module->getDataLayout()) << "Optimizing module with no data layout, probably will crash in LLVM.\n";
    module_pass_manager.add(new DataLayoutPass());
    #endif

    // Make sure things marked as always-inline get inlined
    module_pass_manager.add(createAlwaysInlinerPass());

    PassManagerBuilder b;
    b.OptLevel = 3;
    b.populateFunctionPassManager(function_pass_manager);
    b.populateModulePassManager(module_pass_manager);

    // Run optimization passes
    module_pass_manager.run(*module);
    if (!function_name.empty()) {
        llvm::Function *fn = module->getFunction(function_name);
        internal_assert(fn) << "Could not find function " << function_name << " inside llvm module\n";

        function_pass_manager.doInitialization();
        function_pass_manager.run(*fn);
        function_pass_manager.doFinalization();
    }

    if (debug::debug_level >= 2) {
        module->dump();
    }
}

void CodeGen::compile_to_bitcode(const string &filename) {
    internal_assert(module) << "No module defined. Must call compile before calling compile_to_bitcode.\n";

    string error_string;
#if LLVM_VERSION < 35
    raw_fd_ostream out(filename.c_str(), error_string);
#elif LLVM_VERSION == 35
    raw_fd_ostream out(filename.c_str(), error_string, llvm::sys::fs::F_None);
#else // llvm 3.6
    std::error_code err;
    raw_fd_ostream out(filename.c_str(), err, llvm::sys::fs::F_None);
    if (err) error_string = err.message();
#endif
    internal_assert(error_string.empty())
        << "Error opening output " << filename << ": " << error_string << "\n";

    WriteBitcodeToFile(module, out);
}

void CodeGen::compile_to_native(const string &filename, bool assembly) {
    internal_assert(module) << "No module defined. Must call compile before calling compile_to_native\n";

    // Get the target specific parser.
    string error_string;
    debug(1) << "Compiling to native code...\n";
    debug(2) << "Target triple: " << module->getTargetTriple() << "\n";

    const llvm::Target *target = TargetRegistry::lookupTarget(module->getTargetTriple(), error_string);
    if (!target) {
        cout << error_string << endl;
        TargetRegistry::printRegisteredTargetsForVersion();
    }
    internal_assert(target) << "Could not create target\n";

    debug(2) << "Selected target: " << target->getName() << "\n";

    TargetOptions options;
    options.LessPreciseFPMADOption = true;
    options.NoFramePointerElim = false;
    options.AllowFPOpFusion = FPOpFusion::Fast;
    options.UnsafeFPMath = true;
    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    options.UseSoftFloat = false;
    options.FloatABIType =
        use_soft_float_abi() ? FloatABI::Soft : FloatABI::Hard;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.DisableTailCalls = false;
    options.StackAlignmentOverride = 0;
    options.TrapFuncName = "";
    options.PositionIndependentExecutable = true;
    options.UseInitArray = false;

    TargetMachine *target_machine =
        target->createTargetMachine(module->getTargetTriple(),
                                    mcpu(), mattrs(),
                                    options,
                                    Reloc::PIC_,
                                    CodeModel::Default,
                                    CodeGenOpt::Aggressive);

    internal_assert(target_machine) << "Could not allocate target machine!\n";

    // Figure out where we are going to send the output.
#if LLVM_VERSION < 35
    raw_fd_ostream raw_out(filename.c_str(), error_string);
#elif LLVM_VERSION == 35
    raw_fd_ostream raw_out(filename.c_str(), error_string, llvm::sys::fs::F_None);
#else // llvm 3.6
    std::error_code err;
    raw_fd_ostream raw_out(filename.c_str(), err, llvm::sys::fs::F_None);
    if (err) error_string = err.message();
#endif
    internal_assert(error_string.empty())
        << "Error opening output " << filename << ": " << error_string << "\n";

    formatted_raw_ostream out(raw_out);

    // Build up all of the passes that we want to do to the module.
    PassManager pass_manager;

    // Add an appropriate TargetLibraryInfo pass for the module's triple.
    pass_manager.add(new TargetLibraryInfo(Triple(module->getTargetTriple())));

    #if LLVM_VERSION < 33
    pass_manager.add(new TargetTransformInfo(target_machine->getScalarTargetTransformInfo(),
                                             target_machine->getVectorTargetTransformInfo()));
    #else
    target_machine->addAnalysisPasses(pass_manager);
    #endif

    #if LLVM_VERSION < 35
    DataLayout *layout = new DataLayout(module);
    debug(2) << "Data layout: " << layout->getStringRepresentation();
    pass_manager.add(layout);
    #endif

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

llvm::Value *CodeGen::sym_get(const string &name, bool must_succeed) const {
    // look in the symbol table
    if (!symbol_table.contains(name)) {
        if (must_succeed) {
            std::ostringstream err;
            err << "Symbol not found: " << name << "\n";

            if (debug::debug_level > 0) {
                err << "The following names are in scope:\n"
                    << symbol_table << "\n";
            }

            internal_error << err.str();
        } else {
            return NULL;
        }
    }
    return symbol_table.get(name);
}

bool CodeGen::sym_exists(const string &name) const {
    return symbol_table.contains(name);
}

// Take an llvm Value representing a pointer to a buffer_t,
// and populate the symbol table with its constituent parts
void CodeGen::unpack_buffer(string name, llvm::Value *buffer) {
    Value *host_ptr = buffer_host(buffer);
    Value *dev_ptr = buffer_dev(buffer);

    // Check it's 32-byte aligned

    // Disabled, because we don't currently require external
    // allocations to be aligned.

    /*
    Value *base = builder->CreatePtrToInt(host_ptr, i64);
    Value *check_alignment = builder->CreateAnd(base, 0x1f);
    check_alignment = builder->CreateIsNull(check_alignment);

    string error_message = "Buffer " + name + " is not 32-byte aligned";
    create_assertion(check_alignment, error_message);
    */

    // Instead track this buffer name so that loads and stores from it
    // don't try to be too aligned.
    might_be_misaligned.insert(name);

    // Make sure the buffer object itself is not null
    create_assertion(builder->CreateIsNotNull(buffer), "buffer argument " + name + " is NULL");

    // Push the buffer pointer as well, for backends that care.
    sym_push(name + ".buffer", buffer);

    sym_push(name + ".host", host_ptr);
    sym_push(name + ".dev", dev_ptr);
    Value *nullity_test = builder->CreateAnd(builder->CreateIsNull(host_ptr),
                                             builder->CreateIsNull(dev_ptr));
    sym_push(name + ".host_and_dev_are_null", nullity_test);
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
    buffer_t_type = module->getTypeByName("struct.buffer_t");
    internal_assert(buffer_t_type) << "Did not find buffer_t in initial module";
}

// Given an llvm value representing a pointer to a buffer_t, extract various subfields
Value *CodeGen::buffer_host(Value *buffer) {
    return builder->CreateLoad(buffer_host_ptr(buffer));
}

Value *CodeGen::buffer_dev(Value *buffer) {
    return builder->CreateLoad(buffer_dev_ptr(buffer));
}

Value *CodeGen::buffer_host_dirty(Value *buffer) {
    return builder->CreateLoad(buffer_host_dirty_ptr(buffer));
}

Value *CodeGen::buffer_dev_dirty(Value *buffer) {
    return builder->CreateLoad(buffer_dev_dirty_ptr(buffer));
}

Value *CodeGen::buffer_extent(Value *buffer, int i) {
    return builder->CreateLoad(buffer_extent_ptr(buffer, i));
}

Value *CodeGen::buffer_stride(Value *buffer, int i) {
    return builder->CreateLoad(buffer_stride_ptr(buffer, i));
}

Value *CodeGen::buffer_min(Value *buffer, int i) {
    return builder->CreateLoad(buffer_min_ptr(buffer, i));
}

Value *CodeGen::buffer_elem_size(Value *buffer) {
    return builder->CreateLoad(buffer_elem_size_ptr(buffer));
}

Value *CodeGen::buffer_host_ptr(Value *buffer) {
    return builder->CreateConstInBoundsGEP2_32(buffer, 0, 1, "buf_host");
}

Value *CodeGen::buffer_dev_ptr(Value *buffer) {
    return builder->CreateConstInBoundsGEP2_32(buffer, 0, 0, "buf_dev");
}

Value *CodeGen::buffer_host_dirty_ptr(Value *buffer) {
    return builder->CreateConstInBoundsGEP2_32(buffer, 0, 6, "buffer_host_dirty");
}

Value *CodeGen::buffer_dev_dirty_ptr(Value *buffer) {
    return builder->CreateConstInBoundsGEP2_32(buffer, 0, 7, "buffer_dev_dirty");
}

Value *CodeGen::buffer_extent_ptr(Value *buffer, int i) {
    llvm::Value *zero = ConstantInt::get(i32, 0);
    llvm::Value *field = ConstantInt::get(i32, 2);
    llvm::Value *idx = ConstantInt::get(i32, i);
    vector<llvm::Value *> args = vec(zero, field, idx);
    return builder->CreateInBoundsGEP(buffer, args, "buf_extent");
}

Value *CodeGen::buffer_stride_ptr(Value *buffer, int i) {
    llvm::Value *zero = ConstantInt::get(i32, 0);
    llvm::Value *field = ConstantInt::get(i32, 3);
    llvm::Value *idx = ConstantInt::get(i32, i);
    vector<llvm::Value *> args = vec(zero, field, idx);
    return builder->CreateInBoundsGEP(buffer, args, "buf_stride");
}

Value *CodeGen::buffer_min_ptr(Value *buffer, int i) {
    llvm::Value *zero = ConstantInt::get(i32, 0);
    llvm::Value *field = ConstantInt::get(i32, 4);
    llvm::Value *idx = ConstantInt::get(i32, i);
    vector<llvm::Value *> args = vec(zero, field, idx);
    return builder->CreateInBoundsGEP(buffer, args, "buf_min");
}

Value *CodeGen::buffer_elem_size_ptr(Value *buffer) {
    return builder->CreateConstInBoundsGEP2_32(buffer, 0, 5, "buf_elem_size");
}

Value *CodeGen::codegen(Expr e) {
    internal_assert(e.defined());
    debug(4) << "Codegen: " << e.type() << ", " << e << "\n";
    value = NULL;
    e.accept(this);
    internal_assert(value) << "Codegen of an expr did not produce an llvm value\n";
    return value;
}

void CodeGen::codegen(Stmt s) {
    internal_assert(s.defined());
    debug(3) << "Codegen: " << s << "\n";
    value = NULL;
    s.accept(this);
}

void CodeGen::visit(const IntImm *op) {
    value = ConstantInt::getSigned(i32, op->value);
}

void CodeGen::visit(const FloatImm *op) {
    value = ConstantFP::get(*context, APFloat(op->value));
}

void CodeGen::visit(const StringImm *op) {
    value = create_string_constant(op->value);
}

void CodeGen::visit(const Cast *op) {
    Halide::Type src = op->value.type();
    Halide::Type dst = op->type;

    value = codegen(op->value);

    llvm::Type *llvm_dst = llvm_type_of(dst);

    if (dst.is_handle() && src.is_handle()) {
        value = builder->CreateBitCast(value, llvm_dst);
    } else if (dst.is_handle() || src.is_handle()) {
        internal_error << "Can't cast from " << src << " to " << dst << "\n";
    } else if (!src.is_float() && !dst.is_float()) {
        // Widening integer casts either zero extend or sign extend,
        // depending on the source type. Narrowing integer casts
        // always truncate.
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
        internal_assert(src.is_float() && dst.is_float());
        // Float widening or narrowing
        value = builder->CreateFPCast(value, llvm_dst);
    }
}

void CodeGen::visit(const Variable *op) {
    value = sym_get(op->name);
}

void CodeGen::visit(const Add *op) {
    if (op->type.is_float()) {
        value = builder->CreateFAdd(codegen(op->a), codegen(op->b));
    } else if (op->type.is_int()) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        value = builder->CreateNSWAdd(codegen(op->a), codegen(op->b));
    } else {
        value = builder->CreateAdd(codegen(op->a), codegen(op->b));
    }
}

void CodeGen::visit(const Sub *op) {
    if (op->type.is_float()) {
        value = builder->CreateFSub(codegen(op->a), codegen(op->b));
    } else if (op->type.is_int()) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        value = builder->CreateNSWSub(codegen(op->a), codegen(op->b));
    } else {
        value = builder->CreateSub(codegen(op->a), codegen(op->b));
    }
}

void CodeGen::visit(const Mul *op) {
    if (op->type.is_float()) {
        value = builder->CreateFMul(codegen(op->a), codegen(op->b));
    } else if (op->type.is_int()) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        value = builder->CreateNSWMul(codegen(op->a), codegen(op->b));
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
        // Signed integer division sucks. It should be defined such
        // that it satisifies (a/b)*b + a%b = a, where 0 <= a%b < |b|,
        // i.e. Euclidean division.

        // If it's a small const power of two, then we can just
        // arithmetic right shift. This rounds towards negative
        // infinity.
        for (int bits = 1; bits < 30; bits++) {
            if (is_const(op->b, 1 << bits)) {
                Value *shift = codegen(make_const(op->a.type(), bits));
                value = builder->CreateAShr(codegen(op->a), shift);
                return;
            }
        }

        // We get rounding to work by examining the implied remainder
        // and correcting the quotient.

        /* Here's the C code that we're trying to match:
        int q = a / b;
        int r = a - q * b;
        int bs = b >> (t.bits - 1);
        int rs = r >> (t.bits - 1);
        return q - (rs & bs) + (rs & ~bs);
        */

        Value *a = codegen(op->a), *b = codegen(op->b);

        Value *q = builder->CreateSDiv(a, b);
        Value *r = builder->CreateSub(a, builder->CreateMul(q, b));
        Value *shift = ConstantInt::get(a->getType(), op->a.type().bits-1);
        Value *bs = builder->CreateAShr(b, shift);
        Value *rs = builder->CreateAShr(r, shift);
        Value *round_up = builder->CreateAnd(rs, bs);
        Value *round_down = builder->CreateAnd(rs, builder->CreateNot(bs));
        value = builder->CreateAdd(builder->CreateSub(q, round_up), round_down);
    }
}

void CodeGen::visit(const Mod *op) {
    // To match our definition of division, mod should be between 0
    // and |b|.

    if (op->type.is_float()) {
        value = codegen(simplify(op->a - op->b * floor(op->a/op->b)));
    } else if (op->type.is_uint()) {
        int bits;
        if (is_const_power_of_two(op->b, &bits)) {
            Expr one = make_one(op->b.type());
            value = builder->CreateAnd(codegen(op->a), codegen(op->b - one));
        } else {
            value = builder->CreateURem(codegen(op->a), codegen(op->b));
        }
    } else {
        int bits;
        if (is_const_power_of_two(op->b, &bits)) {
            Expr one = make_one(op->b.type());
            value = builder->CreateAnd(codegen(op->a), codegen(op->b - one));
        } else {
            Value *a = codegen(op->a);
            Value *b = codegen(op->b);

            // Match this non-overflowing C code
            /*
              T r = a % b;
              r = r + (r < 0 ? abs(b) : 0);
            */

            Value *r = builder->CreateSRem(a, b);
            Value *zero = ConstantInt::get(r->getType(), 0);
            Value *b_lt_0 = builder->CreateICmpSLT(b, zero);
            Value *abs_b = builder->CreateSelect(b_lt_0, builder->CreateNeg(b), b);
            Value *r_lt_0 = builder->CreateICmpSLT(r, zero);
            value = builder->CreateSelect(r_lt_0, builder->CreateAdd(r, abs_b), r);
        }
    }
}

void CodeGen::visit(const Min *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    Value *cmp;

    Halide::Type t = op->a.type();
    if (t.is_float()) {
        cmp = builder->CreateFCmpOLT(a, b);
    } else if (t.is_int()) {
        cmp = builder->CreateICmpSLT(a, b);
    } else {
        cmp = builder->CreateICmpULT(a, b);
    }

    value = builder->CreateSelect(cmp, a, b);
}

void CodeGen::visit(const Max *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    Value *cmp;

    Halide::Type t = op->a.type();
    if (t.is_float()) {
        cmp = builder->CreateFCmpOLT(a, b);
    } else if (t.is_int()) {
        cmp = builder->CreateICmpSLT(a, b);
    } else {
        cmp = builder->CreateICmpULT(a, b);
    }

    value = builder->CreateSelect(cmp, b, a);
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
    // For now we always generate select nodes, but the code is here
    // for if then elses if we need it
    if (false && op->condition.type().is_scalar()) {
        // Codegen an if-then-else so we don't go to the expense of
        // generating both vectors

        BasicBlock *true_bb = BasicBlock::Create(*context, "true_bb", function);
        BasicBlock *false_bb = BasicBlock::Create(*context, "false_bb", function);
        BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
        builder->CreateCondBr(codegen(op->condition), true_bb, false_bb);

        builder->SetInsertPoint(true_bb);
        Value *true_value = codegen(op->true_value);
        builder->CreateBr(after_bb);

        builder->SetInsertPoint(false_bb);
        Value *false_value = codegen(op->false_value);
        builder->CreateBr(after_bb);

        builder->SetInsertPoint(after_bb);
        PHINode *phi = builder->CreatePHI(true_value->getType(), 2);
        phi->addIncoming(true_value, true_bb);
        phi->addIncoming(false_value, false_bb);

        value = phi;
    } else if (op->type == Int(32)) {
        // llvm has a performance bug inside of loop strength
        // reduction that barfs on long chains of selects. To avoid
        // it, we use bit-masking instead.
        Value *cmp = codegen(op->condition);
        Value *a = codegen(op->true_value);
        Value *b = codegen(op->false_value);
        cmp = builder->CreateIntCast(cmp, i32, true);
        a = builder->CreateAnd(a, cmp);
        cmp = builder->CreateNot(cmp);
        b = builder->CreateAnd(b, cmp);
        value = builder->CreateOr(a, b);
    } else {
        value = builder->CreateSelect(codegen(op->condition),
                                      codegen(op->true_value),
                                      codegen(op->false_value));
    }
}

namespace {
Expr promote_64(Expr e) {
    if (const Add *a = e.as<Add>()) {
        return Add::make(promote_64(a->a), promote_64(a->b));
    } else if (const Sub *s = e.as<Sub>()) {
        return Sub::make(promote_64(s->a), promote_64(s->b));
    } else if (const Mul *m = e.as<Mul>()) {
        return Mul::make(promote_64(m->a), promote_64(m->b));
    } else if (const Min *m = e.as<Min>()) {
        return Min::make(promote_64(m->a), promote_64(m->b));
    } else if (const Max *m = e.as<Max>()) {
        return Max::make(promote_64(m->a), promote_64(m->b));
    } else {
        return Cast::make(Int(64), e);
    }
}
}

Value *CodeGen::codegen_buffer_pointer(string buffer, Halide::Type type, Expr index) {
    // Promote index to 64-bit on targets that use 64-bit pointers.
    llvm::DataLayout d(module);
    if (d.getPointerSize() == 8) {
        index = promote_64(index);
    }

    return codegen_buffer_pointer(buffer, type, codegen(index));
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

    // Promote index to 64-bit on targets that use 64-bit pointers.
    llvm::DataLayout d(module);
    if (d.getPointerSize() == 8) {
        index = builder->CreateIntCast(index, i64, true);
    }

    return builder->CreateInBoundsGEP(base_address, index);
}

namespace {
int next_power_of_two(int x) {
    for (int p2 = 1; ; p2 *= 2) {
        if (p2 >= x) {
            return p2;
        }
    }
    // unreachable.
}
}

void CodeGen::add_tbaa_metadata(llvm::Instruction *inst, string buffer, Expr index) {

    // If the index is constant, we generate some TBAA info that helps
    // LLVM understand our loads/stores aren't aliased.
    bool constant_index = false;
    int base = 0;
    int width = 1;

    if (index.defined()) {
        if (const Ramp *ramp = index.as<Ramp>()) {
            const int *pstride = as_const_int(ramp->stride);
            const int *pbase = as_const_int(ramp->base);
            if (pstride && pbase) {
                // We want to find the smallest aligned width and offset
                // that contains this ramp.
                int stride = *pstride;
                base = *pbase;
                assert(base >= 0);
                width = next_power_of_two(ramp->width * stride);

                while (base % width) {
                    base -= base % width;
                    width *= 2;
                }
                constant_index = true;
            }
        } else {
            const int *pbase = as_const_int(index);
            if (pbase) {
                base = *pbase;
                constant_index = true;
            }
        }
    }

    // Add type-based-alias-analysis metadata to the pointer, so that
    // loads and stores to different buffers can get reordered.
    MDNode *tbaa = MDNode::get(*context, vec<LLVMMDNodeArgumentType>(MDString::get(*context, "Halide buffer")));
    tbaa = MDNode::get(*context, vec<LLVMMDNodeArgumentType>(MDString::get(*context, buffer), tbaa));

    // We also add metadata for constant indices to allow loads and
    // stores to the same buffer to get reordered.
    if (constant_index) {
        for (int w = 1024; w >= width; w /= 2) {
            int b = (base / w) * w;

            std::stringstream level;
            level << buffer << ".width" << w << ".base" << b;
            tbaa = MDNode::get(*context, vec<LLVMMDNodeArgumentType>(MDString::get(*context, level.str()), tbaa));
        }
    }

    inst->setMetadata("tbaa", tbaa);
}

void CodeGen::visit(const Load *op) {

    bool possibly_misaligned = (might_be_misaligned.find(op->name) != might_be_misaligned.end());

    // There are several cases. Different architectures may wish to override some.
    if (op->type.is_scalar()) {
        // Scalar loads
        Value *ptr = codegen_buffer_pointer(op->name, op->type, op->index);
        LoadInst *load = builder->CreateAlignedLoad(ptr, op->type.bytes());
        add_tbaa_metadata(load, op->name, op->index);
        value = load;
    } else {
        const Ramp *ramp = op->index.as<Ramp>();
        const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;

        if (ramp && stride && stride->value == 1) {
            int alignment = op->type.bytes(); // The size of a single element

            int native_bits = native_vector_bits();

            // Boost the alignment if possible, up to the native vector width.
            ModulusRemainder mod_rem = modulus_remainder(ramp->base, alignment_info);
            if (!possibly_misaligned) {
                while ((mod_rem.remainder & 1) == 0 &&
                       (mod_rem.modulus & 1) == 0 &&
                       alignment < native_bits) {
                    mod_rem.modulus /= 2;
                    mod_rem.remainder /= 2;
                    alignment *= 2;
                }
            }

            // For dense vector loads wider than the native vector
            // width, bust them up into native vectors
            int load_lanes = op->type.width;
            int native_lanes = native_bits / op->type.bits;
            vector<Value *> slices;
            for (int i = 0; i < load_lanes; i += native_lanes) {
                int slice_lanes = std::min(native_lanes, load_lanes - i);
                Expr slice_base = simplify(ramp->base + i);
                Expr slice_index = slice_lanes == 1 ? slice_base : Ramp::make(slice_base, 1, slice_lanes);
                llvm::Type *slice_type = VectorType::get(llvm_type_of(op->type.element_of()), slice_lanes);
                Value *elt_ptr = codegen_buffer_pointer(op->name, op->type.element_of(), slice_base);
                Value *vec_ptr = builder->CreatePointerCast(elt_ptr, slice_type->getPointerTo());
                LoadInst *load = builder->CreateAlignedLoad(vec_ptr, alignment);
                add_tbaa_metadata(load, op->name, slice_index);
                slices.push_back(load);
            }
            value = concat_vectors(slices);

        } else if (ramp && stride && stride->value == 2) {
            // Load two vectors worth and then shuffle
            Expr base_a = ramp->base, base_b = ramp->base + ramp->width;

            // False indicates we should take the even-numbered lanes
            // from the load, true indicates we should take the
            // odd-numbered-lanes.
            bool shifted_a = false, shifted_b = false;

            bool internal = op->param.defined() || op->image.defined();

            // Don't read beyond the end of an external buffer.
            if (!internal) {
                base_b -= 1;
                shifted_b = true;
            } else {
                // If the base ends in an odd constant, then subtract one
                // and do a different shuffle. This helps expressions like
                // (f(2*x) + f(2*x+1) share loads
                const Add *add = ramp->base.as<Add>();
                const IntImm *offset = add ? add->b.as<IntImm>() : NULL;
                if (offset && offset->value & 1) {
                    base_a -= 1;
                    shifted_a = true;
                    base_b -= 1;
                    shifted_b = true;
                }
            }

            // Do each load.
            Expr ramp_a = Ramp::make(base_a, 1, ramp->width);
            Expr ramp_b = Ramp::make(base_b, 1, ramp->width);
            Expr load_a = Load::make(op->type, op->name, ramp_a, op->image, op->param);
            Expr load_b = Load::make(op->type, op->name, ramp_b, op->image, op->param);
            Value *vec_a = codegen(load_a);
            Value *vec_b = codegen(load_b);

            // Shuffle together the results.
            vector<Constant *> indices(ramp->width);
            for (int i = 0; i < (ramp->width + 1)/2; i++) {
                indices[i] = ConstantInt::get(i32, i*2 + (shifted_a ? 1 : 0));
            }
            for (int i = (ramp->width + 1)/2; i < ramp->width; i++) {
                indices[i] = ConstantInt::get(i32, i*2 + (shifted_b ? 1 : 0));
            }

            value = builder->CreateShuffleVector(vec_a, vec_b, ConstantVector::get(indices));
        } else if (ramp && stride && stride->value == -1) {
            // Load the vector and then flip it in-place
            Expr flipped_base = ramp->base - ramp->width + 1;
            Expr flipped_index = Ramp::make(flipped_base, 1, ramp->width);
            Expr flipped_load = Load::make(op->type, op->name, flipped_index, op->image, op->param);

            Value *flipped = codegen(flipped_load);

            vector<Constant *> indices(ramp->width);
            for (int i = 0; i < ramp->width; i++) {
                indices[i] = ConstantInt::get(i32, ramp->width-1-i);
            }

            Constant *undef = UndefValue::get(flipped->getType());
            value = builder->CreateShuffleVector(flipped, undef, ConstantVector::get(indices));
        } else if (ramp) {
            // Gather without generating the indices as a vector
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), ramp->base);
            Value *stride = codegen(ramp->stride);
            value = UndefValue::get(llvm_type_of(op->type));
            for (int i = 0; i < ramp->width; i++) {
                Value *lane = ConstantInt::get(i32, i);
                LoadInst *val = builder->CreateLoad(ptr);
                add_tbaa_metadata(val, op->name, op->index);
                value = builder->CreateInsertElement(value, val, lane);
                ptr = builder->CreateInBoundsGEP(ptr, stride);
            }
        } else if (false /* should_scalarize(op->index) */) {
            // TODO: put something sensible in for
            // should_scalarize. Probably a good idea if there are no
            // loads in it, and it's all int32.

            // Compute the index as scalars, and then do a gather
            Value *vec = UndefValue::get(llvm_type_of(op->type));
            for (int i = 0; i < op->type.width; i++) {
                Expr idx = extract_lane(op->index, i);
                Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), idx);
                LoadInst *val = builder->CreateLoad(ptr);
                add_tbaa_metadata(val, op->name, op->index);
                vec = builder->CreateInsertElement(vec, val, ConstantInt::get(i32, i));
            }
            value = vec;
        } else {
            // General gathers
            Value *index = codegen(op->index);
            Value *vec = UndefValue::get(llvm_type_of(op->type));
            for (int i = 0; i < op->type.width; i++) {
                Value *idx = builder->CreateExtractElement(index, ConstantInt::get(i32, i));
                Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), idx);
                LoadInst *val = builder->CreateLoad(ptr);
                add_tbaa_metadata(val, op->name, op->index);
                vec = builder->CreateInsertElement(vec, val, ConstantInt::get(i32, i));
            }
            value = vec;
        }
    }

}

void CodeGen::visit(const Ramp *op) {
    if (is_const(op->stride) && !is_const(op->base)) {
        // If the stride is const and the base is not (e.g. ramp(x, 1,
        // 4)), we can lift out the stride and broadcast the base so
        // we can do a single vector broadcast and add instead of
        // repeated insertion
        Expr broadcast = Broadcast::make(op->base, op->width);
        Expr ramp = Ramp::make(make_zero(op->base.type()), op->stride, op->width);
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
                    base = builder->CreateNSWAdd(base, stride);
                }
            }
            value = builder->CreateInsertElement(value, base, ConstantInt::get(i32, i));
        }
    }
}

llvm::Value *CodeGen::create_broadcast(llvm::Value *v, int width) {
    Constant *undef = UndefValue::get(VectorType::get(v->getType(), width));
    Constant *zero = ConstantInt::get(i32, 0);
    v = builder->CreateInsertElement(undef, v, zero);
    Constant *zeros = ConstantVector::getSplat(width, zero);
    return builder->CreateShuffleVector(v, undef, zeros);
}

void CodeGen::visit(const Broadcast *op) {
    value = create_broadcast(codegen(op->value), op->width);
}

// Pass through scalars, and unpack broadcasts. Assert if it's a non-vector broadcast.
Expr unbroadcast(Expr e) {
    if (e.type().is_vector()) {
        const Broadcast *broadcast = e.as<Broadcast>();
        internal_assert(broadcast);
        return broadcast->value;
    } else {
        return e;
    }
}

// Returns true if the given function name is one of the Halide runtime
// functions that takes a user_context pointer as its first parameter.
bool CodeGen::function_takes_user_context(const string &name) {
    static const char *user_context_runtime_funcs[] = {
        "halide_copy_to_host",
        "halide_copy_to_dev",
        "halide_current_time_ns",
        "halide_debug_to_file",
        "halide_dev_free",
        "halide_dev_malloc",
        "halide_dev_run",
        "halide_dev_sync",
        "halide_do_par_for",
        "halide_do_task",
        "halide_error",
        "halide_free",
        "halide_init_kernels",
        "halide_malloc",
        "halide_print",
        "halide_profiling_timer",
        "halide_release",
        "halide_start_clock",
        "halide_trace",
        "halide_memoization_cache_lookup",
        "halide_memoization_cache_store"
    };
    const int num_funcs = sizeof(user_context_runtime_funcs) /
        sizeof(user_context_runtime_funcs[0]);
    for (int i = 0; i < num_funcs; ++i) {
        if (name == user_context_runtime_funcs[i]) {
            return true;
        }
    }
    return false;
}

Value *CodeGen::interleave_vectors(Type type, const std::vector<Expr>& vecs) {
    if(vecs.size() == 1) {
        return codegen(vecs[0]);
    } else if(vecs.size() == 2) {
        Expr a = vecs[0], b = vecs[1];
        debug(3) << "Vectors to interleave: " << a << ", " << b << "\n";

        vector<Constant *> indices(type.width);
        for (int i = 0; i < type.width; i++) {
            int idx = i/2;
            if (i % 2 == 1) idx += a.type().width;
            indices[i] = ConstantInt::get(i32, idx);
        }

        return builder->CreateShuffleVector(codegen(a), codegen(b), ConstantVector::get(indices));
    } else if(vecs.size() == 3) {
        Expr a = vecs[0], b = vecs[1], c = vecs[2];
        debug(3) << "Vectors to interleave: " << a << ", " << b << ", " << c << "\n";

        // First we shuffle a & b together...
        vector<Constant *> indices(type.width);
        for (int i = 0; i < type.width; i++) {
            if (i % 3 == 0) {
                indices[i] = ConstantInt::get(i32, i/3);
            } else if (i % 3 == 1) {
                indices[i] = ConstantInt::get(i32, i/3 + a.type().width);
            } else {
                indices[i] = UndefValue::get(i32);
            }
        }

        Value *value_ab = builder->CreateShuffleVector(codegen(a), codegen(b), ConstantVector::get(indices));

        // Then we create a vector of the output size that contains c...
        for (int i = 0; i < type.width; i++) {
            if (i < c.type().width) {
                indices[i] = ConstantInt::get(i32, i);
            } else {
                indices[i] = UndefValue::get(i32);
            }
        }

        Value *none = UndefValue::get(llvm_type_of(c.type()));
        Value *value_c = builder->CreateShuffleVector(codegen(c), none, ConstantVector::get(indices));

        // Finally, we shuffle the above 2 vectors together into the result.
        for (int i = 0; i < type.width; i++) {
            if (i % 3 < 2) {
                indices[i] = ConstantInt::get(i32, i);
            } else {
                indices[i] = ConstantInt::get(i32, i/3 + type.width);
            }
        }

        return builder->CreateShuffleVector(value_ab, value_c, ConstantVector::get(indices));
    } else if (vecs.size() == 4) {
        Expr a = vecs[0], b = vecs[1], c = vecs[2], d = vecs[3];
        debug(3) << "Vectors to interleave: " << a << ", " << b << ", " << c << ", " << d << "\n";

        int half_width = type.width / 2;
        vector<Constant *> indices(half_width);
        for (int i = 0; i < half_width; i++) {
            int idx = i/2;
            if (i % 2 == 1) idx += a.type().width;
            indices[i] = ConstantInt::get(i32, idx);
        }

        // First we shuffle a & b together...
        Value *value_ab = builder->CreateShuffleVector(codegen(a), codegen(b), ConstantVector::get(indices));

        // Next we shuffle c & d together...
        Value *value_cd = builder->CreateShuffleVector(codegen(c), codegen(d), ConstantVector::get(indices));

        // Now we reinterpret the shuffled vectors as vectors of pairs...
        Type t = a.type();
        t.bits *= 2;
        Value *vec_ab = builder->CreateBitCast(value_ab, llvm_type_of(t));
        Value *vec_cd = builder->CreateBitCast(value_cd, llvm_type_of(t));

        // Finally, we shuffle the above 2 vectors together into the result.
        Value *vec = builder->CreateShuffleVector(vec_ab, vec_cd, ConstantVector::get(indices));
        return builder->CreateBitCast(vec, llvm_type_of(type));
    } else {
        Type even_t = type;
        Type odd_t  = type;
        even_t.width = odd_t.width = 0;
        std::vector<Expr> even_vecs, odd_vecs;
        int odd_num_vecs = vecs.size() % 2;
        for (size_t i = 0; i < vecs.size() - odd_num_vecs; ++i) {
            if (i % 2 == 0) {
                even_t.width += vecs[i].type().width;
                even_vecs.push_back(vecs[i]);
            } else {
                odd_t.width += vecs[i].type().width;
                odd_vecs.push_back(vecs[i]);
            }
        }

        Expr last;
        if (odd_num_vecs) {
            last = vecs.back();
        }

        Value* a = interleave_vectors(even_t, even_vecs);
        Value* b = interleave_vectors(odd_t, odd_vecs);

        if (odd_num_vecs == 0 ) {
            vector<Constant *> indices(type.width);
            for (int i = 0; i < type.width; i++) {
                int idx = i/2;
                if (i % 2 == 1) idx += even_t.width;
                indices[i] = ConstantInt::get(i32, idx);
            }

            return builder->CreateShuffleVector(a, b, ConstantVector::get(indices));
        } else {
            vector<Constant *> indices(type.width);
            for (int i = 0, idx = 0; i < type.width; i++) {
                if (i % vecs.size() < vecs.size()-1) {
                    if (idx % 2 == 0) {
                        indices[i] = ConstantInt::get(i32, idx / 2);
                    } else {
                        indices[i] = ConstantInt::get(i32, idx / 2 + even_t.width);
                    }

                    ++idx;
                } else {
                    indices[i] = UndefValue::get(i32);
                }
            }

            Value *ab = builder->CreateShuffleVector(a, b, ConstantVector::get(indices));

            for (int i = 0; i < type.width; i++) {
                if (i < last.type().width) {
                    indices[i] = ConstantInt::get(i32, i);
                } else {
                    indices[i] = UndefValue::get(i32);
                }
            }

            Value *none = UndefValue::get(llvm_type_of(last.type()));
            Value *c = builder->CreateShuffleVector(codegen(last), none, ConstantVector::get(indices));

            for (int i = 0; i < type.width; i++) {
                if (i % vecs.size() < vecs.size()-1) {
                    indices[i] = ConstantInt::get(i32, i);
                } else {
                    indices[i] = ConstantInt::get(i32, i/vecs.size() + type.width);
                }
            }

            return builder->CreateShuffleVector(ab, c, ConstantVector::get(indices));
        }
    }
}

void CodeGen::scalarize(Expr e) {
    llvm::Type *result_type = llvm_type_of(e.type());

    Value *result = UndefValue::get(result_type);

    for (int i = 0; i < e.type().width; i++) {
        Value *v = codegen(extract_lane(e, i));
        result = builder->CreateInsertElement(result, v, ConstantInt::get(i32, i));
    }
    value = result;
}

void CodeGen::visit(const Call *op) {
    internal_assert((op->call_type == Call::Extern || op->call_type == Call::Intrinsic))
        << "Can only codegen extern calls and intrinsics\n";

    if (op->call_type == Call::Intrinsic) {
        // Some call nodes are actually injected at various stages as a
        // cue for llvm to generate particular ops. In general these are
        // handled in the standard library, but ones with e.g. varying
        // types are handled here.
        if (op->name == Call::shuffle_vector) {
            internal_assert((int) op->args.size() == 1 + op->type.width);
            vector<Constant *> indices(op->type.width);
            for (size_t i = 0; i < indices.size(); i++) {
                const IntImm *idx = op->args[i+1].as<IntImm>();
                internal_assert(idx);
                internal_assert(idx->value >= 0 && idx->value <= op->args[0].type().width);
                indices[i] = ConstantInt::get(i32, idx->value);
            }
            Value *arg = codegen(op->args[0]);

            // Make a size 1 vector of undef at the end to mix in undef values.
            //llvm::VectorType *undef_end_type = arg->getType();
            Value *undefs = UndefValue::get(arg->getType());

            value = builder->CreateShuffleVector(arg, undefs, ConstantVector::get(indices));

            if (op->type.is_scalar()) {
                value = builder->CreateExtractElement(value, ConstantInt::get(i32, 0));
            }

        } else if (op->name == Call::interleave_vectors) {
            internal_assert(0 < op->args.size());
            value = interleave_vectors(op->type, op->args);
        } else if (op->name == Call::debug_to_file) {
            internal_assert(op->args.size() == 9);
            const StringImm *filename = op->args[0].as<StringImm>();
            const Load *func = op->args[1].as<Load>();
            internal_assert(func && filename) << "Malformed debug_to_file node\n";
            // Grab the function from the initial module
            llvm::Function *debug_to_file = module->getFunction("halide_debug_to_file");
            internal_assert(debug_to_file) << "Could not find halide_debug_to_file function in initial module\n";

            // Make the filename a global string constant
            Value *user_context = get_user_context();
            Value *char_ptr = codegen(Expr(filename));
            Value *data_ptr = symbol_table.get(func->name + ".host");
            data_ptr = builder->CreatePointerCast(data_ptr, i8->getPointerTo());
            vector<Value *> args = vec(user_context, char_ptr, data_ptr);
            for (size_t i = 3; i < 9; i++) {
                debug(4) << op->args[i];
                args.push_back(codegen(op->args[i]));
            }

            debug(4) << "Creating call to debug_to_file\n";

            value = builder->CreateCall(debug_to_file, args);
        } else if (op->name == Call::bitwise_and) {
            internal_assert(op->args.size() == 2);
            value = builder->CreateAnd(codegen(op->args[0]), codegen(op->args[1]));
        } else if (op->name == Call::bitwise_xor) {
            internal_assert(op->args.size() == 2);
            value = builder->CreateXor(codegen(op->args[0]), codegen(op->args[1]));
        } else if (op->name == Call::bitwise_or) {
            internal_assert(op->args.size() == 2);
            value = builder->CreateOr(codegen(op->args[0]), codegen(op->args[1]));
        } else if (op->name == Call::bitwise_not) {
            internal_assert(op->args.size() == 1);
            value = builder->CreateNot(codegen(op->args[0]));
        } else if (op->name == Call::reinterpret) {
            internal_assert(op->args.size() == 1);
            Type dst = op->type;
            Type src = op->args[0].type();
            llvm::Type *llvm_dst = llvm_type_of(dst);
            value = codegen(op->args[0]);
            if (src.is_handle() && !dst.is_handle()) {
                internal_assert(dst.is_uint() && dst.bits == 64);

                // Handle -> UInt64
                llvm::DataLayout d(module);
                if (d.getPointerSize() == 4) {
                    llvm::Type *intermediate = llvm_type_of(UInt(32, dst.width));
                    value = builder->CreatePtrToInt(value, intermediate);
                    value = builder->CreateZExt(value, llvm_dst);
                } else if (d.getPointerSize() == 8) {
                    value = builder->CreatePtrToInt(value, llvm_dst);
                } else {
                    internal_error << "Pointer size is neither 4 nor 8 bytes\n";
                }

            } else if (dst.is_handle() && !src.is_handle()) {
                internal_assert(dst.is_uint() && dst.bits == 64);

                // UInt64 -> Handle
                llvm::DataLayout d(module);
                if (d.getPointerSize() == 4) {
                    llvm::Type *intermediate = llvm_type_of(UInt(32, src.width));
                    value = builder->CreateTrunc(value, intermediate);
                    value = builder->CreateIntToPtr(value, llvm_dst);
                } else if (d.getPointerSize() == 8) {
                    value = builder->CreateIntToPtr(value, llvm_dst);
                } else {
                    internal_error << "Pointer size is neither 4 nor 8 bytes\n";
                }

            } else {
                value = builder->CreateBitCast(codegen(op->args[0]), llvm_dst);
            }
        } else if (op->name == Call::shift_left) {
            internal_assert(op->args.size() == 2);
            value = builder->CreateShl(codegen(op->args[0]), codegen(op->args[1]));
        } else if (op->name == Call::shift_right) {
            internal_assert(op->args.size() == 2);
            if (op->type.is_int()) {
                value = builder->CreateAShr(codegen(op->args[0]), codegen(op->args[1]));
            } else {
                value = builder->CreateLShr(codegen(op->args[0]), codegen(op->args[1]));
            }
        } else if (op->name == Call::abs) {

            internal_assert(op->args.size() == 1);

            // Check if an appropriate vector abs for this type exists in the initial module
            Type t = op->args[0].type();
            string name = (t.is_float() ? "abs_f" : "abs_i") + int_to_string(t.bits);
            llvm::Function * builtin_abs =
                find_vector_runtime_function(name, op->type.width).first;

            if (t.is_vector() && builtin_abs) {
                codegen(Call::make(op->type, name, op->args, Call::Extern));
            } else {
                // Generate select(x >= 0, x, -x) instead
                Value *arg = codegen(op->args[0]);
                Value *zero = Constant::getNullValue(arg->getType());
                Value *cmp, *neg;
                if (t.is_float()) {
                    cmp = builder->CreateFCmpOGE(arg, zero);
                    neg = builder->CreateFSub(zero, arg);
                } else {
                    internal_assert(t.is_int());
                    cmp = builder->CreateICmpSGE(arg, zero);
                    neg = builder->CreateSub(zero, arg);
                }
                value = builder->CreateSelect(cmp, arg, neg);
            }
        } else if (op->name == Call::absd) {

            internal_assert(op->args.size() == 2);

            Expr a = op->args[0];
            Expr b = op->args[1];

            // Check if an appropriate vector abs for this type exists in the initial module
            Type t = a.type();
            string name;
            if (t.is_float()) {
                codegen(abs(a - b));
                return;
            } else if (t.is_int()) {
                name = "absd_i" + int_to_string(t.bits);
            } else {
                name = "absd_u" + int_to_string(t.bits);
            }

            llvm::Function *builtin_absd =
                find_vector_runtime_function(name, op->type.width).first;

            if (t.is_vector() && builtin_absd) {
                codegen(Call::make(op->type, name, op->args, Call::Extern));
            } else {
                // Use a select instead
                codegen(Select::make(a < b, b - a, a - b));
            }

        } else if (op->name == Call::copy_buffer_t) {
            // Make some memory for this buffer_t
            Value *dst = create_alloca_at_entry(buffer_t_type, 1);
            Value *src = codegen(op->args[0]);
            src = builder->CreatePointerCast(src, buffer_t_type->getPointerTo());
            src = builder->CreateLoad(src);
            builder->CreateStore(src, dst);
            value = dst;
        } else if (op->name == Call::create_buffer_t) {
            // Make some memory for this buffer_t
            Value *buffer = create_alloca_at_entry(buffer_t_type, 1);

            // Populate the fields
            internal_assert(op->args[0].type().is_handle())
                << "The first argument to create_buffer_t must be a Handle\n";
            Value *host_ptr = codegen(op->args[0]);
            host_ptr = builder->CreatePointerCast(host_ptr, i8->getPointerTo());
            builder->CreateStore(host_ptr, buffer_host_ptr(buffer));

            // Type check integer arguments
            for (size_t i = 1; i < op->args.size(); i++) {
                internal_assert(op->args[i].type() == Int(32))
                    << "All arguments to create_buffer_t beyond the first must have type Int(32)\n";
            }

            Value *elem_size = codegen(op->args[1]);
            builder->CreateStore(elem_size, buffer_elem_size_ptr(buffer));

            int dims = op->args.size()/3;
            for (int i = 0; i < 4; i++) {
                Value *min, *extent, *stride;
                if (i < dims) {
                    min    = codegen(op->args[i*3+2]);
                    extent = codegen(op->args[i*3+3]);
                    stride = codegen(op->args[i*3+4]);
                } else {
                    min = extent = stride = ConstantInt::get(i32, 0);
                }
                builder->CreateStore(min, buffer_min_ptr(buffer, i));
                builder->CreateStore(extent, buffer_extent_ptr(buffer, i));
                builder->CreateStore(stride, buffer_stride_ptr(buffer, i));
            }

            // This implement sets device pointer and dirty bits to
            // zero. GPU codegen should also catch this and do
            // something smarter.
            builder->CreateStore(ConstantInt::get(i8, 0), buffer_host_dirty_ptr(buffer));
            builder->CreateStore(ConstantInt::get(i8, 0), buffer_dev_dirty_ptr(buffer));
            builder->CreateStore(ConstantInt::get(i64, 0), buffer_dev_ptr(buffer));

            value = buffer;
        } else if (op->name == Call::extract_buffer_min) {
            internal_assert(op->args.size() == 2);
            const IntImm *idx = op->args[1].as<IntImm>();
            internal_assert(idx);
            Value *buffer = codegen(op->args[0]);
            buffer = builder->CreatePointerCast(buffer, buffer_t_type->getPointerTo());
            value = buffer_min(buffer, idx->value);
        } else if (op->name == Call::extract_buffer_max) {
            internal_assert(op->args.size() == 2);
            const IntImm *idx = op->args[1].as<IntImm>();
            internal_assert(idx);
            Value *buffer = codegen(op->args[0]);
            buffer = builder->CreatePointerCast(buffer, buffer_t_type->getPointerTo());
            Value *extent = buffer_extent(buffer, idx->value);
            Value *min = buffer_min(buffer, idx->value);
            Value *max_plus_one = builder->CreateNSWAdd(min, extent);
            value = builder->CreateNSWSub(max_plus_one, ConstantInt::get(i32, 1));
        } else if (op->name == Call::rewrite_buffer) {
            int dims = ((int)(op->args.size())-2)/3;
            internal_assert((int)(op->args.size()) == dims*3 + 2);
            internal_assert(dims <= 4);

            Value *buffer = codegen(op->args[0]);

            // Rewrite the buffer_t using the args
            builder->CreateStore(codegen(op->args[1]), buffer_elem_size_ptr(buffer));
            for (int i = 0; i < dims; i++) {
                builder->CreateStore(codegen(op->args[i*3+2]), buffer_min_ptr(buffer, i));
                builder->CreateStore(codegen(op->args[i*3+3]), buffer_extent_ptr(buffer, i));
                builder->CreateStore(codegen(op->args[i*3+4]), buffer_stride_ptr(buffer, i));
            }
            for (int i = dims; i < 4; i++) {
                builder->CreateStore(ConstantInt::get(i32, 0), buffer_min_ptr(buffer, i));
                builder->CreateStore(ConstantInt::get(i32, 0), buffer_extent_ptr(buffer, i));
                builder->CreateStore(ConstantInt::get(i32, 0), buffer_stride_ptr(buffer, i));
            }

            // From the point of view of the continued code (a containing assert stmt), this returns true.
            value = codegen(const_true());
        } else if (op->name == Call::set_host_dirty) {
            internal_assert(op->args.size() == 2);
            Value *buffer = codegen(op->args[0]);
            Value *arg = codegen(op->args[1]);
            builder->CreateStore(arg, buffer_host_dirty_ptr(buffer));
            value = ConstantInt::get(i32, 0);
        } else if (op->name == Call::set_dev_dirty) {
            internal_assert(op->args.size() == 2);
            Value *buffer = codegen(op->args[0]);
            Value *arg = codegen(op->args[1]);
            builder->CreateStore(arg, buffer_dev_dirty_ptr(buffer));
            value = ConstantInt::get(i32, 0);
        } else if (op->name == Call::null_handle) {
            internal_assert(op->args.size() == 0) << "null_handle takes no arguments\n";
            internal_assert(op->type == Handle()) << "null_handle must return a Handle type\n";
            value = ConstantPointerNull::get(i8->getPointerTo());
        } else if (op->name == Call::address_of) {
            internal_assert(op->args.size() == 1) << "address_of takes one argument\n";
            internal_assert(op->type == Handle()) << "address_of must return a Handle type\n";
            const Load *load = op->args[0].as<Load>();
            internal_assert(load) << "The sole argument to address_of must be a Load node\n";
            internal_assert(load->index.type().is_scalar()) << "Can't take the address of a vector load\n";

            value = codegen_buffer_pointer(load->name, load->type, load->index);

        } else if (op->name == Call::trace || op->name == Call::trace_expr) {

            int int_args = (int)(op->args.size()) - 5;
            internal_assert(int_args >= 0);

            // Make a global string for the func name. Should be the same for all lanes.
            Value *name = codegen(unbroadcast(op->args[0]));

            // Codegen the event type. Should be the same for all lanes.
            Value *event_type = codegen(unbroadcast(op->args[1]));

            // Codegen the buffer id
            Expr id = op->args[2];
            Value *realization_id;
            if (id.as<Broadcast>()) {
                realization_id = codegen(unbroadcast(id));
            } else {
                realization_id = codegen(id);
            }

            // Codegen the value index. Should be the same for all lanes.
            Value *value_index = codegen(unbroadcast(op->args[3]));

            // Allocate and populate a stack entry for the value arg
            Type type = op->args[4].type();
            Value *value_stored_array = create_alloca_at_entry(llvm_type_of(type), 1);
            Value *value_stored = codegen(op->args[4]);
            builder->CreateStore(value_stored, value_stored_array);
            value_stored_array = builder->CreatePointerCast(value_stored_array, i8->getPointerTo());

            // Allocate and populate a stack array for the integer args
            Value *coords;
            if (int_args > 0) {
                coords = create_alloca_at_entry(llvm_type_of(op->args[5].type()), int_args);
                for (int i = 0; i < int_args; i++) {
                    Value *coord_ptr = builder->CreateConstInBoundsGEP1_32(coords, i);
                    builder->CreateStore(codegen(op->args[5+i]), coord_ptr);
                }
                coords = builder->CreatePointerCast(coords, i32->getPointerTo());
            } else {
                coords = Constant::getNullValue(i32->getPointerTo());
            }

            StructType *trace_event_type = module->getTypeByName("struct.halide_trace_event");
            user_assert(trace_event_type) << "The module being generated does not support tracing.\n";
            Value *trace_event = create_alloca_at_entry(trace_event_type, 1);

            Value *members[10] = {
                name,
                event_type,
                realization_id,
                ConstantInt::get(i32, type.code),
                ConstantInt::get(i32, type.bits),
                ConstantInt::get(i32, type.width),
                value_index,
                value_stored_array,
                ConstantInt::get(i32, int_args * type.width),
                coords};

            for (size_t i = 0; i < sizeof(members)/sizeof(members[0]); i++) {
                Value *field_ptr = builder->CreateConstInBoundsGEP2_32(trace_event, 0, i);
                builder->CreateStore(members[i], field_ptr);
            }

            // Call the runtime function
            vector<Value *> args(2);
            args[0] = get_user_context();
            args[1] = trace_event;

            llvm::Function *trace_fn = module->getFunction("halide_trace");
            internal_assert(trace_fn);

            value = builder->CreateCall(trace_fn, args);

            if (op->name == Call::trace_expr) {
                value = value_stored;
            }

        } else if (op->name == Call::profiling_timer) {
            internal_assert(op->args.size() == 0);
            llvm::Function *fn = Intrinsic::getDeclaration(module,
                Intrinsic::readcyclecounter, std::vector<llvm::Type*>());
            CallInst *call = builder->CreateCall(fn);
            value = call;
        } else if (op->name == Call::lerp) {
            internal_assert(op->args.size() == 3);
            value = codegen(lower_lerp(op->args[0], op->args[1], op->args[2]));
        } else if (op->name == Call::popcount) {
            internal_assert(op->args.size() == 1);
            std::vector<llvm::Type*> arg_type(1);
            arg_type[0] = llvm_type_of(op->args[0].type());
            llvm::Function *fn = Intrinsic::getDeclaration(module, Intrinsic::ctpop, arg_type);
            CallInst *call = builder->CreateCall(fn, codegen(op->args[0]));
            value = call;
        } else if (op->name == Call::count_leading_zeros ||
                   op->name == Call::count_trailing_zeros) {
            internal_assert(op->args.size() == 1);
            std::vector<llvm::Type*> arg_type(1);
            arg_type[0] = llvm_type_of(op->args[0].type());
            llvm::Function *fn = Intrinsic::getDeclaration(module,
                (op->name == Call::count_leading_zeros) ? Intrinsic::ctlz :
                                                          Intrinsic::cttz,
                arg_type);
            llvm::Value *zero_is_not_undef = llvm::ConstantInt::getFalse(*context);
            llvm::Value *args[2] = { codegen(op->args[0]), zero_is_not_undef };
            CallInst *call = builder->CreateCall(fn, args);
            value = call;
        } else if (op->name == Call::return_second) {
            internal_assert(op->args.size() == 2);
            codegen(op->args[0]);
            value = codegen(op->args[1]);
        } else if (op->name == Call::if_then_else) {
            if (op->type.is_vector()) {
                scalarize(op);

            } else {

                internal_assert(op->args.size() == 3);

                BasicBlock *true_bb = BasicBlock::Create(*context, "true_bb", function);
                BasicBlock *false_bb = BasicBlock::Create(*context, "false_bb", function);
                BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
                builder->CreateCondBr(codegen(op->args[0]), true_bb, false_bb);
                builder->SetInsertPoint(true_bb);
                Value *true_value = codegen(op->args[1]);
                builder->CreateBr(after_bb);

                builder->SetInsertPoint(false_bb);
                Value *false_value = codegen(op->args[2]);
                builder->CreateBr(after_bb);

                builder->SetInsertPoint(after_bb);

                PHINode *phi = builder->CreatePHI(true_value->getType(), 2);
                phi->addIncoming(true_value, true_bb);
                phi->addIncoming(false_value, false_bb);

                value = phi;
            }
        } else if (op->name == Call::make_struct) {
            if (op->type.is_vector()) {
                // Make a vector-of-structs
                scalarize(op);
            } else {
                // Codegen each element.
                assert(!op->args.empty());
                vector<llvm::Value *> args(op->args.size());
                vector<llvm::Type *> types(op->args.size());
                for (size_t i = 0; i < op->args.size(); i++) {
                    args[i] = codegen(op->args[i]);
                    types[i] = args[i]->getType();
                }

                // Create an struct on the stack.
                StructType *struct_t = StructType::create(types);
                Value *ptr = create_alloca_at_entry(struct_t, 1);

                // Put the elements in the struct.
                for (size_t i = 0; i < args.size(); i++) {
                    Value *field_ptr = builder->CreateConstInBoundsGEP2_32(ptr, 0, i);
                    builder->CreateStore(args[i], field_ptr);
                }

                value = ptr;
            }

        } else if (op->name == Call::stringify) {
            assert(!op->args.empty());

            if (op->type.is_vector()) {
                scalarize(op);
            } else {

                // Compute the maximum possible size of the message.
                int buf_size = 1; // One for the terminating zero.
                for (size_t i = 0; i < op->args.size(); i++) {
                    Type t = op->args[i].type();
                    if (op->args[i].as<StringImm>()) {
                        buf_size += op->args[i].as<StringImm>()->value.size();
                    } else if (t.is_int() || t.is_uint()) {
                        buf_size += 19; // 2^64 = 18446744073709551616
                    } else if (t.is_float()) {
                        if (t.bits == 32) {
                            buf_size += 47; // %f format of max negative float
                        } else {
                            buf_size += 14; // Scientific notation with 6 decimal places.
                        }
                    } else {
                        internal_assert(t.is_handle());
                        buf_size += 18; // 0x0123456789abcdef
                    }
                }
                // Round up to a multiple of 16 bytes.
                buf_size = ((buf_size + 15)/16)*16;

                // Clamp to at most 8k.
                if (buf_size > 8192) buf_size = 8192;

                // Allocate a stack array to hold the message.
                llvm::Value *buf = create_alloca_at_entry(i8, buf_size);

                llvm::Value *dst = buf;
                llvm::Value *buf_end = builder->CreateConstGEP1_32(buf, buf_size);

                llvm::Function *append_string  = module->getFunction("halide_string_to_string");
                llvm::Function *append_int64   = module->getFunction("halide_int64_to_string");
                llvm::Function *append_uint64  = module->getFunction("halide_uint64_to_string");
                llvm::Function *append_double  = module->getFunction("halide_double_to_string");
                llvm::Function *append_pointer = module->getFunction("halide_pointer_to_string");

                internal_assert(append_string);
                internal_assert(append_int64);
                internal_assert(append_uint64);
                internal_assert(append_double);
                internal_assert(append_pointer);

                for (size_t i = 0; i < op->args.size(); i++) {
                    const StringImm *s = op->args[i].as<StringImm>();
                    Type t = op->args[i].type();
                    internal_assert(t.width == 1);
                    vector<Value *> call_args(2);
                    call_args[0] = dst;
                    call_args[1] = buf_end;

                    if (s) {
                        call_args.push_back(codegen(op->args[i]));
                        dst = builder->CreateCall(append_string, call_args);
                    } else if (t.is_int()) {
                        call_args.push_back(codegen(Cast::make(Int(64), op->args[i])));
                        call_args.push_back(ConstantInt::get(i32, 1));
                        dst = builder->CreateCall(append_int64, call_args);
                    } else if (t.is_uint()) {
                        call_args.push_back(codegen(Cast::make(UInt(64), op->args[i])));
                        call_args.push_back(ConstantInt::get(i32, 1));
                        dst = builder->CreateCall(append_uint64, call_args);
                    } else if (t.is_float()) {
                        call_args.push_back(codegen(Cast::make(Float(64), op->args[i])));
                        // Use scientific notation for doubles
                        call_args.push_back(ConstantInt::get(i32, t.bits == 64 ? 1 : 0));
                        dst = builder->CreateCall(append_double, call_args);
                    } else {
                        internal_assert(t.is_handle());
                        call_args.push_back(codegen(op->args[i]));
                        dst = builder->CreateCall(append_pointer, call_args);
                    }
                }
                value = buf;
            }
        } else if (op->name == Call::memoize_expr) {
            // Used as an annotation for caching, should be invisible to
            // codegen. Ignore arguments beyond the first as they are only
            // used in the cache key.
            internal_assert(op->args.size() > 0);
            value = codegen(op->args[0]);
        } else if (op->name == Call::copy_memory) {
            value = builder->CreateMemCpy(codegen(op->args[0]),
                                          codegen(op->args[1]),
                                          codegen(op->args[2]), 0);
        } else {
            internal_error << "Unknown intrinsic: " << op->name << "\n";
        }
    } else if (op->call_type == Call::Extern && op->name == "pow_f32") {
        internal_assert(op->args.size() == 2);
        Expr x = op->args[0];
        Expr y = op->args[1];
        Expr e = Internal::halide_exp(Internal::halide_log(x) * y);
        e.accept(this);
    } else if (op->call_type == Call::Extern && op->name == "log_f32") {
        internal_assert(op->args.size() == 1);
        Expr e = Internal::halide_log(op->args[0]);
        e.accept(this);
    } else if (op->call_type == Call::Extern && op->name == "exp_f32") {
        internal_assert(op->args.size() == 1);
        Expr e = Internal::halide_exp(op->args[0]);
        e.accept(this);
    } else if (op->call_type == Call::Extern &&
               (op->name == "is_nan_f32" || op->name == "is_nan_f64")) {
        internal_assert(op->args.size() == 1);
        Value *a = codegen(op->args[0]);
        value = builder->CreateFCmpUNO(a, a);
    } else {
        // It's an extern call.

        // Codegen the args
        vector<Value *> args(op->args.size());
        for (size_t i = 0; i < op->args.size(); i++) {
            args[i] = codegen(op->args[i]);
        }

        llvm::Function *fn = module->getFunction(op->name);

        llvm::Type *result_type = llvm_type_of(op->type);

        // If we can't find it, declare it extern "C"
        if (!fn) {
            // cout << "Didn't find " << op->name << " in initial module. Assuming it's extern." << endl;
            vector<llvm::Type *> arg_types(args.size());
            for (size_t i = 0; i < args.size(); i++) {
                arg_types[i] = args[i]->getType();
                if (arg_types[i]->isVectorTy()) {
                    VectorType *vt = dyn_cast<VectorType>(arg_types[i]);
                    arg_types[i] = vt->getElementType();
                }
            }

            llvm::Type *scalar_result_type = result_type;
            if (result_type->isVectorTy()) {
                VectorType *vt = dyn_cast<VectorType>(result_type);
                scalar_result_type = vt->getElementType();
            }

            FunctionType *func_t = FunctionType::get(scalar_result_type, arg_types, false);

            fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, op->name, module);
            fn->setCallingConv(CallingConv::C);
            debug(4) << "Did not find " << op->name << ". Declared it extern \"C\".\n";
        } else {
            debug(4) << "Found " << op->name << "\n";

            // Halide's type system doesn't preserve pointer types
            // correctly (they just get called "Handle()"), so we may
            // need to pointer cast to the appropriate type. Only look at
            // fixed params (not varags) in llvm function.
            // Functions which take a user context have it added below so the
            // llvm function argument indexing is one greater in that case.
            size_t llvm_arg_offset = function_takes_user_context(op->name) ? 1 : 0;
            FunctionType *func_t = fn->getFunctionType();
            for (size_t i = 0;
                 i < std::min(args.size(),
                              func_t->getNumParams() - llvm_arg_offset);
                 i++) {
                if (op->args[i].type().is_handle()) {
                    llvm::Type *t = func_t->getParamType(i + llvm_arg_offset);

                    // Widen to vector-width as needed. If the
                    // function doesn't actually take a vector,
                    // individual lanes will be extracted below.
                    if (op->args[i].type().is_vector() &&
                        !t->isVectorTy()) {
                        t = VectorType::get(t, op->args[i].type().width);
                    }

                    if (t != args[i]->getType()) {
                        debug(4) << "Pointer casting argument to extern call: "
                                 << op->args[i] << "\n";
                        args[i] = builder->CreatePointerCast(args[i], t);
                    }
                }
            }
        }

        // If any of the args are handles, assume it might access memory
        bool pure = true;
        for (size_t i = 0; i < op->args.size(); i++) {
            if (op->args[i].type().is_handle()) {
                pure = false;
            }
        }

        // We also have several impure runtime functions that do not
        // take a handle.
        if (op->name == "halide_current_time_ns" ||
            op->name == "halide_gpu_thread_barrier") {
            pure = false;
        }

        // Add a user context arg as needed. It's never a vector.
        if (function_takes_user_context(op->name)) {
            debug(4) << "Adding user_context to " << op->name << " args\n";
            args.insert(args.begin(), get_user_context());
        }

        if (op->type.is_scalar()) {
            debug(4) << "Creating scalar call to " << op->name << "\n";
            CallInst *call = builder->CreateCall(fn, args);
            if (pure) {
                call->setDoesNotAccessMemory();
            }
            call->setDoesNotThrow();
            value = call;
        } else {

            // Check if a vector version of the function already
            // exists at some useful width.
            pair<llvm::Function *, int> vec =
                find_vector_runtime_function(op->name, op->type.width);
            llvm::Function *vec_fn = vec.first;
            int w = vec.second;

            if (vec_fn) {
                value = call_intrin(llvm_type_of(op->type), w,
                                    vec_fn->getName(), args);
            } else {

                // No vector version found. Scalarize. Extract each simd
                // lane in turn and do one scalar call to the function.
                value = UndefValue::get(result_type);
                for (int i = 0; i < op->type.width; i++) {
                    Value *idx = ConstantInt::get(i32, i);
                    vector<Value *> arg_lane(args.size());
                    for (size_t j = 0; j < args.size(); j++) {
                        if (args[j]->getType()->isVectorTy()) {
                            arg_lane[j] = builder->CreateExtractElement(args[j], idx);
                        } else {
                            arg_lane[j] = args[j];
                        }
                    }
                    CallInst *call = builder->CreateCall(fn, arg_lane);
                    if (pure) {
                        call->setDoesNotAccessMemory();
                    }
                    call->setDoesNotThrow();
                    value = builder->CreateInsertElement(value, call, idx);
                }
            }
        }
    }
}

void CodeGen::visit(const Let *op) {
    sym_push(op->name, codegen(op->value));
    if (op->value.type() == Int(32)) {
        alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
    }
    value = codegen(op->body);
    if (op->value.type() == Int(32)) {
        alignment_info.pop(op->name);
    }
    sym_pop(op->name);
}

void CodeGen::visit(const LetStmt *op) {
    sym_push(op->name, codegen(op->value));

    if (op->value.type() == Int(32)) {
        alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
    }
    codegen(op->body);
    if (op->value.type() == Int(32)) {
        alignment_info.pop(op->name);
    }
    sym_pop(op->name);
}

void CodeGen::visit(const AssertStmt *op) {
    create_assertion(codegen(op->condition), op->message);
}

Constant *CodeGen::create_string_constant(const string &s) {
    map<string, Constant *>::iterator iter = string_constants.find(s);
    if (iter == string_constants.end()) {
        vector<char> data;
        data.reserve(s.size()+1);
        data.insert(data.end(), s.begin(), s.end());
        data.push_back(0);
        Constant *val = create_constant_binary_blob(data, "str");
        string_constants[s] = val;
        return val;
    } else {
        return iter->second;
    }
}

Constant *CodeGen::create_constant_binary_blob(const vector<char> &data, const string &name) {

    llvm::Type *type = ArrayType::get(i8, data.size());
    GlobalVariable *global = new GlobalVariable(*module, type,
                                                true, GlobalValue::PrivateLinkage,
                                                0, name);
    ArrayRef<unsigned char> data_array((const unsigned char *)&data[0], data.size());
    global->setInitializer(ConstantDataArray::get(*context, data_array));
    global->setAlignment(32);

    Constant *zero = ConstantInt::get(i32, 0);
    Constant *ptr = ConstantExpr::getInBoundsGetElementPtr(global, vec(zero, zero));
    return ptr;
}

void CodeGen::create_assertion(Value *cond, Expr message) {

    if (target.has_feature(Target::NoAsserts)) return;

    // If the condition is a vector, fold it down to a scalar
    VectorType *vt = dyn_cast<VectorType>(cond->getType());
    if (vt) {
        Value *scalar_cond = builder->CreateExtractElement(cond, ConstantInt::get(i32, 0));
        for (unsigned i = 1; i < vt->getNumElements(); i++) {
            Value *lane = builder->CreateExtractElement(cond, ConstantInt::get(i32, i));
            scalar_cond = builder->CreateAnd(scalar_cond, lane);
        }
        cond = scalar_cond;
    }

    // Make a new basic block for the assert
    BasicBlock *assert_fails_bb = BasicBlock::Create(*context, "assert failed", function);
    BasicBlock *assert_succeeds_bb = BasicBlock::Create(*context, "assert succeeded", function);

    // If the condition fails, enter the assert body, otherwise, enter the block after
    builder->CreateCondBr(cond, assert_succeeds_bb, assert_fails_bb, very_likely_branch);

    // Build the failure case
    builder->SetInsertPoint(assert_fails_bb);

    // Codegen the message here, inside the failure case. This may be
    // expensive, and the calls that build the string may appear to be
    // side-effecting to llvm, so it's important to do the codegen
    // right here.
    llvm::Value *msg = codegen(message);

    // Call the error handler
    llvm::Function *error_handler = module->getFunction("halide_error");
    internal_assert(error_handler)
        << "Could not find halide_error in initial module\n";
    debug(4) << "Creating call to error handlers\n";
    builder->CreateCall(error_handler, vec<llvm::Value *>(get_user_context(), msg));

    // Do any architecture-specific cleanup necessary
    debug(4) << "Creating cleanup code\n";
    prepare_for_early_exit();

    // Bail out with error code -1
    builder->CreateRet(ConstantInt::get(i32, -1));

    // Continue on using the success case
    builder->SetInsertPoint(assert_succeeds_bb);
}

void CodeGen::visit(const Pipeline *op) {
    BasicBlock *produce = BasicBlock::Create(*context, std::string("produce ") + op->name, function);
    builder->CreateBr(produce);
    builder->SetInsertPoint(produce);
    codegen(op->produce);

    if (op->update.defined()) {
        BasicBlock *update = BasicBlock::Create(*context, std::string("update ") + op->name, function);
        builder->CreateBr(update);
        builder->SetInsertPoint(update);
        codegen(op->update);
    }

    BasicBlock *consume = BasicBlock::Create(*context, std::string("consume ") + op->name, function);
    builder->CreateBr(consume);
    builder->SetInsertPoint(consume);
    codegen(op->consume);
}

void CodeGen::visit(const For *op) {
    Value *min = codegen(op->min);
    Value *extent = codegen(op->extent);

    if (op->for_type == For::Serial) {
        Value *max = builder->CreateNSWAdd(min, extent);

        BasicBlock *preheader_bb = builder->GetInsertBlock();

        // Make a new basic block for the loop
        BasicBlock *loop_bb = BasicBlock::Create(*context, std::string("for ") + op->name, function);
        // Create the block that comes after the loop
        BasicBlock *after_bb = BasicBlock::Create(*context, std::string("end for ") + op->name, function);

        // If min < max, fall through to the loop bb
        Value *enter_condition = builder->CreateICmpSLT(min, max);
        builder->CreateCondBr(enter_condition, loop_bb, after_bb, very_likely_branch);
        builder->SetInsertPoint(loop_bb);

        // Make our phi node.
        PHINode *phi = builder->CreatePHI(i32, 2);
        phi->addIncoming(min, preheader_bb);

        // Within the loop, the variable is equal to the phi value
        sym_push(op->name, phi);

        // Emit the loop body
        codegen(op->body);

        // Update the counter
        Value *next_var = builder->CreateNSWAdd(phi, ConstantInt::get(i32, 1));

        // Add the back-edge to the phi node
        phi->addIncoming(next_var, builder->GetInsertBlock());

        // Maybe exit the loop
        Value *end_condition = builder->CreateICmpNE(next_var, max);
        builder->CreateCondBr(end_condition, loop_bb, after_bb);

        builder->SetInsertPoint(after_bb);

        // Pop the loop variable from the scope
        sym_pop(op->name);
    } else if (op->for_type == For::Parallel) {

        debug(3) << "Entering parallel for loop over " << op->name << "\n";

        // Find every symbol that the body of this loop refers to
        // and dump it into a closure
        Closure closure(op->body, op->name, buffer_t_type);

        // Allocate a closure
        StructType *closure_t = closure.build_type(context);
        Value *ptr = create_alloca_at_entry(closure_t, 1);

        // Fill in the closure
        closure.pack_struct(ptr, symbol_table, builder);

        // Make a new function that does one iteration of the body of the loop
        llvm::Type *voidPointerType = (llvm::Type *)(i8->getPointerTo());
        FunctionType *func_t = FunctionType::get(i32, vec(voidPointerType, i32, voidPointerType), false);
        llvm::Function *containing_function = function;
        function = llvm::Function::Create(func_t, llvm::Function::InternalLinkage,
                                          "par for " + function->getName() + "_" + op->name, module);
        function->setDoesNotAlias(3);

        // Make the initial basic block and jump the builder into the new function
        BasicBlock *call_site = builder->GetInsertBlock();
        BasicBlock *block = BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(block);

        // Get the user context value before swapping out the symbol table.
        Value *user_context = get_user_context();

        // Make a new scope to use
        Scope<Value *> saved_symbol_table;
        symbol_table.swap(saved_symbol_table);

        // Get the function arguments

        // The user context is first argument of the function; it's
        // important that we override the name to be "__user_context",
        // since the LLVM function has a random auto-generated name for
        // this argument.
        llvm::Function::arg_iterator iter = function->arg_begin();
        sym_push("__user_context", iter);

        // Next is the loop variable.
        ++iter;
        sym_push(op->name, iter);

        // The closure pointer is the third and last argument.
        ++iter;
        iter->setName("closure");
        Value *closure_handle = builder->CreatePointerCast(iter, closure_t->getPointerTo());
        // Load everything from the closure into the new scope
        closure.unpack_struct(symbol_table, closure_handle, builder);

        // Generate the new function body
        codegen(op->body);

        // Return success
        builder->CreateRet(ConstantInt::get(i32, 0));

        // Move the builder back to the main function and call do_par_for
        builder->SetInsertPoint(call_site);
        llvm::Function *do_par_for = module->getFunction("halide_do_par_for");
        internal_assert(do_par_for) << "Could not find halide_do_par_for in initial module\n";
        do_par_for->setDoesNotAlias(5);
        //do_par_for->setDoesNotCapture(5);
        ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());
        vector<Value *> args = vec<Value *>(user_context, function, min, extent, ptr);
        debug(4) << "Creating call to do_par_for\n";
        Value *result = builder->CreateCall(do_par_for, args);

        debug(3) << "Leaving parallel for loop over " << op->name << "\n";

        // Now restore the scope
        symbol_table.swap(saved_symbol_table);
        function = containing_function;

        // Check for success
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
        create_assertion(did_succeed, "Failure inside parallel for loop");

    } else {
        internal_error << "Unknown type of For node. Only Serial and Parallel For nodes should survive down to codegen.\n";
    }
}

void CodeGen::visit(const Store *op) {
    Value *val = codegen(op->value);
    Halide::Type value_type = op->value.type();
    bool possibly_misaligned = (might_be_misaligned.find(op->name) != might_be_misaligned.end());
    // Scalar
    if (value_type.is_scalar()) {
        Value *ptr = codegen_buffer_pointer(op->name, value_type, op->index);
        StoreInst *store = builder->CreateAlignedStore(val, ptr, op->value.type().bytes());
        add_tbaa_metadata(store, op->name, op->index);
    } else {
        int alignment = op->value.type().bytes();
        const Ramp *ramp = op->index.as<Ramp>();
        if (ramp && is_one(ramp->stride)) {

            int native_bits = native_vector_bits();

            // Boost the alignment if possible, up to the native vector width.
            ModulusRemainder mod_rem = modulus_remainder(ramp->base, alignment_info);
            if (!possibly_misaligned) {
                while ((mod_rem.remainder & 1) == 0 &&
                       (mod_rem.modulus & 1) == 0 &&
                       alignment < native_bits) {
                    mod_rem.modulus /= 2;
                    mod_rem.remainder /= 2;
                    alignment *= 2;
                }
            }

            // For dense vector stores wider than the native vector
            // width, bust them up into native vectors.
            int store_lanes = op->value.type().width;
            int native_lanes = native_bits / op->value.type().bits;

            for (int i = 0; i < store_lanes; i += native_lanes) {
                int slice_lanes = std::min(native_lanes, store_lanes - i);
                Expr slice_base = simplify(ramp->base + i);
                Expr slice_index = slice_lanes == 1 ? slice_base : Ramp::make(slice_base, 1, slice_lanes);
                Value *slice_val = slice_vector(val, i, slice_lanes);
                Value *elt_ptr = codegen_buffer_pointer(op->name, value_type.element_of(), slice_base);
                Value *vec_ptr = builder->CreatePointerCast(elt_ptr, slice_val->getType()->getPointerTo());
                StoreInst *store = builder->CreateAlignedStore(slice_val, vec_ptr, alignment);
                add_tbaa_metadata(store, op->name, slice_index);
            }
        } else if (ramp) {
            Value *ptr = codegen_buffer_pointer(op->name, value_type.element_of(), ramp->base);
            const IntImm *const_stride = ramp->stride.as<IntImm>();
            Value *stride = codegen(ramp->stride);
            // Scatter without generating the indices as a vector
            for (int i = 0; i < ramp->width; i++) {
                Constant *lane = ConstantInt::get(i32, i);
                Value *v = builder->CreateExtractElement(val, lane);
                if (const_stride) {
                    // Use a constant offset from the base pointer
                    Value *p = builder->CreateConstInBoundsGEP1_32(ptr, const_stride->value * i);
                    StoreInst *store = builder->CreateStore(v, p);
                    add_tbaa_metadata(store, op->name, op->index);
                } else {
                    // Increment the pointer by the stride for each element
                    StoreInst *store = builder->CreateStore(v, ptr);
                    add_tbaa_metadata(store, op->name, op->index);
                    ptr = builder->CreateInBoundsGEP(ptr, stride);
                }
            }
        } else {
            // Scatter
            Value *index = codegen(op->index);
            for (int i = 0; i < value_type.width; i++) {
                Value *lane = ConstantInt::get(i32, i);
                Value *idx = builder->CreateExtractElement(index, lane);
                Value *v = builder->CreateExtractElement(val, lane);
                Value *ptr = codegen_buffer_pointer(op->name, value_type.element_of(), idx);
                StoreInst *store = builder->CreateStore(v, ptr);
                add_tbaa_metadata(store, op->name, op->index);
            }
        }
    }

}


void CodeGen::visit(const Block *op) {
    codegen(op->first);
    if (op->rest.defined()) codegen(op->rest);
}

void CodeGen::visit(const Realize *op) {
    internal_error << "Realize encountered during codegen\n";
}

void CodeGen::visit(const Provide *op) {
    internal_error << "Provide encountered during codegen\n";
}

void CodeGen::visit(const IfThenElse *op) {
    BasicBlock *true_bb = BasicBlock::Create(*context, "true_bb", function);
    BasicBlock *false_bb = BasicBlock::Create(*context, "false_bb", function);
    BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
    builder->CreateCondBr(codegen(op->condition), true_bb, false_bb);

    builder->SetInsertPoint(true_bb);
    codegen(op->then_case);
    builder->CreateBr(after_bb);

    builder->SetInsertPoint(false_bb);
    if (op->else_case.defined()) {
        codegen(op->else_case);
    }
    builder->CreateBr(after_bb);

    builder->SetInsertPoint(after_bb);
}

void CodeGen::visit(const Evaluate *op) {
    codegen(op->value);

    // Discard result
    value = NULL;
}

Value *CodeGen::create_alloca_at_entry(llvm::Type *t, int n, const string &name) {
    llvm::BasicBlock *here = builder->GetInsertBlock();
    llvm::BasicBlock *entry = &here->getParent()->getEntryBlock();
    if (entry->empty()) {
        builder->SetInsertPoint(entry);
    } else {
        builder->SetInsertPoint(entry->getFirstInsertionPt());
    }
    Value *size = ConstantInt::get(i32, n);
    Value *ptr = builder->CreateAlloca(t, size, name);
    builder->SetInsertPoint(here);
    return ptr;
}

Value *CodeGen::get_user_context() const {
    Value *ctx = sym_get("__user_context", false);
    if (!ctx) {
        ctx = ConstantPointerNull::get(i8->getPointerTo()); // void*
    }
    return ctx;
}


Value *CodeGen::call_intrin(Type result_type, int intrin_vector_width,
                            const string &name, vector<Expr> args) {
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    return call_intrin(llvm_type_of(result_type),
                       intrin_vector_width,
                       name, arg_values);
}

Value *CodeGen::call_intrin(llvm::Type *result_type, int intrin_vector_width,
                            const string &name, vector<Value *> arg_values) {
    int arg_vector_width = (int)result_type->getVectorNumElements();

    if (intrin_vector_width != arg_vector_width) {
        // Cut up each arg into appropriately-sized pieces, call the
        // intrinsic on each, then splice together the results.
        vector<Value *> results;
        for (int start = 0; start < arg_vector_width; start += intrin_vector_width) {
            vector<Value *> args;
            for (size_t i = 0; i < arg_values.size(); i++) {
                if (arg_values[i]->getType()->isVectorTy()) {
                    internal_assert((int)arg_values[i]->getType()->getVectorNumElements() == arg_vector_width);
                    args.push_back(slice_vector(arg_values[i], start, intrin_vector_width));
                } else {
                    args.push_back(arg_values[i]);
                }
            }

            llvm::Type *result_slice_type =
                llvm::VectorType::get(result_type->getScalarType(), intrin_vector_width);

            results.push_back(call_intrin(result_slice_type, intrin_vector_width, name, args));
        }
        Value *result = concat_vectors(results);
        return slice_vector(result, 0, arg_vector_width);
    }

    vector<llvm::Type *> arg_types(arg_values.size());
    for (size_t i = 0; i < arg_values.size(); i++) {
        arg_types[i] = arg_values[i]->getType();
    }

    llvm::Function *fn = module->getFunction(name);

    if (!fn) {
        llvm::Type *intrinsic_result_type = VectorType::get(result_type->getScalarType(), intrin_vector_width);
        FunctionType *func_t = FunctionType::get(intrinsic_result_type, arg_types, false);
        fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module);
        fn->setCallingConv(CallingConv::C);
    }

    CallInst *call = builder->CreateCall(fn, arg_values);

    call->setDoesNotAccessMemory();
    call->setDoesNotThrow();

    return call;
}

Value *CodeGen::slice_vector(Value *vec, int start, int size) {
    int vec_lanes = vec->getType()->getVectorNumElements();

    if (start == 0 && size == vec_lanes) {
        return vec;
    }

    vector<Constant *> indices(size);
    for (int i = 0; i < size; i++) {
        int idx = start + i;
        if (idx >= 0 && idx < vec_lanes) {
            indices[i] = ConstantInt::get(i32, idx);
        } else {
            indices[i] = UndefValue::get(i32);
        }
    }
    Constant *indices_vec = ConstantVector::get(indices);
    Value *undefs = UndefValue::get(vec->getType());
    return builder->CreateShuffleVector(vec, undefs, indices_vec);
}

Value *CodeGen::concat_vectors(const vector<Value *> &v) {
    if (v.size() == 1) return v[0];

    internal_assert(!v.empty());

    vector<Value *> vecs = v;

    while (vecs.size() > 1) {
        vector<Value *> new_vecs;

        for (size_t i = 0; i < vecs.size()-1; i += 2) {
            Value *v1 = vecs[i];
            Value *v2 = vecs[i+1];

            int w1 = v1->getType()->getVectorNumElements();
            int w2 = v2->getType()->getVectorNumElements();

            // Possibly pad one of the vectors to match widths.
            if (w1 < w2) {
                v1 = slice_vector(v1, 0, w2);
            } else if (w2 < w1) {
                v2 = slice_vector(v2, 0, w1);
            }
            int w_matched = std::max(w1, w2);

            internal_assert(v1->getType() == v2->getType());

            vector<Constant *> indices(w1 + w2);
            for (int i = 0; i < w1; i++) {
                indices[i] = ConstantInt::get(i32, i);
            }
            for (int i = 0; i < w2; i++) {
                indices[w1 + i] = ConstantInt::get(i32, w_matched + i);
            }
            Constant *indices_vec = ConstantVector::get(indices);

            Value *merged = builder->CreateShuffleVector(v1, v2, indices_vec);

            new_vecs.push_back(merged);
        }

        // If there were an odd number of them, we need to also push
        // the one that didn't get merged.
        if (vecs.size() & 1) {
            new_vecs.push_back(vecs.back());
        }

        vecs.swap(new_vecs);
    }

    return vecs[0];
}

std::pair<llvm::Function *, int> CodeGen::find_vector_runtime_function(const std::string &name, int width) {
    // Check if a vector version of the function already
    // exists at some useful width. We use the naming
    // convention that a N-wide version of a function foo is
    // called fooxN. All of our intrinsics are power-of-two
    // sized, so starting at the first power of two >= the
    // vector width, we'll try all powers of two in decreasing
    // order.
    vector<int> sizes_to_try;
    int w = 1;
    while (w < width) w *= 2;
    for (int i = w; i > 1; i /= 2) {
        sizes_to_try.push_back(i);
    }

    // If none of those match, we'll also try doubling
    // the width up to the next power of two (this is to catch
    // cases where we're a 64-bit vector and have a 128-bit
    // vector implementation).
    sizes_to_try.push_back(w*2);

    for (size_t i = 0; i < sizes_to_try.size(); i++) {
        int w = sizes_to_try[i];
        llvm::Function *vec_fn = module->getFunction(name + "x" + int_to_string(w));
        if (vec_fn) {
            return std::make_pair(vec_fn, w);
        }
    }

    return std::make_pair<llvm::Function *, int>(NULL, 0);
}

template<>
EXPORT RefCount &ref_count<CodeGen>(const CodeGen *p) {return p->ref_count;}

template<>
EXPORT void destroy<CodeGen>(const CodeGen *p) {delete p;}

}}
