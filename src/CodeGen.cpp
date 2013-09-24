#include <iostream>
#include "IRPrinter.h"
#include "CodeGen.h"
#include "IROperator.h"
#include "Debug.h"
#include "CodeGen_C.h"
#include "Function.h"
#include "Deinterleave.h"
#include "Simplify.h"
#include "JITCompiledModule.h"
#include "CodeGen_Internal.h"
#include "Lerp.h"

#include <sstream>

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
#if WITH_X86
#define InitializeX86Target()       InitializeTarget(X86)
#define InitializeX86AsmParser()    InitializeAsmParser(X86)
#define InitializeX86AsmPrinter()   InitializeAsmPrinter(X86)
#endif

#if WITH_ARM
#define InitializeARMTarget()       InitializeTarget(ARM)
#define InitializeARMAsmParser()    InitializeAsmParser(ARM)
#define InitializeARMAsmPrinter()   InitializeAsmPrinter(ARM)
#endif

#if WITH_PTX
#define InitializeNVPTXTarget()       InitializeTarget(NVPTX)
#define InitializeNVPTXAsmParser()    InitializeAsmParser(NVPTX)
#define InitializeNVPTXAsmPrinter()   InitializeAsmPrinter(NVPTX)
#endif

CodeGen::CodeGen() :
    module(NULL), owns_module(false),
    function(NULL), context(NULL),
    builder(NULL),
    value(NULL),
    void_t(NULL), i1(NULL), i8(NULL), i16(NULL), i32(NULL), i64(NULL),
    f16(NULL), f32(NULL), f64(NULL),
    buffer_t(NULL), need_stack_restore(false) {

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
    if (builder) delete builder;

    context = new LLVMContext();
    builder = new IRBuilder<>(*context);

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

// llvm includes above disable assert.  Include Util.h here
// to reenable assert.
#include "Util.h"

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
bool CodeGen::llvm_NVPTX_enabled = false;

void CodeGen::compile(Stmt stmt, string name, const vector<Argument> &args) {
    assert(module && context && builder && "The CodeGen subclass should have made an initial module before calling CodeGen::compile");
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
            wrapper_args[i] = builder->CreatePointerCast(ptr, buffer_t->getPointerTo());
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
    optimize_module();

    if (debug::debug_level >= 2) {
        module->dump();
    }
}

llvm::Type *CodeGen::llvm_type_of(Type t) {
    return Internal::llvm_type_of(context, t);
}

JITCompiledModule CodeGen::compile_to_function_pointers() {
    assert(module && "No module defined. Must call compile before calling compile_to_function_pointer");

    JITCompiledModule m;

    m.compile_module(this, module, function_name);

    // We now relinquish ownership of the module, and give it to the
    // JITCompiledModule object that we're returning.
    owns_module = false;

    return m;
}

void CodeGen::optimize_module() {
    FunctionPassManager function_pass_manager(module);
    PassManager module_pass_manager;

    // Make sure things marked as always-inline get inlined
    module_pass_manager.add(createAlwaysInlinerPass());

    PassManagerBuilder b;
    b.OptLevel = 3;
    b.populateFunctionPassManager(function_pass_manager);
    b.populateModulePassManager(module_pass_manager);

    llvm::Function *fn = module->getFunction(function_name);
    assert(fn && "Could not find function inside llvm module");

    if (debug::debug_level >= 3) {
        module->dump();
    }

    // Run optimization passes
    module_pass_manager.run(*module);
    function_pass_manager.doInitialization();
    function_pass_manager.run(*fn);
    function_pass_manager.doFinalization();

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
    debug(1) << "Compiling to native code...\n";
    debug(2) << "Target triple: " << module->getTargetTriple() << "\n";

    const Target *target = TargetRegistry::lookupTarget(module->getTargetTriple(), error_string);
    if (!target) {
        cout << error_string << endl;
        TargetRegistry::printRegisteredTargetsForVersion();
    }
    assert(target && "Could not create target");

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
    options.EnableSegmentedStacks = false;
    options.UseInitArray = false;

    TargetMachine *target_machine =
        target->createTargetMachine(module->getTargetTriple(),
                                    mcpu(), mattrs(),
                                    options,
                                    Reloc::PIC_,
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

    #if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
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

llvm::Value *CodeGen::sym_get(const string &name, bool must_succeed) {
    // look in the symbol table
    if (!symbol_table.contains(name)) {
        if (must_succeed) {
            std::cerr << "Symbol not found: " << name << "\n";

            if (debug::debug_level > 0) {
                std::cerr << "The following names are in scope:\n";
                std::cerr << symbol_table << "\n";
            }

            assert(false);
        } else {
            return NULL;
        }
    }
    return symbol_table.get(name);
}

bool CodeGen::sym_exists(const string &name) {
    return symbol_table.contains(name);
}

// Take an llvm Value representing a pointer to a buffer_t,
// and populate the symbol table with its constituent parts
void CodeGen::unpack_buffer(string name, llvm::Value *buffer) {
    Value *host_ptr = buffer_host(buffer);
    Value *dev_ptr = buffer_dev(buffer);

    /*
    // Check it's 32-byte aligned

    // Andrew: There's no point. External buffers come in with unknown
    // mins, so accesses to them are never aligned anyway.

    Value *base = builder->CreatePtrToInt(host_ptr, i64);
    Value *check_alignment = builder->CreateAnd(base, 0x1f);
    check_alignment = builder->CreateIsNull(check_alignment);

    string error_message = "Buffer " + name + " is not 32-byte aligned";
    create_assertion(check_alignment, error_message);
    */

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
    buffer_t = module->getTypeByName("struct.buffer_t");
    assert(buffer_t && "Did not find buffer_t in initial module");
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
    assert(e.defined());
    debug(4) << "Codegen: " << e.type() << ", " << e << "\n";
    value = NULL;
    e.accept(this);
    assert(value && "Codegen of an expr did not produce an llvm value");
    return value;
}

void CodeGen::codegen(Stmt s) {
    assert(s.defined());
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
        // Signed integer division sucks. It should round down (to
        // make upsampling kernels work across the zero boundary), but
        // it doesn't.

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

        // We get the rounding to work correctly by introducing a pre
        // and post offset by one. The offsets depend on the sign of
        // the numerator and denominator

        /* Here's the C code that we're trying to match (due to Len Hamey)
        T axorb = a ^ b;
        post = a != 0 ? ((axorb) >> (t.bits-1)) : 0;
        pre = a < 0 ? -post : post;
        T num = a + pre;
        T quo = num / b;
        T result = quo + post;
        */

        Value *a = codegen(op->a), *b = codegen(op->b);

        Value *a_xor_b = builder->CreateXor(a, b);
        Value *shift = ConstantInt::get(a->getType(), op->a.type().bits-1);
        Value *a_xor_b_sign = builder->CreateAShr(a_xor_b, shift);
        Value *zero = ConstantInt::get(a->getType(), 0);
        Value *a_not_zero = builder->CreateICmpNE(a, zero);
        Value *post = builder->CreateSelect(a_not_zero, a_xor_b_sign, zero);
        Value *minus_post = builder->CreateNeg(post);
        Value *a_lt_zero = builder->CreateICmpSLT(a, zero);
        Value *pre = builder->CreateSelect(a_lt_zero, minus_post, post);
        Value *num = builder->CreateAdd(a, pre);
        Value *quo = builder->CreateSDiv(num, b);
        value = builder->CreateAdd(quo, post);
    }
}

void CodeGen::visit(const Mod *op) {
    // To match our definition of division, mod should have this behavior:
    // 3 % 2 -> 1;
    // -3 % 2 -> 1;
    // 3 % -2 -> -1;
    // -3 % -2 -> -1;
    // I.e. the remainder should be between zero and b

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

            // Match this non-overflowing C code due to Len Hamey
            /*
              T rem = a % b;
              rem = rem + (rem != 0 && (rem ^ b) < 0 ? b : 0);
            */

            Value *rem = builder->CreateSRem(a, b);
            Value *zero = ConstantInt::get(rem->getType(), 0);
            Value *rem_not_zero = builder->CreateICmpNE(rem, zero);
            Value *rem_xor_b = builder->CreateXor(rem, b);
            Value *rem_xor_b_lt_zero = builder->CreateICmpSLT(rem_xor_b, zero);
            Value *need_to_add_b = builder->CreateAnd(rem_not_zero, rem_xor_b_lt_zero);
            Value *offset = builder->CreateSelect(need_to_add_b, b, zero);
            value = builder->CreateNSWAdd(rem, offset);
        }
    }
}

