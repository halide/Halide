#include <iostream>

#include "CodeGen_Posix.h"
#include "CodeGen_Internal.h"
#include "LLVM_Headers.h"
#include "IR.h"
#include "IROperator.h"
#include "Debug.h"
#include "IRPrinter.h"
#include "Simplify.h"
#include "CSE.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::map;
using std::pair;

using namespace llvm;

CodeGen_Posix::CodeGen_Posix(Target t) :
  CodeGen_LLVM(t) {
}

Value *CodeGen_Posix::codegen_allocation_size(const std::string &name, Type type, const std::vector<Expr> &extents) {
    // Compute size from list of extents checking for overflow.

    Expr overflow = make_zero(UInt(64));
    Expr total_size = make_const(UInt(64), type.lanes() * type.bytes());

    // We'll multiply all the extents into the 64-bit value
    // total_size. We'll also track (total_size >> 32) as a 64-bit
    // value to check for overflow as we go. The loop invariant will
    // be that either the overflow Expr is non-zero, or total_size_hi
    // only occupies the bottom 32-bits. Overflow could be more simply
    // checked for using division, but that's slower at runtime. This
    // method generates much better assembly.
    Expr total_size_hi = make_zero(UInt(64));

    Expr low_mask = make_const(UInt(64), (uint64_t)(0xffffffff));
    for (size_t i = 0; i < extents.size(); i++) {
        Expr next_extent = cast(UInt(32), extents[i]);

        // Update total_size >> 32. This math can't overflow due to
        // the loop invariant:
        total_size_hi *= next_extent;
        // Deal with carry from the low bits. Still can't overflow.
        total_size_hi += ((total_size & low_mask) * next_extent) >> 32;

        // Update total_size. This may overflow.
        total_size *= next_extent;

        // We can check for overflow by asserting that total_size_hi
        // is still a 32-bit number.
        overflow = overflow | (total_size_hi >> 32);
    }

    Expr max_size = make_const(UInt(64), target.maximum_buffer_size());
    Expr size_check = (overflow == 0) && (total_size <= max_size);

    // For constant-sized allocations this check should simplify away.
    size_check = common_subexpression_elimination(simplify(size_check));
    if (!is_one(size_check)) {
        create_assertion(codegen(size_check),
                         Call::make(Int(32), "halide_error_buffer_allocation_too_large",
                                    {name, total_size, max_size}, Call::Extern));
    }

    total_size = simplify(total_size);
    return codegen(total_size);
}

int CodeGen_Posix::allocation_padding(Type type) const {
    // We potentially load one scalar value past the end of the
    // buffer, so pad the allocation with an extra instance of the
    // scalar type.
    return type.bytes();
}

