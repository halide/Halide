#include "LLVM_Headers.h"
#include "CodeGen_X86.h"
#include "IROperator.h"
#include <iostream>
#include "buffer_t.h"
#include "IRPrinter.h"
#include "IRMatch.h"
#include "Debug.h"
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
using std::make_pair;

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

CodeGen_Posix::Allocation CodeGen_Posix::create_allocation(const std::string &name, Type type, Expr size) {

    Allocation allocation;

    if (const IntImm *int_size = size.as<IntImm>()) {
        allocation.stack_size = int_size->value * type.bytes();

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
        // Do a new 32-byte aligned alloca
        allocation.saved_stack = save_stack();
        Value *size = ConstantInt::get(i32, allocation.stack_size/32);
        Value *ptr = builder->CreateAlloca(i32x8, size, name);
        allocation.ptr = builder->CreatePointerCast(ptr, llvm_type->getPointerTo());
    } else {
        allocation.saved_stack = NULL;
        Value *llvm_size = codegen(size * type.bytes());

        // call malloc
        llvm::Function *malloc_fn = module->getFunction("halide_malloc");
        malloc_fn->setDoesNotAlias(0);
        assert(malloc_fn && "Could not find halide_malloc in module");

        llvm_size = builder->CreateIntCast(llvm_size, malloc_fn->arg_begin()->getType(), false);

        debug(4) << "Creating call to halide_malloc\n";
        CallInst *call = builder->CreateCall(malloc_fn, llvm_size);
        allocation.ptr = call;

        // Assert that the allocation worked.
        create_assertion(builder->CreateIsNotNull(allocation.ptr),
                         "Out of memory (malloc returned NULL)");
    }

    // Push the allocation base pointer onto the symbol table
    debug(3) << "Pushing allocation called " << name << ".host onto the symbol table\n";

    allocations.push(name, allocation);

    return allocation;
}

void CodeGen_Posix::free_allocation(const std::string &name) {
    Allocation alloc = allocations.get(name);

    assert(alloc.ptr);

    CallInst *call_inst = dyn_cast<CallInst>(alloc.ptr);
    llvm::Function *allocated_in = call_inst ? call_inst->getParent()->getParent() : NULL;
    llvm::Function *current_func = builder->GetInsertBlock()->getParent();

    if (alloc.stack_size) {
        // Free is a no-op for stack allocations
    } else if (allocated_in == current_func) { // Skip over allocations from outside this function.
        // Call free
        llvm::Function *free_fn = module->getFunction("halide_free");
        assert(free_fn && "Could not find halide_free in module");
        debug(4) << "Creating call to halide_free\n";
        builder->CreateCall(free_fn, alloc.ptr);
    }

    allocations.pop(name);
}

void CodeGen_Posix::destroy_allocation(Allocation alloc) {

    if (alloc.saved_stack) {
        restore_stack(alloc.saved_stack);
    }

    // Heap allocations have already been freed.
}

void CodeGen_Posix::visit(const Allocate *alloc) {

    Allocation allocation = create_allocation(alloc->name, alloc->type, alloc->size);
    sym_push(alloc->name + ".host", allocation.ptr);

    codegen(alloc->body);

    // Should have been freed
    assert(!sym_exists(alloc->name + ".host"));
    assert(!allocations.contains(alloc->name));

    debug(2) << "Destroying allocation " << alloc->name << "\n";
    destroy_allocation(allocation);
}

void CodeGen_Posix::visit(const Free *stmt) {
    free_allocation(stmt->name);
    sym_pop(stmt->name + ".host");
}

void CodeGen_Posix::prepare_for_early_exit() {
    // We've jumped to a code path that will be called just before
    // bailing out. Free everything outstanding.
    vector<string> names;
    for (Scope<Allocation>::iterator iter = allocations.begin();
         iter != allocations.end(); ++iter) {
        names.push_back(iter.name());
    }

    for (size_t i = 0; i < names.size(); i++) {
        std::vector<Allocation> stash;
        while (allocations.contains(names[i])) {
            // The value in the symbol table is not necessarily the
            // one in the allocation - it may have been forwarded
            // inside a parallel for loop
            stash.push_back(allocations.get(names[i]));
            free_allocation(names[i]);
        }

        // Restore all the allocations before we jump back to the main
        // code path.
        for (size_t j = stash.size(); j > 0; j--) {
            allocations.push(names[i], stash[j-1]);
        }
    }
}

}}