void CodeGen::visit(const Min *op) {
    Expr a = Variable::make(op->a.type(), "a");
    Expr b = Variable::make(op->a.type(), "b");
    Expr equiv = Let::make("a", op->a, Let::make("b", op->b, Select::make(a < b, a, b)));
    value = codegen(equiv);
}

void CodeGen::visit(const Max *op) {
    Expr a = Variable::make(op->a.type(), "a");
    Expr b = Variable::make(op->a.type(), "b");
    Expr equiv = Let::make("a", op->a, Let::make("b", op->b, Select::make(a > b, a, b)));
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
    if (module->getPointerSize() == llvm::Module::Pointer64) {
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
    if (module->getPointerSize() == llvm::Module::Pointer64) {
        index = builder->CreateIntCast(index, i64, true);
    }

    return builder->CreateInBoundsGEP(base_address, index);
}

void CodeGen::add_tbaa_metadata(llvm::Instruction *inst, string buffer) {
    // Add type-based-alias-analysis metadata to the pointer, so that
    // loads and stores to different buffers can get reordered.
    MDNode *tbaa_root = MDNode::get(*context, vec<Value *>(MDString::get(*context, "Halide buffer")));
    MDNode *tbaa = MDNode::get(*context, vec<Value *>(MDString::get(*context, buffer), tbaa_root));
    inst->setMetadata("tbaa", tbaa);
}

void CodeGen::visit(const Load *op) {

    // There are several cases. Different architectures may wish to override some.
    if (op->type.is_scalar()) {
        // Scalar loads
        Value *ptr = codegen_buffer_pointer(op->name, op->type, op->index);
        LoadInst *load = builder->CreateAlignedLoad(ptr, op->type.bytes());
        add_tbaa_metadata(load, op->name);
        value = load;
    } else {
        int alignment = op->type.bytes();
        const Ramp *ramp = op->index.as<Ramp>();
        const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;

        bool internal = !op->image.defined() && !op->param.defined();

        if (ramp && internal) {
            // If it's an internal allocation, we can boost the
            // alignment using the results of the modulus remainder
            // analysis
            ModulusRemainder mod_rem = modulus_remainder(ramp->base);
            alignment *= gcd(gcd(mod_rem.modulus, mod_rem.remainder), 32);
        }

        if (ramp && stride && stride->value == 1) {
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), ramp->base);
            ptr = builder->CreatePointerCast(ptr, llvm_type_of(op->type)->getPointerTo());
            LoadInst *load = builder->CreateAlignedLoad(ptr, alignment);
            add_tbaa_metadata(load, op->name);
            value = load;
        } else if (ramp && stride && stride->value == 2) {
            // Load two vectors worth and then shuffle

            // If the base ends in an odd constant, then subtract one
            // and do a different shuffle. This helps expressions like
            // (f(2*x) + f(2*x+1) share loads.
            Expr new_base;
            const Add *add = ramp->base.as<Add>();
            const IntImm *offset = add ? add->b.as<IntImm>() : NULL;
            if (offset) {
                if (offset->value == 1) {
                    new_base = add->a;
                } else {
                    new_base = add->a + (offset->value - 1);
                }
            } else {
                new_base = ramp->base;
            }

            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), new_base);
            ptr = builder->CreatePointerCast(ptr, llvm_type_of(op->type)->getPointerTo());
            LoadInst *a = builder->CreateAlignedLoad(ptr, alignment);
            add_tbaa_metadata(a, op->name);
            ptr = builder->CreateConstInBoundsGEP1_32(ptr, 1);
            int bytes = (op->type.bits * op->type.width)/8;
            LoadInst *b = builder->CreateAlignedLoad(ptr, gcd(alignment, bytes));
            add_tbaa_metadata(b, op->name);
            vector<Constant *> indices(ramp->width);
            for (int i = 0; i < ramp->width; i++) {
                indices[i] = ConstantInt::get(i32, i*2 + (offset ? 1 : 0));
            }
            value = builder->CreateShuffleVector(a, b, ConstantVector::get(indices));
        } else if (ramp && stride && stride->value == -1) {
            // Load the vector and then flip it in-place
            Expr base = ramp->base - ramp->width + 1;

            // Re-do alignment analysis for the flipped index
            if (internal) {
                alignment = op->type.bytes();
                ModulusRemainder mod_rem = modulus_remainder(ramp->base - ramp->width + 1);
                alignment *= gcd(gcd(mod_rem.modulus, mod_rem.remainder), 32);
            }

            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), base);
            ptr = builder->CreatePointerCast(ptr, llvm_type_of(op->type)->getPointerTo());
            LoadInst *vec = builder->CreateAlignedLoad(ptr, alignment);
            add_tbaa_metadata(vec, op->name);
            Value *undef = UndefValue::get(vec->getType());

            vector<Constant *> indices(ramp->width);
            for (int i = 0; i < ramp->width; i++) {
                indices[i] = ConstantInt::get(i32, ramp->width-1-i);
            }
            value = builder->CreateShuffleVector(vec, undef, ConstantVector::get(indices));
        } else if (ramp) {
            // Gather without generating the indices as a vector
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), ramp->base);
            Value *stride = codegen(ramp->stride);
            value = UndefValue::get(llvm_type_of(op->type));
            for (int i = 0; i < ramp->width; i++) {
                Value *lane = ConstantInt::get(i32, i);
                LoadInst *val = builder->CreateLoad(ptr);
                add_tbaa_metadata(val, op->name);
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
                add_tbaa_metadata(val, op->name);
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
                add_tbaa_metadata(val, op->name);
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
    Constant *undef = UndefValue::get(VectorType::get(v->getType(), 1));
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
        assert(broadcast);
        return broadcast->value;
    } else {
        return e;
    }
}

