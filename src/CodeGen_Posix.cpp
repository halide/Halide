#include <iostream>

#include "CodeGen_Posix.h"
#include "CodeGen_Internal.h"
#include "LLVM_Headers.h"
#include "IR.h"
#include "IROperator.h"
#include "Debug.h"
#include "IRPrinter.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::map;
using std::pair;
using std::make_pair;

using namespace llvm;

CodeGen_Posix::CodeGen_Posix(Target t) :
  CodeGen_LLVM(t) {
}

Value *CodeGen_Posix::codegen_allocation_size(const std::string &name, Type type, const std::vector<Expr> &extents) {
    // Compute size from list of extents checking for 32-bit signed overflow.
    // Math is done using 64-bit intergers as overflow checked 32-bit mutliply
    // does not work with NaCl at the moment.

    Expr no_overflow = const_true(1);
    Expr total_size = cast<int64_t>(type.width * type.bytes());
    Expr max_size;
    if (target.bits < 64) {
        max_size = cast<int64_t>(0x7fffffff);
    } else {
        // The Halide compiler currently can't represent
        // 64-bit immediate values.
        max_size = (cast<int64_t>(1) << cast<int64_t>(63)) - cast<int64_t>(1);
    }
    for (size_t i = 0; i < extents.size(); i++) {
        total_size *= extents[i];
        no_overflow = no_overflow && (total_size <= max_size);
    }

    // For constant-sized allocations this check should simplify away.
    no_overflow = simplify(no_overflow);
    if (!is_one(no_overflow)) {
        create_assertion(codegen(no_overflow),
                         Call::make(Int(32), "halide_error_buffer_allocation_too_large",
                                    {name, total_size, max_size}, Call::Extern));
    }

    total_size = simplify(total_size);
    return codegen(total_size);
}

CodeGen_Posix::Allocation CodeGen_Posix::create_allocation(const std::string &name, Type type,
                                                           const std::vector<Expr> &extents, Expr condition,
                                                           Expr new_expr, std::string free_function) {
    Value *llvm_size = NULL;
    int64_t stack_bytes = 0;
    const int64_t max_size = target.bits == 64 ? 0x7fffffffffffffff : 0x7fffffff;
    int32_t constant_bytes = 0;
    if (constant_allocation_size(extents, name, constant_bytes)) {
        constant_bytes *= type.bytes();
        stack_bytes = constant_bytes;

        if (stack_bytes > max_size) {
            const string str_max_size = target.bits == 64 ? "2^63 - 1" : "2^31 - 1";
            user_error << "Total size for allocation " << name << " is constant but exceeds " << str_max_size << ".";
        } else if (stack_bytes <= 1024 * 16) {
            // Round up to nearest multiple of 32.
            stack_bytes = ((stack_bytes + 31)/32)*32;
        } else {
            stack_bytes = 0;
            llvm_size = codegen(Expr(constant_bytes));
        }
    } else {
        llvm_size = codegen_allocation_size(name, type, extents);
    }

    // Only allocate memory if the condition is true, otherwise 0.
    if (llvm_size != NULL) {
        Value *llvm_condition = codegen(condition);
        llvm_size = builder->CreateSelect(llvm_condition,
                                          llvm_size,
                                          ConstantInt::get(llvm_size->getType(), 0));
    }

    Allocation allocation;
    allocation.constant_bytes = constant_bytes;
    allocation.stack_bytes = new_expr.defined() ? 0 : stack_bytes;
    allocation.ptr = NULL;
    allocation.destructor = NULL;
    allocation.destructor_function = NULL;

    if (!new_expr.defined() && stack_bytes != 0) {
        // Try to find a free stack allocation we can use.
        vector<Allocation>::iterator free = free_stack_allocs.end();
        for (free = free_stack_allocs.begin(); free != free_stack_allocs.end(); ++free) {
            AllocaInst *alloca_inst = dyn_cast<AllocaInst>(free->ptr);
            llvm::Function *allocated_in = alloca_inst ? alloca_inst->getParent()->getParent() : NULL;
            llvm::Function *current_func = builder->GetInsertBlock()->getParent();

            if (allocated_in == current_func &&
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

            // This allocation isn't free anymore.
            free_stack_allocs.erase(free);
        } else {
            debug(4) << "Allocating " << stack_bytes << " bytes on the stack for " << name << "\n";
            // We used to do the alloca locally and save and restore the
            // stack pointer, but this makes llvm generate streams of
            // spill/reloads.
            allocation.ptr = create_alloca_at_entry(i32x8, stack_bytes/32, false, name);
            allocation.stack_bytes = stack_bytes;
        }
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

            CallInst *call = builder->CreateCall(malloc_fn, args);
            allocation.ptr = call;
        }

        // Assert that the allocation worked.
        Value *check = builder->CreateIsNotNull(allocation.ptr);
        if (!new_expr.defined()) { // Zero sized allocation if allowed for custom new...
            Value *zero_size = builder->CreateIsNull(llvm_size);
            check = builder->CreateOr(check, zero_size);
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

    // Should have been freed
    internal_assert(!sym_exists(alloc->name + ".host"));
    internal_assert(!allocations.contains(alloc->name));
}

void CodeGen_Posix::visit(const Free *stmt) {
    Allocation alloc = allocations.get(stmt->name);

    if (alloc.stack_bytes) {
        // Remember this allocation so it can be re-used by a later allocation.
        free_stack_allocs.push_back(alloc);
    } else {
        internal_assert(alloc.destructor);
        trigger_destructor(alloc.destructor_function, alloc.destructor);
    }

    allocations.pop(stmt->name);
    sym_pop(stmt->name + ".host");
}

}}
