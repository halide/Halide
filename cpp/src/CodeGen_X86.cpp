#include "CodeGen_X86.h"
#include "IROperator.h"
#include <iostream>
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/IRReader.h"
#include "buffer_t.h"
#include "IRPrinter.h"
#include "IRMatch.h"
#include "Log.h"
#include "Util.h"
#include "Var.h"
#include "Param.h"

namespace Halide { 
namespace Internal {

extern unsigned char builtins_bitcode_x86[];
extern int builtins_bitcode_x86_length;

using namespace llvm;

CodeGen_X86::CodeGen_X86() : CodeGen() {
    i32x4 = VectorType::get(i32, 4);
    i32x8 = VectorType::get(i32, 8);
}

void CodeGen_X86::compile(Stmt stmt, string name, const vector<Argument> &args) {
    assert(builtins_bitcode_x86_length && "initial module for x86 is empty");

    // Wrap the initial module in a memory buffer
    StringRef sb = StringRef((char *)builtins_bitcode_x86, builtins_bitcode_x86_length);
    MemoryBuffer *bitcode_buffer = MemoryBuffer::getMemBuffer(sb);

    // Parse it
    module = ParseBitcodeFile(bitcode_buffer, context);

    // Fix the target triple

    // For now we'll just leave it as whatever the module was
    // compiled as. This assumes that we're not cross-compiling
    // between different x86 operating systems
    // module->setTargetTriple( ... );
        
    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args);
    delete bitcode_buffer;
}

Expr _i64(Expr e) {
    return cast(Int(64, e.type().width), e);
}

Expr _u64(Expr e) {
    return cast(UInt(64, e.type().width), e);
}

Expr _i32(Expr e) {
    return cast(Int(32, e.type().width), e);
}

Expr _u32(Expr e) {
    return cast(UInt(32, e.type().width), e);
}

Expr _i16(Expr e) {
    return cast(Int(16, e.type().width), e);
}

Expr _u16(Expr e) {
    return cast(UInt(16, e.type().width), e);
}
 
Expr _i8(Expr e) {
    return cast(Int(8, e.type().width), e);
}

Expr _u8(Expr e) {
    return cast(UInt(8, e.type().width), e);
}

Expr _f32(Expr e) {
    return cast(Float(32, e.type().width), e);
}

Expr _f64(Expr e) {
    return cast(Float(64, e.type().width), e);
}

Value *CodeGen_X86::call_intrin(Type result_type, const string &name, Expr arg1, Expr arg2) {
    vector<llvm::Type *> arg_types = vec(llvm_type_of(arg1.type()));
    vector<Value *> arg_values = vec(codegen(arg1));

    if (arg2.defined()) {
        arg_types.push_back(llvm_type_of(arg2.type()));
        arg_values.push_back(codegen(arg2));
    }

    FunctionType *func_t = FunctionType::get(llvm_type_of(result_type), arg_types, false);
    
    llvm::Function *fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, "llvm.x86." + name, module);
    fn->setCallingConv(CallingConv::C);            
    return builder.CreateCall(fn, arg_values);
}

