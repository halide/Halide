#include "LLVM_Headers.h"
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



namespace Halide { 
namespace Internal {

using std::vector;
using std::string;
using std::map;
using std::pair;
using std::stack;

using namespace llvm;

CodeGen_Posix::CodeGen_Posix() : 
    CodeGen(),

    // Vector types. These need an LLVMContext before they can be initialized.
    i8x8(NULL), 
    i8x16(NULL),
    i8x32(NULL),
    i16x4(NULL), 
    i16x8(NULL),
    i16x16(NULL),
    i32x2(NULL),
    i32x4(NULL),
    i32x8(NULL),
    i64x2(NULL),
    i64x4(NULL),
    f32x2(NULL),
    f32x4(NULL),
    f32x8(NULL),
    f64x2(NULL),
    f64x4(NULL),

    // Wildcards for pattern matching
    wild_i8x8(Variable::make(Int(8, 8), "*")),
    wild_i16x4(Variable::make(Int(16, 4), "*")),
    wild_i32x2(Variable::make(Int(32, 2), "*")),

    wild_u8x8(Variable::make(UInt(8, 8), "*")),
    wild_u16x4(Variable::make(UInt(16, 4), "*")),
    wild_u32x2(Variable::make(UInt(32, 2), "*")),

    wild_i8x16(Variable::make(Int(8, 16), "*")),
    wild_i16x8(Variable::make(Int(16, 8), "*")),
    wild_i32x4(Variable::make(Int(32, 4), "*")),
    wild_i64x2(Variable::make(Int(64, 2), "*")),

    wild_u8x16(Variable::make(UInt(8, 16), "*")),
    wild_u16x8(Variable::make(UInt(16, 8), "*")),
    wild_u32x4(Variable::make(UInt(32, 4), "*")),
    wild_u64x2(Variable::make(UInt(64, 2), "*")),

    wild_i8x32(Variable::make(Int(8, 32), "*")),
    wild_i16x16(Variable::make(Int(16, 16), "*")),
    wild_i32x8(Variable::make(Int(32, 8), "*")),
    wild_i64x4(Variable::make(Int(64, 4), "*")),

    wild_u8x32(Variable::make(UInt(8, 32), "*")),
    wild_u16x16(Variable::make(UInt(16, 16), "*")),
    wild_u32x8(Variable::make(UInt(32, 8), "*")),
    wild_u64x4(Variable::make(UInt(64, 4), "*")),

    wild_f32x2(Variable::make(Float(32, 2), "*")),

    wild_f32x4(Variable::make(Float(32, 4), "*")),
    wild_f64x2(Variable::make(Float(64, 2), "*")),

    wild_f32x8(Variable::make(Float(32, 8), "*")),
    wild_f64x4(Variable::make(Float(64, 4), "*")),

    // Bounds of types
    min_i8(Int(8).min()),
    max_i8(Int(8).max()),
    max_u8(UInt(8).max()),

    min_i16(Int(16).min()),
    max_i16(Int(16).max()),
    max_u16(UInt(16).max()),

    min_i32(Int(32).min()),
    max_i32(Int(32).max()),
    max_u32(UInt(32).max()),

    min_i64(Int(64).min()),
    max_i64(Int(64).max()),
    max_u64(UInt(64).max()),

    min_f32(Float(32).min()),
    max_f32(Float(32).max()),

    min_f64(Float(64).min()),
    max_f64(Float(64).max()) {

}

void CodeGen_Posix::init_module() {
    CodeGen::init_module();

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
}

Value *CodeGen_Posix::save_stack() {
    llvm::Function *stacksave =
        llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::stacksave);
    return builder->CreateCall(stacksave);    
}

void CodeGen_Posix::restore_stack(llvm::Value *saved_stack) {
    llvm::Function *stackrestore =
        llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::stackrestore);
    builder->CreateCall(stackrestore, saved_stack);
}

