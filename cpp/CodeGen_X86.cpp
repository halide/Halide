#include "CodeGen_X86.h"
#include "IROperator.h"
#include <iostream>

namespace HalideInternal {

    using namespace llvm;

    CodeGen_X86::CodeGen_X86() : CodeGen() {
        i32x4 = VectorType::get(i32, 4);
        i32x8 = VectorType::get(i32, 8);        
    }

    void CodeGen_X86::compile(Stmt stmt, string name, const vector<Argument> &args) {
        CodeGen::compile(stmt, name, args, "x86_64-unknown-linux-gnu");
    }


    void CodeGen_X86::visit(const Allocate *alloc) {
        std::cout << "In allocate" << std::endl;

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
            assert(false && "Calling malloc not yet implemented");
        }

        // In the future, we may want to construct an entire buffer_t here
        string allocation_name = alloc->buffer + ".host";

        symbol_table.push(allocation_name, ptr);        
        codegen(alloc->body);
        symbol_table.pop(allocation_name);

        if (!on_stack) {
            // call free
            assert(false && "Calling free not yet implemented");
        }
    }
    
    void CodeGen_X86::test() {
        // corner cases to test:
        // signed mod by power of two, non-power of two
        // loads of mismatched types (e.g. load a float from something allocated as an array of ints)

        // A simple function with no arguments
        Argument buffer_arg = {"buf", true, Int(0)};
        Argument scalar_arg = {"alpha", false, Float(32)};
        vector<Argument> args(2);
        args[0] = buffer_arg;
        args[1] = scalar_arg;
        Expr x = new Var(Int(32), "x");
        Expr alpha = new Var(Float(32), "alpha");
        Expr e = new Select(alpha > 4.0f, 3, 2);
        Stmt s = new Store("buf", e, x);
        s = new LetStmt("x", 4, s);
        s = new Allocate("tmp", Int(32), 127, s);
        CodeGen_X86 cg;
        cg.compile(s, "test1", args);
        cg.module->dump();
    }

}