void CodeGen_X86::visit(const Cast *op) {

    Expr i8_1 = new Variable(Int(8, 16), "i8_1");
    Expr i8_2 = new Variable(Int(8, 16), "i8_2");
    Expr i16_1 = new Variable(Int(16, 8), "i16_1");
    Expr i16_2 = new Variable(Int(16, 8), "i16_2");
    Expr i32_1 = new Variable(Int(32, 4), "i32_1");
    Expr i32_2 = new Variable(Int(32, 4), "i32_2");
    Expr u8_1 = new Variable(UInt(8, 16), "u8_1");
    Expr u8_2 = new Variable(UInt(8, 16), "u8_2");
    Expr u16_1 = new Variable(UInt(16, 8), "u16_1");
    Expr u16_2 = new Variable(UInt(16, 8), "u16_2");
    Expr u32_1 = new Variable(UInt(32, 4), "u32_1");
    Expr u32_2 = new Variable(UInt(32, 4), "u32_2");

    Expr min_i8 = new Broadcast(cast(Int(16), -128), 16);
    Expr max_i8 = new Broadcast(cast(Int(16), 127), 16);
    Expr min_i16 = new Broadcast(cast(Int(32), -32768), 8);
    Expr max_i16 = new Broadcast(cast(Int(32), 32767), 8);
    Expr min_u8 = new Broadcast(cast(Int(16), 0), 16);
    Expr max_u8 = new Broadcast(cast(UInt(16), 255), 16);
    Expr min_u16 = new Broadcast(cast(Int(32), 0), 8);
    Expr max_u16 = new Broadcast(cast(UInt(32), 65535), 8);


    map<string, Expr> env;

    if (expr_match(_i8(clamp(_i16(i8_1) + _i16(i8_2), min_i8, max_i8)), op, env)) {
        value = call_intrin(Int(8, 16), "sse2.padds.b", env["i8_1"], env["i8_2"]);
    } else if (expr_match(_i8(clamp(_i16(i8_1) - _i16(i8_2), min_i8, max_i8)), op, env)) {
        value = call_intrin(Int(8, 16), "sse2.psubs.b", env["i8_1"], env["i8_2"]);
    } else if (expr_match(_u8(min(_u16(u8_1) + _u16(u8_2), max_u8)), op, env)) {
        value = call_intrin(UInt(8, 16), "sse2.paddus.b", env["u8_1"], env["u8_2"]);
    } else if (expr_match(_u8(max(_i16(u8_1) - _i16(u8_2), min_u8)), op, env)) {
        value = call_intrin(UInt(8, 16), "sse2.psubus.b", env["u8_1"], env["u8_2"]);
    } else if (expr_match(_i16(clamp(_i32(i16_1) + _i32(i16_2), min_i16, max_i16)), op, env)) {
        value = call_intrin(Int(16, 8), "sse2.padds.w", env["i16_1"], env["i16_2"]);
    } else if (expr_match(_i16(clamp(_i32(i16_1) - _i32(i16_2), min_i16, max_i16)), op, env)) {
        value = call_intrin(Int(16, 8), "sse2.psubs.w", env["i16_1"], env["i16_2"]);
    } else if (expr_match(_u16(min(_u32(u16_1) + _u32(u16_2), max_u16)), op, env)) {
        value = call_intrin(UInt(16, 8), "sse2.paddus.w", env["u16_1"], env["u16_2"]);
    } else if (expr_match(_u16(max(_i32(u16_1) - _i32(u16_2), min_u16)), op, env)) {
        value = call_intrin(UInt(16, 8), "sse2.psubus.w", env["u16_1"], env["u16_2"]);
    } else {
        CodeGen::visit(op);
    }

    /*
    check_sse("paddsb", 16, i8(clamp(i16(i8_1) + i16(i8_2), min_i8, max_i8)));
    check_sse("psubsb", 16, i8(clamp(i16(i8_1) - i16(i8_2), min_i8, max_i8)));
    check_sse("paddusb", 16, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
    check_sse("psubusb", 16, u8(min(u16(u8_1) - u16(u8_2), max_u8)));
    check_sse("paddsw", 8, i16(clamp(i32(i16_1) + i32(i16_2), min_i16, max_i16)));
    check_sse("psubsw", 8, i16(clamp(i32(i16_1) - i32(i16_2), min_i16, max_i16)));
    check_sse("paddusw", 8, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
    check_sse("psubusw", 8, u16(min(u32(u16_1) - u32(u16_2), max_u16)));
    check_sse("pmulhw", 8, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
    check_sse("pmulhw", 8, i16_1 / 15);

    // SSE 1
    check_sse("rcpps", 4, 1.0f / f32_2);
    check_sse("rsqrtps", 4, 1.0f / sqrt(f32_2));
    check_sse("pavgb", 16, u8((u16(u8_1) + u16(u8_2) + 1)/2));
    check_sse("pavgw", 8, u16((u32(u16_1) + u32(u16_2) + 1)/2));
    check_sse("pmaxsw", 8, max(i16_1, i16_2));
    check_sse("pminsw", 8, min(i16_1, i16_2));
    check_sse("pmaxub", 16, max(u8_1, u8_2));
    check_sse("pminub", 16, min(u8_1, u8_2));
    check_sse("pmulhuw", 8, u16((u32(u16_1) * u32(u16_2))/(256*256)));
    check_sse("pmulhuw", 8, u16_1 / 15);

    check_sse("shufps", 4, in_f32(2*x));

    // SSE 2
    check_sse("packssdw", 8, i16(clamp(i32_1, min_i16, max_i16)));
    check_sse("packsswb", 16, i8(clamp(i16_1, min_i8, max_i8)));
    check_sse("packuswb", 16, u8(clamp(i16_1, 0, max_u8)));

    // SSE 4.1
    check_sse("pmaxsb", 16, max(i8_1, i8_2));
    check_sse("pminsb", 16, min(i8_1, i8_2));
    check_sse("pmaxuw", 8, max(u16_1, u16_2));
    check_sse("pminuw", 8, min(u16_1, u16_2));
    check_sse("pmaxud", 4, max(u32_1, u32_2));
    check_sse("pminud", 4, min(u32_1, u32_2));
    check_sse("pmaxsd", 4, max(i32_1, i32_2));
    check_sse("pminsd", 4, min(i32_1, i32_2));
    check_sse("packusdw", 8, u16(clamp(i32_1, 0, max_u16)));
    */
}