CodeGen_Posix::Allocation CodeGen_Posix::create_allocation(const std::string &name, Type type, Expr size) {

    Allocation allocation;

    int bytes_per_element = type.bits / 8;
    if (const IntImm *int_size = size.as<IntImm>()) {
        allocation.stack_size = int_size->value * bytes_per_element;

        // Round up to nearest multiple of 32.
        allocation.stack_size = ((allocation.stack_size + 31)/32)*32;

        // If it's more than 8k, put it on the heap.
        if (allocation.stack_size > 8*1024) {
            allocation.stack_size = 0;
        }
    } else {
        allocation.stack_size = 0;
    }

    llvm::Type *llvm_type = llvm_type_of(type);

    if (allocation.stack_size) {
        // First, try to reuse an existing free block using a best-fit
        // algorithm. Note that we don't actually need to fit, because
        // it's possible to go back to the original alloca and
        // increase its size.
        int best_fit = -1;
        int best_waste = 0;
        for (size_t i = 0; i < free_stack_blocks.size(); i++) {
            int waste = free_stack_blocks[i].stack_size - allocation.stack_size;
            if (waste < 0) waste = -waste;
            if (i == 0 || waste <= best_waste) {
                best_fit = (int)i;
                best_waste = waste;
            }
        }
        
        if (best_fit >= 0) {

            // Grab the old allocation and remove it from the free blocks list
            Allocation old_alloc = free_stack_blocks[best_fit];
            free_stack_blocks[best_fit] = free_stack_blocks[free_stack_blocks.size()-1];
            free_stack_blocks.pop_back();

            if (old_alloc.stack_size < allocation.stack_size) {
                log(2) << "Allocation of " << name << " reusing an old smaller allocation\n";
                // We need to go back in time and rewrite the original
                // alloca to be slightly larger so we can fit this one
                // into the same space.
                Value *size = ConstantInt::get(i32, allocation.stack_size/32);
                // Insert the new version just before the old version
                string old_name = old_alloc.alloca_inst->getName();
                AllocaInst *new_alloca = new AllocaInst(i32x8, size, old_name + "_" + name, old_alloc.alloca_inst);
                // Replace all uses of the old version with the new version
                old_alloc.alloca_inst->replaceAllUsesWith(new_alloca);
                // Delete the old version
                old_alloc.alloca_inst->eraseFromParent();
                // For future rewrites to this alloca, we want to rewrite the new version
                allocation.alloca_inst = new_alloca;
                
            } else {
                log(2) << "Allocation of " << name << " reusing an old larger allocation\n";                
                allocation.alloca_inst = old_alloc.alloca_inst;
                allocation.stack_size = old_alloc.stack_size;
            }

            // No need to restore the stack if we're reusing an old allocation
            allocation.saved_stack = NULL;

            // The new allocation uses the same pointer and the same alloca instruction
            allocation.ptr = old_alloc.ptr;                

        } else {            
            // Do a new 32-byte aligned alloca
            allocation.saved_stack = save_stack();
            Value *size = ConstantInt::get(i32, allocation.stack_size/32);
            allocation.alloca_inst = builder->CreateAlloca(i32x8, size, name);
            allocation.ptr = builder->CreatePointerCast(allocation.alloca_inst, llvm_type->getPointerTo());
        }
    } else {
        allocation.saved_stack = NULL;
        allocation.alloca_inst = NULL;
        Value *llvm_size = codegen(size * bytes_per_element);

        // call malloc
        llvm::Function *malloc_fn = module->getFunction("halide_malloc");
        malloc_fn->setDoesNotAlias(0);
        assert(malloc_fn && "Could not find halide_malloc in module");        

        llvm_size = builder->CreateIntCast(llvm_size, malloc_fn->arg_begin()->getType(), false);

        log(4) << "Creating call to halide_malloc\n";
        CallInst *call = builder->CreateCall(malloc_fn, llvm_size);
        allocation.ptr = call;
    }

    // Push the allocation base pointer onto the symbol table
    log(3) << "Pushing allocation called " << name << ".host onto the symbol table\n";

    allocations.push(name, allocation);
    sym_push(name + ".host", allocation.ptr);

    return allocation;
}

void CodeGen_Posix::free_allocation(const std::string &name) {
    Allocation alloc = allocations.get(name);

    if (alloc.stack_size) {
        log(2) << "Moving allocation " << name << " onto the free stack blocks list\n";
        // Mark this block as free, but don't restore the stack yet
        free_stack_blocks.push_back(alloc);
    } else {
        // Call free
        llvm::Function *free_fn = module->getFunction("halide_free");
        assert(free_fn && "Could not find halide_free in module");
        log(4) << "Creating call to halide_free\n";
        builder->CreateCall(free_fn, alloc.ptr);
    }

    sym_pop(name + ".host");
    allocations.pop(name);
}

void CodeGen_Posix::destroy_allocation(Allocation alloc) {

    if (alloc.saved_stack) {
        // We should be in the free blocks list
        bool found = false;
        for (size_t i = 0; !found && i < free_stack_blocks.size(); i++) {            
            if (free_stack_blocks[i].ptr == alloc.ptr) {
                free_stack_blocks[i] = free_stack_blocks[free_stack_blocks.size()-1];
                free_stack_blocks.pop_back();
                found = true;
            }
        }
        assert(found && "Stack allocation should have been in the free blocks list");

        restore_stack(alloc.saved_stack);
    }
    
}

void CodeGen_Posix::visit(const Allocate *alloc) {
    
    Allocation allocation = create_allocation(alloc->name, alloc->type, alloc->size);

    codegen(alloc->body);

    // Should have been freed
    assert(!sym_exists(alloc->name + ".host"));
    assert(!allocations.contains(alloc->name));
    
    log(2) << "Destroying allocation " << alloc->name << "\n";
    destroy_allocation(allocation);
}

void CodeGen_Posix::visit(const Free *stmt) {
    free_allocation(stmt->name);
}

void CodeGen_Posix::prepare_for_early_exit() {
    // We've jumped to a code path that will be called just before
    // bailing out. Free everything outstanding.
    for (map<string, stack<Allocation> >::const_iterator iter = allocations.get_table().begin();
         iter != allocations.get_table().end(); ++iter) {
        string name = iter->first;
        std::vector<Allocation> stash;
        while (allocations.contains(name)) {
            stash.push_back(allocations.get(name));            
            free_allocation(name);
        }

        // Restore all the allocations before we jump back to the main
        // code path.
        for (size_t i = stash.size(); i > 0; i--) {
            allocations.push(name, stash[i-1]);
            sym_push(name + ".host", stash[i-1].ptr);
        }
    }
}

}}
