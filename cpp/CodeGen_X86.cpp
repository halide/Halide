#include "CodeGen_X86.h"
#include "IROperator.h"
#include <iostream>
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/IRReader.h"
#include "buffer.h"
#include "IRPrinter.h"
#include "Util.h"

extern unsigned char builtins_bitcode_x86[];
extern int builtins_bitcode_x86_length;

namespace Halide { namespace Internal {

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


    void CodeGen_X86::visit(const Allocate *alloc) {

        // Allocate anything less than 32k on the stack
        int stack_size = 0;
        bool on_stack = false;
        if (const IntImm *size = alloc->size.as<IntImm>()) {            
            stack_size = size->value;
            on_stack = stack_size < 32*1024;
        }

        Value *size = codegen(alloc->size);
        llvm::Type *llvm_type = llvm_type_of(alloc->type);
        Value *ptr;                

        if (on_stack) {
            // Do a 32-byte aligned alloca
            int bytes_per_element = alloc->type.bits / 8;
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

        Argument buffer_arg = {"buf", true, Int(0)};
        Argument float_arg = {"alpha", false, Float(32)};
        Argument int_arg = {"beta", false, Int(32)};
        vector<Argument> args(3);
        args[0] = buffer_arg;
        args[1] = float_arg;
        args[2] = int_arg;        
        Expr x = new Variable(Int(32), "x");
        Expr i = new Variable(Int(32), "i");
        Expr alpha = new Variable(Float(32), "alpha");
        Expr beta = new Variable(Int(32), "beta");

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
                                                   new Load(Int(32, 4), "buf", new Ramp(i*8, 2, 4))),
                                           new Ramp(i*8, 2, 4))));

        // Then print some stuff (disabled to prevent debugging spew)
        // vector<Expr> print_args = vec<Expr>(3, 4.5f, new Cast(Int(8), 2), new Ramp(alpha, 3.2f, 4));
        // init = new Block(init, new PrintStmt("Test print: ", print_args));

        // Then run a parallel for loop that clobbers three elements of buf
        Expr e = new Select(alpha > 4.0f, 3, 2);
        e += (new Call(Int(32), "extern_function_1", vec(alpha), Call::Extern, NULL));
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
        void *ptr = cg.compile_to_function_pointer();
        typedef void (*fn_type)(::buffer_t *, float, int);
        fn_type fn = (fn_type)ptr;

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

        std::cout << "CodeGen_X86 test passed" << std::endl;
    }

}}
