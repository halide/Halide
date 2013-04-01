#include "CodeGen_X86.h"
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

#include <llvm/Config/config.h>

// Temporary affordance to compile with both llvm 3.2 and 3.3.
// Protected as at least one installation of llvm elides version macros.
#if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/TargetTransformInfo.h>
#include <llvm/DataLayout.h>
#include <llvm/IRBuilder.h>
#include <llvm/Attributes.h>
#include <llvm/Support/IRReader.h>
// They renamed this type in 3.3
typedef llvm::Attributes Attribute;
#else
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Attributes.h>
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#include <llvm/Support/MemoryBuffer.h>

// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(pop)
#endif

namespace Halide { 
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_Posix::CodeGen_Posix() : CodeGen() {
    i8x8 = VectorType::get(i8, 8);
    i8x16 = VectorType::get(i8, 16);
    i8x32 = VectorType::get(i8, 32);
    i16x4 = VectorType::get(i16, 4);
    i16x8 = VectorType::get(i16, 8);
    i16x16 = VectorType::get(i16, 16);
    i32x2 = VectorType::get(i32, 2);
    i32x4 = VectorType::get(i32, 4);
    i32x8 = VectorType::get(i32, 8);
    i64x2 = VectorType::get(i64, 2);
    i64x4 = VectorType::get(i64, 4);    
    f32x2 = VectorType::get(f32, 2);
    f32x4 = VectorType::get(f32, 4);
    f32x8 = VectorType::get(f32, 8);
    f64x2 = VectorType::get(f64, 2);
    f64x4 = VectorType::get(f64, 4);    

    wild_i8x8 = new Variable(Int(8, 8), "*");
    wild_i8x16 = new Variable(Int(8, 16), "*");
    wild_i8x32 = new Variable(Int(8, 32), "*");
    wild_u8x8 = new Variable(UInt(8, 8), "*");
    wild_u8x16 = new Variable(UInt(8, 16), "*");
    wild_u8x32 = new Variable(UInt(8, 32), "*");
    wild_i16x4 = new Variable(Int(16, 4), "*");
    wild_i16x8 = new Variable(Int(16, 8), "*");
    wild_i16x16 = new Variable(Int(16, 16), "*");
    wild_u16x4 = new Variable(UInt(16, 4), "*");
    wild_u16x8 = new Variable(UInt(16, 8), "*");
    wild_u16x16 = new Variable(UInt(16, 16), "*");
    wild_i32x2 = new Variable(Int(32, 2), "*");
    wild_i32x4 = new Variable(Int(32, 4), "*");
    wild_i32x8 = new Variable(Int(32, 8), "*");
    wild_u32x2 = new Variable(UInt(32, 2), "*");
    wild_u32x4 = new Variable(UInt(32, 4), "*");
    wild_u32x8 = new Variable(UInt(32, 8), "*");

    wild_u64x2 = new Variable(UInt(64, 2), "*");
    wild_i64x2 = new Variable(Int(64, 2), "*");

    wild_f32x2 = new Variable(Float(32, 2), "*");
    wild_f32x4 = new Variable(Float(32, 4), "*");
    wild_f32x8 = new Variable(Float(32, 8), "*");
    wild_f64x2 = new Variable(Float(64, 2), "*");
    wild_f64x4 = new Variable(Float(64, 4), "*");
}

void CodeGen_Posix::visit(const Allocate *alloc) {

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

    // In the future, we may want to construct an entire buffer_t here
    string allocation_name = alloc->name + ".host";
    log(3) << "Pushing allocation called " << allocation_name << " onto the symbol table\n";

    if (on_stack) {
        // Do a 32-byte aligned alloca
        int total_bytes = stack_size * bytes_per_element;            
        int chunks = (total_bytes + 31)/32;
        ptr = builder->CreateAlloca(i32x8, ConstantInt::get(i32, chunks)); 
        ptr = builder->CreatePointerCast(ptr, llvm_type->getPointerTo());
    } else {
        // call malloc
        llvm::Function *malloc_fn = module->getFunction("halide_malloc");
        // Make the return value as noalias
        malloc_fn->setDoesNotAlias(0);
        assert(malloc_fn && "Could not find halide_malloc in module");
        Value *sz = builder->CreateIntCast(size, malloc_fn->arg_begin()->getType(), false);
	log(4) << "Creating call to halide_malloc\n";
        CallInst *call = builder->CreateCall(malloc_fn, sz);
#if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
        call->addAttribute(0, Attribute::get(context, Attribute::NoAlias));
#else
        call->addAttribute(0, Attribute::NoAlias);
#endif
        ptr = call;
        heap_allocations.push_back(ptr);
    }

    sym_push(allocation_name, ptr);
    codegen(alloc->body);
    sym_pop(allocation_name);

    if (!on_stack) {
        heap_allocations.pop_back();
        // call free
        llvm::Function *free_fn = module->getFunction("halide_free");
        assert(free_fn && "Could not find halide_free in module");
	log(4) << "Creating call to halide_free\n";
        CallInst *call = builder->CreateCall(free_fn, ptr);
#if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
        call->addAttribute(1, Attribute::get(context, Attribute::NoCapture));
#else
        call->addAttribute(1, Attribute::NoCapture);
#endif
    }
}

void CodeGen_Posix::prepare_for_early_exit() {
    llvm::Function *free_fn = module->getFunction("halide_free");
    assert(free_fn && "Could not find halide_free in module");
    for (size_t i = 0; i < heap_allocations.size(); i++) {
        // TODO: What if I'm inside a parallel for loop?
        builder->CreateCall(free_fn, heap_allocations[i]);        
    }
}

}}
