#include <iostream>

#include "CSE.h"
#include "CodeGen_Internal.h"
#include "CodeGen_Posix.h"
#include "Debug.h"
#include "IR.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace llvm;

CodeGen_Posix::CodeGen_Posix(const Target &t)
    : CodeGen_LLVM(t) {
}

Value *CodeGen_Posix::codegen_allocation_size(const std::string &name, Type type, const std::vector<Expr> &extents, const Expr &condition) {
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
    for (const auto &extent : extents) {
        Expr next_extent = cast(UInt(32), max(0, extent));

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

    if (!is_const_one(condition)) {
        size_check = simplify(size_check || !condition);
    }

    // For constant-sized allocations this check should simplify away.
    size_check = common_subexpression_elimination(simplify(size_check));
    if (!is_const_one(size_check)) {
        create_assertion(codegen(size_check || !condition),
                         Call::make(Int(32), "halide_error_buffer_allocation_too_large",
                                    {name, total_size, max_size}, Call::Extern));
    }

    total_size = simplify(total_size);
    return codegen(total_size);
}

CodeGen_Posix::Allocation CodeGen_Posix::create_allocation(const std::string &name, Type type, MemoryType memory_type,
                                                           const std::vector<Expr> &extents, const Expr &condition,
                                                           const Expr &new_expr, std::string free_function, int padding) {
    Value *llvm_size = nullptr;
    int64_t stack_bytes = 0;
    int32_t constant_bytes = Allocate::constant_allocation_size(extents, name);
    if (constant_bytes > 0) {
        constant_bytes *= type.bytes();
        stack_bytes = constant_bytes + padding * type.bytes();

        if (stack_bytes > target.maximum_buffer_size()) {
            const string str_max_size = target.has_large_buffers() ? "2^63 - 1" : "2^31 - 1";
            user_error << "Total size for allocation " << name << " is constant but exceeds " << str_max_size << ".";
        } else if (memory_type == MemoryType::Heap ||
                   (memory_type != MemoryType::Register &&
                    !can_allocation_fit_on_stack(stack_bytes))) {
            // We should put the allocation on the heap if it's
            // explicitly placed on the heap, or if it's not
            // explicitly placed in registers and it's large. Large
            // stack allocations become pseudostack allocations
            // instead.
            stack_bytes = 0;
            llvm_size = codegen(Expr(constant_bytes));
        }
    } else {
        // Should have been caught in bound_small_allocations
        internal_assert(memory_type != MemoryType::Register);
        llvm_size = codegen_allocation_size(name, type, extents, condition);
    }

    // Only allocate memory if the condition is true, otherwise 0.
    Value *llvm_condition = codegen(condition);
    if (llvm_size != nullptr) {
        // Add the requested padding to the allocation size. If the
        // allocation is on the stack, we can just read past the top
        // of the stack, so we only need this for heap allocations.
        Value *padding_bytes = ConstantInt::get(llvm_size->getType(), padding * type.bytes());
        llvm_size = builder->CreateAdd(llvm_size, padding_bytes);
        llvm_size = builder->CreateSelect(llvm_condition,
                                          llvm_size,
                                          ConstantInt::get(llvm_size->getType(), 0));
    }

    Allocation allocation;
    allocation.constant_bytes = constant_bytes;
    allocation.stack_bytes = new_expr.defined() ? 0 : stack_bytes;
    allocation.type = type;
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
        vector<Allocation>::iterator it = free_stack_allocs.end();
        for (it = free_stack_allocs.begin(); it != free_stack_allocs.end(); ++it) {
            if (it->pseudostack_slot) {
                // Don't merge with dynamic stack allocations
                continue;
            }
            AllocaInst *alloca_inst = dyn_cast<AllocaInst>(it->ptr);
            llvm::Function *allocated_in = alloca_inst ? alloca_inst->getParent()->getParent() : nullptr;
            llvm::Function *current_func = builder->GetInsertBlock()->getParent();

            if (allocated_in == current_func &&
                it->type == type &&
                it->stack_bytes >= stack_bytes) {
                break;
            }
        }
        if (it != free_stack_allocs.end()) {
            debug(4) << "Reusing freed stack allocation of " << it->stack_bytes
                     << " bytes for allocation " << name
                     << " of " << stack_bytes << " bytes.\n";
            // Use a free alloc we found.
            allocation.ptr = it->ptr;
            allocation.stack_bytes = it->stack_bytes;
            allocation.name = it->name;

            // This allocation isn't free anymore.
            free_stack_allocs.erase(it);
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
    } else if (memory_type == MemoryType::Stack && !new_expr.defined()) {
        // Try to find a free pseudostack allocation we can use.
        vector<Allocation>::iterator it = free_stack_allocs.end();
        for (it = free_stack_allocs.begin(); it != free_stack_allocs.end(); ++it) {
            if (!it->pseudostack_slot) {
                // Don't merge with static stack allocations
                continue;
            }
            AllocaInst *alloca_inst = dyn_cast<AllocaInst>(it->pseudostack_slot);
            llvm::Function *allocated_in = alloca_inst ? alloca_inst->getParent()->getParent() : nullptr;
            llvm::Function *current_func = builder->GetInsertBlock()->getParent();
            if (it->type == type &&
                allocated_in == current_func) {
                break;
            }
        }
        Value *slot = nullptr;
        if (it != free_stack_allocs.end()) {
            debug(4) << "Reusing freed pseudostack allocation from " << it->name
                     << " for " << name << "\n";
            slot = it->pseudostack_slot;
            allocation.name = it->name;
            allocation.destructor = it->destructor;
            // We've already created a destructor stack entry for this
            // pseudostack allocation, but we may not have actually
            // registered the destructor if we're reusing an
            // allocation that occurs conditionally. TODO: Why not
            // just register the destructor at entry?

            builder->CreateStore(builder->CreatePointerCast(slot, i8_t->getPointerTo()), allocation.destructor);
            free_stack_allocs.erase(it);
        } else {
            // Stack allocation with a dynamic size
            slot = create_alloca_at_entry(pseudostack_slot_t_type, 1, true, name + ".pseudostack_slot");
            llvm::Function *free_fn = module->getFunction("pseudostack_free");
            allocation.destructor = register_destructor(free_fn, slot, Always);
        }

        // Even if we're reusing a stack slot, we need to call
        // pseudostack_alloc to potentially reallocate.
        llvm::Function *alloc_fn = module->getFunction("pseudostack_alloc");
        internal_assert(alloc_fn) << "Could not find pseudostack_alloc in module\n";
        alloc_fn->setReturnDoesNotAlias();

        llvm::Function::arg_iterator arg_iter = alloc_fn->arg_begin();
        ++arg_iter;  // skip the user context *
        slot = builder->CreatePointerCast(slot, arg_iter->getType());
        ++arg_iter;  // skip the pointer to the stack slot
        llvm::Type *size_type = arg_iter->getType();
        llvm_size = builder->CreateIntCast(llvm_size, size_type, false);
        Value *args[3] = {get_user_context(), slot, llvm_size};
        Value *call = builder->CreateCall(alloc_fn, args);
        llvm::Type *ptr_type = llvm_type_of(type)->getPointerTo();
        call = builder->CreatePointerCast(call, ptr_type);

        // Figure out how much we need to allocate on the real stack
        Value *returned_non_null = builder->CreateIsNotNull(call);

        BasicBlock *here_bb = builder->GetInsertBlock();
        BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
        BasicBlock *need_alloca_bb = BasicBlock::Create(*context, "then_bb", function);

        builder->CreateCondBr(returned_non_null, after_bb, need_alloca_bb, very_likely_branch);
        builder->SetInsertPoint(need_alloca_bb);

        // Allocate it. It's zero most of the time.
        AllocaInst *alloca_inst = builder->CreateAlloca(i8_t->getPointerTo(), llvm_size);
        // Give it the right alignment
        alloca_inst->setAlignment(llvm::Align(native_vector_bits() / 8));

        // Set the pseudostack slot ptr to the right thing so we reuse
        // this pointer next time around.
        Value *stack_ptr = builder->CreatePointerCast(alloca_inst, ptr_type);
        Value *slot_ptr_ptr = builder->CreatePointerCast(slot, ptr_type->getPointerTo());
        builder->CreateStore(stack_ptr, slot_ptr_ptr);

        builder->CreateBr(after_bb);
        builder->SetInsertPoint(after_bb);

        PHINode *phi = builder->CreatePHI(ptr_type, 2);
        phi->addIncoming(stack_ptr, need_alloca_bb);
        phi->addIncoming(call, here_bb);

        allocation.ptr = phi;
        allocation.pseudostack_slot = slot;
    } else {
        if (new_expr.defined()) {
            allocation.ptr = codegen(new_expr);
        } else {
            // call malloc
            llvm::Function *malloc_fn = module->getFunction("halide_malloc");
            internal_assert(malloc_fn) << "Could not find halide_malloc in module\n";
            malloc_fn->setReturnDoesNotAlias();

            llvm::Function::arg_iterator arg_iter = malloc_fn->arg_begin();
            ++arg_iter;  // skip the user context *
            llvm_size = builder->CreateIntCast(llvm_size, arg_iter->getType(), false);

            debug(4) << "Creating call to halide_malloc for allocation " << name
                     << " of size " << type.bytes();
            for (const Expr &e : extents) {
                debug(4) << " x " << e;
            }
            debug(4) << "\n";
            Value *args[2] = {get_user_context(), llvm_size};

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
        if (!is_const_one(condition)) {
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
    debug(3) << "Pushing allocation called " << name << " onto the symbol table\n";

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
    } else if (alloc.pseudostack_slot) {
        // Don't call the destructor yet - the lifetime persists until function exit.
        free_stack_allocs.push_back(alloc);
    } else if (alloc.destructor_function) {
        internal_assert(alloc.destructor);
        trigger_destructor(alloc.destructor_function, alloc.destructor);
    }

    allocations.pop(name);
    sym_pop(name);
}

string CodeGen_Posix::get_allocation_name(const std::string &n) {
    if (allocations.contains(n)) {
        return allocations.get(n).name;
    } else {
        return n;
    }
}

void CodeGen_Posix::visit(const Allocate *alloc) {
    if (sym_exists(alloc->name)) {
        user_error << "Can't have two different buffers with the same name: "
                   << alloc->name << "\n";
    }

    Allocation allocation = create_allocation(alloc->name, alloc->type, alloc->memory_type,
                                              alloc->extents, alloc->condition,
                                              alloc->new_expr, alloc->free_function, alloc->padding);
    sym_push(alloc->name, allocation.ptr);

    codegen(alloc->body);

    // If there was no early free, free it now.
    if (allocations.contains(alloc->name)) {
        free_allocation(alloc->name);
    }
}

void CodeGen_Posix::visit(const Free *stmt) {
    free_allocation(stmt->name);
}

}  // namespace Internal
}  // namespace Halide