CodeGen_Posix::Allocation CodeGen_Posix::create_allocation(const std::string &name, Type type,
                                                           const std::vector<Expr> &extents, Expr condition,
                                                           Expr new_expr, std::string free_function) {
    Value *llvm_size = nullptr;
    int64_t stack_bytes = 0;
    int32_t constant_bytes = Allocate::constant_allocation_size(extents, name);
    if (constant_bytes > 0) {
        constant_bytes *= type.bytes();
        stack_bytes = constant_bytes;

        if (stack_bytes > target.maximum_buffer_size()) {
            const string str_max_size = target.has_feature(Target::LargeBuffers) ? "2^63 - 1" : "2^31 - 1";
            user_error << "Total size for allocation " << name << " is constant but exceeds " << str_max_size << ".";
        } else if (!can_allocation_fit_on_stack(stack_bytes)) {
            stack_bytes = 0;
            llvm_size = codegen(Expr(constant_bytes));
        }
    } else {
        llvm_size = codegen_allocation_size(name, type, extents);
    }

    // Only allocate memory if the condition is true, otherwise 0.
    Value *llvm_condition = codegen(condition);
    if (llvm_size != nullptr) {
        // Add the requested padding to the allocation size. If the
        // allocation is on the stack, we can just read past the top
        // of the stack, so we only need this for heap allocations.
        Value *padding = ConstantInt::get(llvm_size->getType(), allocation_padding(type));
        llvm_size = builder->CreateAdd(llvm_size, padding);


        llvm_size = builder->CreateSelect(llvm_condition,
                                          llvm_size,
                                          ConstantInt::get(llvm_size->getType(), 0));
    }

    Allocation allocation;
    allocation.constant_bytes = constant_bytes;
    allocation.stack_bytes = new_expr.defined() ? 0 : stack_bytes;
    allocation.type = type;
    allocation.ptr = nullptr;
    allocation.destructor = nullptr;
    allocation.destructor_function = nullptr;
    allocation.name = name;

    if (!new_expr.defined() && extents.empty()) {
        // If it's a scalar allocation, don't try anything clever. We
        // want llvm to be able to promote it to a register.
        allocation.ptr = create_alloca_at_entry(llvm_type_of(type), 1, false, name);
        allocation.stack_bytes = stack_bytes;
        cur_stack_alloc_total += allocation.stack_bytes;
        debug(4) << "cur_stack_alloc_total += " << allocation.stack_bytes << " -> " << cur_stack_alloc_total << " for " << name << "\n";
    } else if (!new_expr.defined() && stack_bytes != 0) {

        // Try to find a free stack allocation we can use.
        vector<Allocation>::iterator free = free_stack_allocs.end();
        for (free = free_stack_allocs.begin(); free != free_stack_allocs.end(); ++free) {
            AllocaInst *alloca_inst = dyn_cast<AllocaInst>(free->ptr);
            llvm::Function *allocated_in = alloca_inst ? alloca_inst->getParent()->getParent() : nullptr;
            llvm::Function *current_func = builder->GetInsertBlock()->getParent();

            if (allocated_in == current_func &&
                free->type == type &&
                free->stack_bytes >= stack_bytes) {
                break;
            }
        }
        if (free != free_stack_allocs.end()) {
            debug(4) << "Reusing freed stack allocation of " << free->stack_bytes
                     << " bytes for allocation " << name
                     << " of " << stack_bytes << " bytes.\n";
            // Use a free alloc we found.
            allocation.ptr = free->ptr;
            allocation.stack_bytes = free->stack_bytes;
            allocation.name = free->name;

            // This allocation isn't free anymore.
            free_stack_allocs.erase(free);
        } else {
            debug(4) << "Allocating " << stack_bytes << " bytes on the stack for " << name << "\n";
            // We used to do the alloca locally and save and restore the
            // stack pointer, but this makes llvm generate streams of
            // spill/reloads.
            int64_t stack_size = (stack_bytes + type.bytes() - 1) / type.bytes();
            // Handles are stored as uint64s
            llvm::Type *t =
                llvm_type_of(type.is_handle() ? UInt(64, type.lanes()) : type);   
            allocation.ptr = create_alloca_at_entry(t, stack_size, false, name);
            allocation.stack_bytes = stack_bytes;
        }
        cur_stack_alloc_total += allocation.stack_bytes;
        debug(4) << "cur_stack_alloc_total += " << allocation.stack_bytes << " -> " << cur_stack_alloc_total << " for " << name << "\n";
    } else {
        if (new_expr.defined()) {
            allocation.ptr = codegen(new_expr);
        } else {
            // call malloc
            llvm::Function *malloc_fn = module->getFunction("halide_malloc");
            internal_assert(malloc_fn) << "Could not find halide_malloc in module\n";
            malloc_fn->setDoesNotAlias(0);

            llvm::Function::arg_iterator arg_iter = malloc_fn->arg_begin();
            ++arg_iter;  // skip the user context *
            llvm_size = builder->CreateIntCast(llvm_size, arg_iter->getType(), false);

            debug(4) << "Creating call to halide_malloc for allocation " << name
                     << " of size " << type.bytes();
            for (Expr e : extents) {
                debug(4) << " x " << e;
            }
            debug(4) << "\n";
            Value *args[2] = { get_user_context(), llvm_size };

            Value *call = builder->CreateCall(malloc_fn, args);

            // Fix the type to avoid pointless bitcasts later
            call = builder->CreatePointerCast(call, llvm_type_of(type)->getPointerTo());    

            allocation.ptr = call;
        }

        // Assert that the allocation worked.
        Value *check = builder->CreateIsNotNull(allocation.ptr);
        if (llvm_size) {
            Value *zero_size = builder->CreateIsNull(llvm_size);
            check = builder->CreateOr(check, zero_size);
        }
        if (!is_one(condition)) {
            // If the condition is false, it's OK for the new_expr to be null.
            Value *condition_is_false = builder->CreateIsNull(llvm_condition);
            check = builder->CreateOr(check, condition_is_false);
        }

        create_assertion(check, Call::make(Int(32), "halide_error_out_of_memory",
                                           std::vector<Expr>(), Call::Extern));

        // Register a destructor for this allocation.
        if (free_function.empty()) {
            free_function = "halide_free";
        }
        llvm::Function *free_fn = module->getFunction(free_function);
        internal_assert(free_fn) << "Could not find " << free_function << " in module.\n";
        allocation.destructor = register_destructor(free_fn, allocation.ptr, OnError);
        allocation.destructor_function = free_fn;
    }

    // Push the allocation base pointer onto the symbol table
    debug(3) << "Pushing allocation called " << name << ".host onto the symbol table\n";

    allocations.push(name, allocation);

    return allocation;
}

void CodeGen_Posix::free_allocation(const std::string &name) {
    Allocation alloc = allocations.get(name);

    if (alloc.stack_bytes) {
        // Remember this allocation so it can be re-used by a later allocation.
        free_stack_allocs.push_back(alloc);
        cur_stack_alloc_total -= alloc.stack_bytes;
        debug(4) << "cur_stack_alloc_total -= " << alloc.stack_bytes << " -> " << cur_stack_alloc_total << " for " << name << "\n";
    } else {
        internal_assert(alloc.destructor);
        trigger_destructor(alloc.destructor_function, alloc.destructor);
    }

    allocations.pop(name);
    sym_pop(name + ".host");
}

string CodeGen_Posix::get_allocation_name(const std::string &n) {
    if (allocations.contains(n)) {
        return allocations.get(n).name;
    } else {
        return n;
    }
}

void CodeGen_Posix::visit(const Allocate *alloc) {
    if (sym_exists(alloc->name + ".host")) {
        user_error << "Can't have two different buffers with the same name: "
                   << alloc->name << "\n";
    }

    Allocation allocation = create_allocation(alloc->name, alloc->type,
                                              alloc->extents, alloc->condition,
                                              alloc->new_expr, alloc->free_function);
    sym_push(alloc->name + ".host", allocation.ptr);

    codegen(alloc->body);

    // If there was no early free, free it now.
    if (allocations.contains(alloc->name)) {
        free_allocation(alloc->name);
    }
}

void CodeGen_Posix::visit(const Free *stmt) {
    free_allocation(stmt->name);
}

}}