void CodeGen::visit(const Call *op) {
    assert((op->call_type == Call::Extern || op->call_type == Call::Intrinsic) &&
           "Can only codegen extern calls and intrinsics");

    if (op->call_type == Call::Intrinsic) {
        // Some call nodes are actually injected at various stages as a
        // cue for llvm to generate particular ops. In general these are
        // handled in the standard library, but ones with e.g. varying
        // types are handled here.
        if (op->name == Call::shuffle_vector) {
            assert((int) op->args.size() == 1 + op->type.width);
            vector<Constant *> indices(op->type.width);
            for (size_t i = 0; i < indices.size(); i++) {
                const IntImm *idx = op->args[i+1].as<IntImm>();
                assert(idx);
                indices[i] = ConstantInt::get(i32, idx->value);
            }
            Value *arg = codegen(op->args[0]);
            value = builder->CreateShuffleVector(arg, arg, ConstantVector::get(indices));

        } else if (op->name == Call::interleave_vectors) {
            assert(op->args.size() == 2);
            Expr a = op->args[0], b = op->args[1];
            debug(3) << "Vectors to interleave: " << a << ", " << b << "\n";

            vector<Constant *> indices(op->type.width);
            for (int i = 0; i < op->type.width; i++) {
                int idx = i/2;
                if (i % 2 == 1) idx += a.type().width;
                indices[i] = ConstantInt::get(i32, idx);
            }

            value = builder->CreateShuffleVector(codegen(a), codegen(b), ConstantVector::get(indices));

        } else if (op->name == Call::debug_to_file) {
            assert(op->args.size() == 9);
            const StringImm *filename = op->args[0].as<StringImm>();
            const Load *func = op->args[1].as<Load>();
            assert(func && filename && "Malformed debug_to_file node");
            // Grab the function from the initial module
            llvm::Function *debug_to_file = module->getFunction("halide_debug_to_file");
            assert(debug_to_file && "Could not find halide_debug_to_file function in initial module");

            // Make the filename a global string constant
            Value *char_ptr = codegen(Expr(filename));
            Value *data_ptr = symbol_table.get(func->name + ".host");
            data_ptr = builder->CreatePointerCast(data_ptr, i8->getPointerTo());
            vector<Value *> args = vec(char_ptr, data_ptr);
            for (size_t i = 3; i < 9; i++) {
                debug(4) << op->args[i];
                args.push_back(codegen(op->args[i]));
            }

            debug(4) << "Creating call to debug_to_file\n";

            value = builder->CreateCall(debug_to_file, args);
        } else if (op->name == Call::bitwise_and) {
            assert(op->args.size() == 2);
            value = builder->CreateAnd(codegen(op->args[0]), codegen(op->args[1]));
        } else if (op->name == Call::bitwise_xor) {
            assert(op->args.size() == 2);
            value = builder->CreateXor(codegen(op->args[0]), codegen(op->args[1]));
        } else if (op->name == Call::bitwise_or) {
            assert(op->args.size() == 2);
            value = builder->CreateOr(codegen(op->args[0]), codegen(op->args[1]));
        } else if (op->name == Call::bitwise_not) {
            assert(op->args.size() == 1);
            value = builder->CreateNot(codegen(op->args[0]));
        } else if (op->name == Call::reinterpret) {
            assert(op->args.size() == 1);
            value = builder->CreateBitCast(codegen(op->args[0]), llvm_type_of(op->type));
        } else if (op->name == Call::shift_left) {
            assert(op->args.size() == 2);
            value = builder->CreateShl(codegen(op->args[0]), codegen(op->args[1]));
        } else if (op->name == Call::shift_right) {
            assert(op->args.size() == 2);
            if (op->type.is_int()) {
                value = builder->CreateAShr(codegen(op->args[0]), codegen(op->args[1]));
            } else {
                value = builder->CreateLShr(codegen(op->args[0]), codegen(op->args[1]));
            }
        } else if (op->name == Call::create_buffer_t) {
            // Make some memory for this buffer_t
            const Call *c = op->args[0].as<Call>();

            Value *buffer = builder->CreateAlloca(buffer_t, ConstantInt::get(i32, 1));
            need_stack_restore = true;

            // Populate the fields
            Value *host_ptr;
            if (!c) {
                const IntImm *imm = op->args[0].as<IntImm>();
                assert(imm && imm->value == 0 && "First argument to create_buffer_t must either be a buffer name or the constant zero");
                // Buffers with null host pointers are used for bounds
                // inference queries to external stages.
                host_ptr = ConstantPointerNull::get(i8->getPointerTo());
            } else {
                host_ptr = sym_get(c->name + ".host");
            }

            int elem_size = op->args[1].as<IntImm>()->value;
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
            Value *host_ptr_field = buffer_host_ptr(buffer);
            host_ptr = builder->CreatePointerCast(host_ptr, i8->getPointerTo());
            builder->CreateStore(host_ptr, host_ptr_field);
            builder->CreateStore(ConstantInt::get(i32, elem_size), buffer_elem_size_ptr(buffer));
            value = buffer;
        } else if (op->name == Call::extract_buffer_min) {
            assert(op->args.size() == 2);
            const IntImm *idx = op->args[1].as<IntImm>();
            assert(idx);
            Value *buffer = codegen(op->args[0]);
            buffer = builder->CreatePointerCast(buffer, buffer_t->getPointerTo());
            value = buffer_min(buffer, idx->value);
        } else if (op->name == Call::extract_buffer_extent) {
            assert(op->args.size() == 2);
            const IntImm *idx = op->args[1].as<IntImm>();
            assert(idx);
            Value *buffer = codegen(op->args[0]);
            buffer = builder->CreatePointerCast(buffer, buffer_t->getPointerTo());
            value = buffer_extent(buffer, idx->value);
        } else if (op->name == Call::rewrite_buffer) {
            int dims = ((int)(op->args.size())-2)/3;
            assert((int)(op->args.size()) == dims*3 + 2);
            assert(dims <= 4);

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
        } else if (op->name == Call::trace) {

            int int_args = (int)(op->args.size()) - 4;
            assert(int_args >= 0);

            // Make a global string for the func name. Should be the same for all lanes.
            Value *name = codegen(unbroadcast(op->args[0]));

            // Codegen the event type. Should be the same for all lanes.
            Value *event_type = codegen(unbroadcast(op->args[1]));

            // Codegen the value index. Should be the same for all lanes.
            Value *value_index = codegen(unbroadcast(op->args[2]));

            Value *saved_stack = save_stack();

            // Allocate and populate a stack entry for the value arg
            Type type = op->args[3].type();
            Value *value_stored_array = builder->CreateAlloca(llvm_type_of(type), ConstantInt::get(i32, 1));
            Value *value_stored = codegen(op->args[3]);
            builder->CreateStore(value_stored, value_stored_array);
            value_stored_array = builder->CreatePointerCast(value_stored_array, i8->getPointerTo());

            // Allocate and populate a stack array for the integer args
            Value *coords;
            if (int_args > 0) {
                coords = builder->CreateAlloca(llvm_type_of(op->args[4].type()),
                                                      ConstantInt::get(i32, int_args));
                for (int i = 0; i < int_args; i++) {
                    Value *coord_ptr = builder->CreateConstInBoundsGEP1_32(coords, i);
                    builder->CreateStore(codegen(op->args[4+i]), coord_ptr);
                }
                coords = builder->CreatePointerCast(coords, i32->getPointerTo());
            } else {
                coords = Constant::getNullValue(i32->getPointerTo());
            }

            // Call the runtime function
            vector<Value *> args(9);
            args[0] = name;
            args[1] = event_type;
            args[2] = ConstantInt::get(i32, type.t);
            args[3] = ConstantInt::get(i32, type.bits);
            args[4] = ConstantInt::get(i32, type.width);
            args[5] = value_index;
            args[6] = value_stored_array;
            args[7] = ConstantInt::get(i32, int_args * type.width);
            args[8] = coords;

            llvm::Function *trace_fn = module->getFunction("halide_trace");
            assert(trace_fn);

            builder->CreateCall(trace_fn, args);

            restore_stack(saved_stack);

            // Evaluates to the value arg
            value = value_stored;

        } else if (op->name == Call::profiling_timer) {
            assert(op->args.size() == 0);
            llvm::Function *fn = Intrinsic::getDeclaration(module,
                Intrinsic::readcyclecounter, std::vector<llvm::Type*>());
            CallInst *call = builder->CreateCall(fn);
            value = call;
        } else if (op->name == Call::lerp) {
            assert(op->args.size() == 3);

            value = codegen(lower_lerp(op->args[0], op->args[1], op->args[2]));
        } else {
            std::cerr << "Unknown intrinsic: " << op->name << "\n";
            assert(false);
        }


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
            // need to pointer cast to the appropriate type.
            FunctionType *func_t = fn->getFunctionType();
            for (size_t i = 0; i < args.size(); i++) {
                if (op->args[i].type().is_handle()) {
                    llvm::Type *t = func_t->getParamType(i);
                    if (t != args[i]->getType()) {
                        debug(4) << "Pointer casting argument to extern call: "
                                 << op->args[i] << "\n";
                        args[i] = builder->CreatePointerCast(args[i], t);
                    }
                }
            }
        }

        bool has_side_effects = false;
        // TODO: Need a general solution here
        if (op->name == "halide_current_time_ns") {
            has_side_effects = true;
        }

        if (op->type.is_scalar()) {
            debug(4) << "Creating scalar call to " << op->name << "\n";
            CallInst *call = builder->CreateCall(fn, args);
            if (!has_side_effects) {
                call->setDoesNotAccessMemory();
                call->setDoesNotThrow();
            }
            value = call;
        } else {
            // Check if a vector version of the function already
            // exists. We use the naming convention that a N-wide
            // version of a function foo is called fooxN.
            ostringstream ss;
            ss << op->name << 'x' << op->type.width;
            llvm::Function *vec_fn = module->getFunction(ss.str());
            if (vec_fn) {
                debug(4) << "Creating vector call to " << ss.str() << "\n";
                CallInst *call = builder->CreateCall(vec_fn, args);
                if (!has_side_effects) {
                    call->setDoesNotAccessMemory();
                    call->setDoesNotThrow();
                }
                value = call;
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
                    CallInst *call = builder->CreateCall(fn, arg_lane);
                    if (!has_side_effects) {
                        call->setDoesNotAccessMemory();
                        call->setDoesNotThrow();
                    }
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

Value *CodeGen::create_string_constant(const string &s) {
    llvm::Type *str_type = ArrayType::get(i8, s.size()+1);
    GlobalVariable *str_global = new GlobalVariable(*module, str_type,
                                                    true, GlobalValue::PrivateLinkage, 0);
    str_global->setInitializer(ConstantDataArray::getString(*context, s));
    return builder->CreateConstInBoundsGEP2_32(str_global, 0, 0);
}

void CodeGen::create_assertion(Value *cond, const string &message) {

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
    BasicBlock *assert_fails_bb = BasicBlock::Create(*context, "assert_failed", function);
    BasicBlock *assert_succeeds_bb = BasicBlock::Create(*context, "after_assert", function);

    // If the condition fails, enter the assert body, otherwise, enter the block after
    builder->CreateCondBr(cond, assert_succeeds_bb, assert_fails_bb);

    // Build the failure case
    builder->SetInsertPoint(assert_fails_bb);

    // Make the error message string a global constant
    Value *char_ptr = create_string_constant(message);

    // Call the error handler
    llvm::Function *error_handler = module->getFunction("halide_error");
    assert(error_handler && "Could not find halide_error in initial module");
    debug(4) << "Creating call to error handlers\n";
    builder->CreateCall(error_handler, vec(char_ptr));

    // Do any architecture-specific cleanup necessary
    debug(4) << "Creating cleanup code\n";
    prepare_for_early_exit();

    // Bail out with error code -1
    builder->CreateRet(ConstantInt::get(i32, -1));

    // Continue on using the success case
    builder->SetInsertPoint(assert_succeeds_bb);
}

void CodeGen::visit(const Pipeline *op) {
    codegen(op->produce);
    if (op->update.defined()) codegen(op->update);
    codegen(op->consume);
}

void CodeGen::visit(const For *op) {
    Value *min = codegen(op->min);
    Value *extent = codegen(op->extent);

    if (op->for_type == For::Serial) {
        Value *max = builder->CreateNSWAdd(min, extent);

        BasicBlock *preheader_bb = builder->GetInsertBlock();

        // Make a new basic block for the loop
        BasicBlock *loop_bb = BasicBlock::Create(*context, op->name + "_loop", function);
        // Create the block that comes after the loop
        BasicBlock *after_bb = BasicBlock::Create(*context, op->name + "_after_loop", function);

        // If min < max, fall through to the loop bb
        Value *enter_condition = builder->CreateICmpSLT(min, max);
        builder->CreateCondBr(enter_condition, loop_bb, after_bb);
        builder->SetInsertPoint(loop_bb);

        // Make our phi node.
        PHINode *phi = builder->CreatePHI(i32, 2);
        phi->addIncoming(min, preheader_bb);

        // Within the loop, the variable is equal to the phi value
        sym_push(op->name, phi);

        // Set up state to detect if we need to do a stack restore on exit from this block.
        bool old_need_stack_restore = need_stack_restore;
        need_stack_restore = false;
        Value *saved_stack = save_stack();

        // Emit the loop body
        codegen(op->body);

        // Do any necessary stack restore/save
        if (need_stack_restore) {
            restore_stack(saved_stack);
        } else {
            // Remove the save
            Instruction *save_inst = dyn_cast<Instruction>(saved_stack);
            assert(save_inst);
            save_inst->eraseFromParent();
        }

        need_stack_restore = old_need_stack_restore;

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
        Closure closure = Closure::make(op->body, op->name, track_buffers(), buffer_t);

	Value *saved_stack = save_stack();

        // Allocate a closure
        StructType *closure_t = closure.build_type(context);
        Value *ptr = builder->CreateAlloca(closure_t, ConstantInt::get(i32, 1));

        // Fill in the closure
        closure.pack_struct(ptr, symbol_table, builder);

        // Make a new function that does one iteration of the body of the loop
        FunctionType *func_t = FunctionType::get(i32, vec(i32, (llvm::Type *)(i8->getPointerTo())), false);
        llvm::Function *containing_function = function;
        function = llvm::Function::Create(func_t, llvm::Function::InternalLinkage, "par_for_" + op->name, module);
        function->setDoesNotAlias(2);

        // Make the initial basic block and jump the builder into the new function
        BasicBlock *call_site = builder->GetInsertBlock();
        BasicBlock *block = BasicBlock::Create(*context, "entry", function);
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

        // Return success
        builder->CreateRet(ConstantInt::get(i32, 0));

        // Move the builder back to the main function and call do_par_for
        builder->SetInsertPoint(call_site);
        llvm::Function *do_par_for = module->getFunction("halide_do_par_for");
        assert(do_par_for && "Could not find halide_do_par_for in initial module");
        do_par_for->setDoesNotAlias(4);
        //do_par_for->setDoesNotCapture(4);
        ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());
        vector<Value *> args = vec<Value *>(function, min, extent, ptr);
        debug(4) << "Creating call to do_par_for\n";
        Value *result = builder->CreateCall(do_par_for, args);

        debug(3) << "Leaving parallel for loop over " << op->name << "\n";

	restore_stack(saved_stack);

        // Now restore the scope
        std::swap(symbol_table, saved_symbol_table);
        function = containing_function;

        // Check for success
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32, 0));
        create_assertion(did_succeed, "Failure inside parallel for loop");

    } else {
        assert(false && "Unknown type of For node. Only Serial and Parallel For nodes should survive down to codegen");
    }
}