void CodeGen_X86::visit(const Allocate *alloc) {

    // Allocate anything less than 32k on the stack
    int bytes_per_element = alloc->type.bits / 8;
    int stack_size = 0;
    bool on_stack = false;
    if (const IntImm *size = alloc->size.as<IntImm>()) {            
        stack_size = size->value;
        on_stack = stack_size < 32*1024;
    }

    Value *size = codegen(alloc->size * bytes_per_element);
    llvm::Type *llvm_type = llvm_type_of(alloc->type);
    Value *ptr;                

    if (on_stack) {
        // Do a 32-byte aligned alloca
        int total_bytes = stack_size * bytes_per_element;            
        int chunks = (total_bytes + 31)/32;
        ptr = builder.CreateAlloca(i32x8, ConstantInt::get(i32, chunks)); 
        ptr = builder.CreatePointerCast(ptr, llvm_type->getPointerTo());
    } else {
        // call malloc
        llvm::Function *malloc_fn = module->getFunction("fast_malloc");
        Value *sz = builder.CreateIntCast(size, i64, false);
        ptr = builder.CreateCall(malloc_fn, sz);
    }

    // In the future, we may want to construct an entire buffer_t here
    string allocation_name = alloc->buffer + ".host";
    log(3) << "Pushing allocation called " << allocation_name << " onto the symbol table\n";

    symbol_table.push(allocation_name, ptr);
    codegen(alloc->body);
    symbol_table.pop(allocation_name);

    if (!on_stack) {
        // call free
        llvm::Function *free_fn = module->getFunction("fast_free");
        builder.CreateCall(free_fn, ptr);
    }
}

static bool extern_function_1_was_called = false;
extern "C" int extern_function_1(float x) {
    extern_function_1_was_called = true;
    return x < 0.4 ? 3 : 1;
}

