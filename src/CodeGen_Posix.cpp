#include <iostream>

#include "CodeGen_Posix.h"
#include "CodeGen_Internal.h"
#include "LLVM_Headers.h"
#include "IR.h"
#include "IROperator.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::map;
using std::pair;
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

Value *CodeGen_Posix::codegen_allocation_size(const std::string &name, Type type, const std::vector<Expr> &extents) {
    // Compute size from list of extents checking for 32-bit signed overflow.
    llvm::Function *mul_overflow_fn = module->getFunction("llvm.smul.with.overflow.i32");

    // TODO: Make a method in code gen to handle getting or greating
    // a function through llvm. See also: CodeGen_ARM::call_intrin
    if (!mul_overflow_fn) {
        llvm::Type *result_type = llvm::StructType::get(i32, i1, NULL);
        vector<llvm::Type *> arg_types;
        arg_types.push_back(i32);
        arg_types.push_back(i32);
        FunctionType *func_t = FunctionType::get(result_type, arg_types, false);
        mul_overflow_fn = llvm::Function::Create(func_t,
                                                 llvm::Function::ExternalLinkage,
                                                 "llvm.smul.with.overflow.i32", module);
        mul_overflow_fn->setCallingConv(CallingConv::C);
        mul_overflow_fn->setDoesNotAccessMemory();
        mul_overflow_fn->setDoesNotThrow();
    }
    assert(mul_overflow_fn != NULL);

    Value *llvm_size;
    Value *overflow = NULL;
    if (extents.size() == 0) {
      llvm_size = codegen(Expr(0));
    } else {
        llvm_size = codegen(Expr(type.bytes()));
        for (size_t i = 0; i < extents.size(); i++) {
            Value *terms[2] = { llvm_size, codegen(extents[i]) };
            CallInst *multiply = builder->CreateCall(mul_overflow_fn, terms);
            llvm_size = builder->CreateExtractValue(multiply, vec(0U));
            Value *new_overflow = builder->CreateExtractValue(multiply, vec(1U));
            if (overflow == NULL) {
                overflow = new_overflow;
            } else {
                overflow = builder->CreateOr(overflow, new_overflow);
            }
        }
    }
    if (overflow != NULL) {
        create_assertion(builder->CreateNot(overflow),
                         std::string("32-bit signed overflow computing size of allocation ") + name);
    }
    return llvm_size;
}

CodeGen_Posix::Allocation CodeGen_Posix::create_allocation(const std::string &name, Type type, const std::vector<Expr> &extents) {

    Allocation allocation;
    Value *llvm_size = NULL;

    int32_t constant_size;
    if (constant_allocation_size(extents, name, constant_size)) {
        int64_t stack_bytes = constant_size * type.bytes();

        // Special case for zero sized allocation.
        if (stack_bytes == 0)
            stack_bytes = 1;

        if (stack_bytes > ((int64_t(1) << 31) - 1)) {
            std::cerr << "Total size for allocation " << name << " is constant but exceeds 2^31 - 1.";
            assert(false);
        } else if (stack_bytes <= 1024 * 8) {
            // Round up to nearest multiple of 32.
            allocation.stack_size = static_cast<int32_t>(((stack_bytes + 31)/32)*32);
        } else {
            allocation.stack_size = 0;
            llvm_size = codegen(Expr(static_cast<int32_t>(stack_bytes)));
        }
    } else {
        allocation.stack_size = 0;
        llvm_size = codegen_allocation_size(name, type, extents);
    }

    llvm::Type *llvm_type = llvm_type_of(type);

    if (allocation.stack_size) {

        // We used to do the alloca locally and save and restore the
        // stack pointer, but this makes llvm generate streams of
        // spill/reloads.
        Value *ptr = create_alloca_at_entry(i32x8, allocation.stack_size/32, name);
        allocation.ptr = builder->CreatePointerCast(ptr, llvm_type->getPointerTo());

    } else {
        // call malloc
        llvm::Function *malloc_fn = module->getFunction("halide_malloc");
        malloc_fn->setDoesNotAlias(0);
        assert(malloc_fn && "Could not find halide_malloc in module");

        llvm::Function::arg_iterator arg_iter = malloc_fn->arg_begin();
        ++arg_iter;  // skip the user context *
        llvm_size = builder->CreateIntCast(llvm_size, arg_iter->getType(), false);

        debug(4) << "Creating call to halide_malloc\n";
        Value *args[2] = { get_user_context(), llvm_size };

        CallInst *call = builder->CreateCall(malloc_fn, args);
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
        Value *args[2] = { get_user_context(), alloc.ptr };
        builder->CreateCall(free_fn, args);
    }

    allocations.pop(name);
}

void CodeGen_Posix::destroy_allocation(Allocation alloc) {
    // Heap allocations have already been freed.
}

void CodeGen_Posix::visit(const Allocate *alloc) {

    if (sym_exists(alloc->name + ".host")) {
        std::cerr << "Can't have two different buffers with the same name: "
                  << alloc->name << "\n";
        assert(false);
    }

    Allocation allocation = create_allocation(alloc->name, alloc->type, alloc->extents);
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