void CodeGen::visit(const Store *op) {
    Value *val = codegen(op->value);
    Halide::Type value_type = op->value.type();
    // Scalar
    if (value_type.is_scalar()) {
        Value *ptr = codegen_buffer_pointer(op->name, value_type, op->index);
        StoreInst *store = builder->CreateAlignedStore(val, ptr, op->value.type().bytes());
        add_tbaa_metadata(store, op->name);
    } else {
        int alignment = op->value.type().bytes();
        const Ramp *ramp = op->index.as<Ramp>();
        if (ramp && is_one(ramp->stride)) {

            // Boost the alignment if possible
            ModulusRemainder mod_rem = modulus_remainder(ramp->base, alignment_info);
            while ((mod_rem.remainder & 1) == 0 &&
                   (mod_rem.modulus & 1) == 0 &&
                   alignment < 256) {
                mod_rem.modulus /= 2;
                mod_rem.remainder /= 2;
                alignment *= 2;
            }

            Value *ptr = codegen_buffer_pointer(op->name, value_type.element_of(), ramp->base);
            Value *ptr2 = builder->CreatePointerCast(ptr, llvm_type_of(value_type)->getPointerTo());
            StoreInst *store = builder->CreateAlignedStore(val, ptr2, alignment);
            add_tbaa_metadata(store, op->name);
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
                    StoreInst *store = builder->CreateAlignedStore(v, p, op->value.type().bytes());
                    add_tbaa_metadata(store, op->name);
                } else {
                    // Increment the pointer by the stride for each element
                    StoreInst *store = builder->CreateAlignedStore(v, ptr, op->value.type().bytes());
                    add_tbaa_metadata(store, op->name);
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
                add_tbaa_metadata(store, op->name);
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

Value *CodeGen::save_stack() {
    llvm::Function *stacksave =
        llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::stacksave);
    debug(4) << "Saving stack\n";
    return builder->CreateCall(stacksave);
}

void CodeGen::restore_stack(llvm::Value *saved_stack) {
    llvm::Function *stackrestore =
        llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::stackrestore);
    debug(4) << "Restoring stack\n";
    builder->CreateCall(stackrestore, saved_stack);
}

template<>
EXPORT RefCount &ref_count<CodeGen>(const CodeGen *p) {return p->ref_count;}

template<>
EXPORT void destroy<CodeGen>(const CodeGen *p) {delete p;}

}}