void CodeGen_X86::test() {
    // corner cases to test:
    // signed mod by power of two, non-power of two
    // loads of mismatched types (e.g. load a float from something allocated as an array of ints)
    // Calls to vectorized externs, and externs for which no vectorized version exists

    Argument buffer_arg("buf", true, Int(0));
    Argument float_arg("alpha", false, Float(32));
    Argument int_arg("beta", false, Int(32));
    vector<Argument> args(3);
    args[0] = buffer_arg;
    args[1] = float_arg;
    args[2] = int_arg;        
    Var x("x"), i("i");
    Param<float> alpha("alpha");
    Param<int> beta("beta");

    // We'll clear out the initial buffer except for the first and
    // last two elements using dense unaligned vectors
    Stmt init = new For("i", 0, 3, For::Serial, 
                        new Store("buf", 
                                  new Ramp(i*4+2, 1, 4),
                                  new Ramp(i*4+2, 1, 4)));

    // Now set the first two elements using scalars, and last four elements using a dense aligned vector
    init = new Block(init, new Store("buf", 0, 0));
    init = new Block(init, new Store("buf", 1, 1));
    init = new Block(init, new Store("buf", new Ramp(12, 1, 4), new Ramp(12, 1, 4)));

    // Then multiply the even terms by 17 using sparse vectors
    init = new Block(init, 
                     new For("i", 0, 2, For::Serial, 
                             new Store("buf", 
                                       new Mul(new Broadcast(17, 4), 
                                               new Load(Int(32, 4), "buf", new Ramp(i*8, 2, 4), Buffer(), Parameter())),
                                       new Ramp(i*8, 2, 4))));

    // Then print some stuff (disabled to prevent debugging spew)
    // vector<Expr> print_args = vec<Expr>(3, 4.5f, new Cast(Int(8), 2), new Ramp(alpha, 3.2f, 4));
    // init = new Block(init, new PrintStmt("Test print: ", print_args));

    // Then run a parallel for loop that clobbers three elements of buf
    Expr e = new Select(alpha > 4.0f, 3, 2);
    e += (new Call(Int(32), "extern_function_1", vec<Expr>(alpha)));
    Stmt loop = new Store("buf", e, x + i);
    loop = new LetStmt("x", beta+1, loop);
    // Do some local allocations within the loop
    loop = new Allocate("tmp_stack", Int(32), 127, loop);
    loop = new Allocate("tmp_heap", Int(32), 43 * beta, loop);
    loop = new For("i", -1, 3, For::Parallel, loop);        

    Stmt s = new Block(init, loop);

    CodeGen_X86 cg;
    cg.compile(s, "test1", args);

    //cg.compile_to_bitcode("test1.bc");
    //cg.compile_to_native("test1.o", false);
    //cg.compile_to_native("test1.s", true);

    if (!getenv("HL_NUMTHREADS")) {
        setenv("HL_NUMTHREADS", "4", 1);
    }
    JITCompiledModule m = cg.compile_to_function_pointers();
    typedef void (*fn_type)(::buffer_t *, float, int);
    fn_type fn = (fn_type)m.function;

    int scratch[16];
    ::buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    buf.host = (uint8_t *)(&scratch[0]);

    fn(&buf, -32, 0);
    assert(scratch[0] == 5);
    assert(scratch[1] == 5);
    assert(scratch[2] == 5);
    assert(scratch[3] == 3);
    assert(scratch[4] == 4*17);
    assert(scratch[5] == 5);
    assert(scratch[6] == 6*17);

    fn(&buf, 37.32f, 2);
    assert(scratch[0] == 0);
    assert(scratch[1] == 1);
    assert(scratch[2] == 4);
    assert(scratch[3] == 4);
    assert(scratch[4] == 4);
    assert(scratch[5] == 5);
    assert(scratch[6] == 6*17);

    fn(&buf, 4.0f, 1);
    assert(scratch[0] == 0);
    assert(scratch[1] == 3);
    assert(scratch[2] == 3);
    assert(scratch[3] == 3);
    assert(scratch[4] == 4*17);
    assert(scratch[5] == 5);
    assert(scratch[6] == 6*17);
    assert(extern_function_1_was_called);

    // Check the wrapped version does the same thing
    extern_function_1_was_called = false;
    for (int i = 0; i < 16; i++) scratch[i] = 0;

    float float_arg_val = 4.0f;
    int int_arg_val = 1;
    const void *arg_array[] = {&buf, &float_arg_val, &int_arg_val};
    m.wrapped_function(arg_array);
    assert(scratch[0] == 0);
    assert(scratch[1] == 3);
    assert(scratch[2] == 3);
    assert(scratch[3] == 3);
    assert(scratch[4] == 4*17);
    assert(scratch[5] == 5);
    assert(scratch[6] == 6*17);
    assert(extern_function_1_was_called);

    std::cout << "CodeGen_X86 test passed" << std::endl;
}

}}
