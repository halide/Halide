#include "LowerParallelTasks.h"

#include <string>

#include "Argument.h"
#include "Closure.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Module.h"

namespace Halide {
namespace Internal {

// TODO(zalman): Find a better place for this code to live.
LoweredFunc GenreateClosureIR(const std::string &name, const Closure &closure, const Stmt &body) {
    std::vector<LoweredArgument> args;
  
    // Figure out if user_context has to be dealt with here.
    std::string closure_arg_name = unique_name("closure_arg");
    args.emplace_back(closure_arg_name, Argument::Kind::InputScalar,
                      type_of<void *>(), 0, ArgumentEstimates());
    Expr closure_arg = Variable::make(type_of<void *>(), closure_arg_name);

    Stmt wrapped_body = body;
    std::vector<Expr> type_args(closure.vars.size() + closure.buffers.size() * 2 + 1);
    std::string closure_type_name = unique_name("closure_struct_type");
    Expr struct_type = Variable::make(Handle(), closure_type_name);
    type_args[0] = StringImm::make(closure_type_name);

    int struct_index = 0;
    for (const auto &v : closure.vars) {
        type_args[struct_index + 1] = make_zero(v.second);
        wrapped_body = LetStmt::make(v.first,
                                     Call::make(v.second, Call::load_struct_member,
                                                { closure_arg, struct_type, struct_index },
                                                Call::PureIntrinsic),
                                     wrapped_body);
        struct_index++;
    }
    for (const auto &b : closure.buffers) {
        type_args[struct_index + 1] = make_zero(type_of<void *>());
        type_args[struct_index + 2] = make_zero(type_of<halide_buffer_t *>());
        wrapped_body = LetStmt::make(b.first,
                                     Call::make(type_of<void *>(), Call::load_struct_member,
                                                { closure_arg, struct_type, struct_index },
                                                Call::PureIntrinsic),
                                     wrapped_body);
        wrapped_body = LetStmt::make(b.first + ".buffer",
                                     Call::make(type_of<halide_buffer_t *>(), Call::load_struct_member,
                                                { closure_arg, struct_type, struct_index + 1},
                                                Call::PureIntrinsic),
                                     wrapped_body);
        struct_index += 2;
    }

    Expr struct_type_decl = Call::make(Handle(), Call::make_struct_type, type_args, Call::PureIntrinsic);
    wrapped_body = LetStmt::make(closure_type_name, struct_type_decl, wrapped_body);

    return {name, args, wrapped_body, LinkageType::Internal, NameMangling::Default };
}

Expr AllocateClosure(const std::string &name, const Closure &closure) {
    std::vector<Expr> closure_elements;
    // TODO(zalman): Ensure this is unique within scopes it is used in.
    closure_elements.push_back(StringImm::make(name + "_closure"));
    for (const auto &v : closure.vars) {
        closure_elements.push_back(Variable::make(v.second, v.first));
    }
    for (const auto &b : closure.buffers) {
        // TODO(zalman): Verify types here...
        closure_elements.push_back(Variable::make(type_of<void *>(), b.first));
        // TODO(zalman): this has to allow a failed lookup.
        closure_elements.push_back(Variable::make(type_of<halide_buffer_t *>(), b.first + ".buffer"));
    }    
    return Call::make(type_of<void *>(), Call::make_struct, closure_elements, Call::Intrinsic);
}

Expr GenrerateClosureCall(const std::string &name, const Closure &closure, const Expr &closure_storage) {
    // TODO(zalman): Figure out user_context.
    return Call::make(Int(32), name, { closure_storage }, Call::Extern);
}

namespace {

std::string task_debug_name(const std::pair<std::string, int> &prefix) {
    if (prefix.second <= 1) {
        return prefix.first;
    } else {
        return prefix.first + "_" + std::to_string(prefix.second - 1);
    }
}

void add_fork(std::pair<std::string, int> &prefix) {
    if (prefix.second == 0) {
        prefix.first += ".fork";
    }
    prefix.second++;
}

void add_suffix(std::pair<std::string, int> &prefix, const std::string &suffix) {
    if (prefix.second > 1) {
        prefix.first += "_" + std::to_string(prefix.second - 1);
        prefix.second = 0;
    }
    prefix.first += suffix;
}

}  // namespace

struct LowerParallelTasks : public IRMutator {
  
    /** Codegen a call to do_parallel_tasks */
    struct ParallelTask {
        Stmt body;
        struct SemAcquire {
            Expr semaphore;
            Expr count;
        };
        std::vector<SemAcquire> semaphores;
        std::string loop_var;
        Expr min, extent;
        Expr serial;
        std::string name;
    };

    using IRMutator::visit;

    Stmt visit(const For *op) override {
        const Acquire *acquire = op->body.as<Acquire>();

        if (op->for_type == ForType::Parallel ||
            (op->for_type == ForType::Serial &&
             acquire &&
             !expr_uses_var(acquire->count, op->name))) {
            return do_as_parallel_task(op);
        }
        return op;
    }

    Stmt visit(const Acquire *op) override {
        return do_as_parallel_task(op);
    }

    Stmt visit(const Fork *op) override {
        return do_as_parallel_task(op);
    }

    Stmt rewrite_parallel_tasks(const std::vector<ParallelTask> &tasks) {
        Stmt body;
      
        Closure closure;
        for (const auto &t : tasks) {
            Stmt s = t.body;
            if (!t.loop_var.empty()) {
                s = LetStmt::make(t.loop_var, 0, s);
            }
            s.accept(&closure);
        }

        int num_tasks = (int)(tasks.size());
        std::string tasks_allocation_name = unique_name("tasks_allocation");
        // TODO(zalman): Might need to get type correct here.
        Expr tasks_list = Variable::make(Handle(), tasks_allocation_name);

        std::string closure_name = unique_name("parallel_closure");
        Expr closure_struct = AllocateClosure(closure_name, closure);
 
        // TODO: Allocate tasks list and wrap Let around body.
    #if 0
        // Make space on the stack for the tasks
        llvm::Value *task_stack_ptr = create_alloca_at_entry(parallel_task_t_type, num_tasks);

        llvm::Type *args_t[] = {i8_t->getPointerTo(), i32_t, i8_t->getPointerTo()};
        FunctionType *task_t = FunctionType::get(i32_t, args_t, false);
        llvm::Type *loop_args_t[] = {i8_t->getPointerTo(), i32_t, i32_t, i8_t->getPointerTo(), i8_t->getPointerTo()};
        FunctionType *loop_task_t = FunctionType::get(i32_t, loop_args_t, false);

        Value *result = nullptr;
    #endif

        for (int i = 0; i < num_tasks; i++) {
            ParallelTask t = tasks[i];

            // Analyze the task body
            class MayBlock : public IRVisitor {
                using IRVisitor::visit;
                void visit(const Acquire *op) override {
                    result = true;
                }

            public:
                bool result = false;
            };

            // TODO(zvookin|abadams): This makes multiple passes over the
            // IR to cover each node. (One tree walk produces the min
            // thread count for all nodes, but we redo each subtree when
            // compiling a given node.) Ideally we'd move to a lowering pass
            // that converts our parallelism constructs to Call nodes, or
            // direct hardware operations in some cases.
            // Also, this code has to exactly mirror the logic in get_parallel_tasks.
            // It would be better to do one pass on the tree and centralize the task
            // deduction logic in one place.
            class MinThreads : public IRVisitor {
                using IRVisitor::visit;

                std::pair<Stmt, int> skip_acquires(Stmt first) {
                    int count = 0;
                    while (first.defined()) {
                        const Acquire *acq = first.as<Acquire>();
                        if (acq == nullptr) {
                            break;
                        }
                        count++;
                        first = acq->body;
                    }
                    return {first, count};
                }

                void visit(const Fork *op) override {
                    int total_threads = 0;
                    int direct_acquires = 0;
                    // Take the sum of min threads across all
                    // cascaded Fork nodes.
                    const Fork *node = op;
                    while (node != nullptr) {
                        result = 0;
                        auto after_acquires = skip_acquires(node->first);
                        direct_acquires += after_acquires.second;

                        after_acquires.first.accept(this);
                        total_threads += result;

                        const Fork *continued_branches = node->rest.as<Fork>();
                        if (continued_branches == nullptr) {
                            result = 0;
                            after_acquires = skip_acquires(node->rest);
                            direct_acquires += after_acquires.second;
                            after_acquires.first.accept(this);
                            total_threads += result;
                        }
                        node = continued_branches;
                    }
                    if (direct_acquires == 0 && total_threads == 0) {
                        result = 0;
                    } else {
                        result = total_threads + 1;
                    }
                }

                void visit(const For *op) override {
                    result = 0;

                    if (op->for_type == ForType::Parallel) {
                        IRVisitor::visit(op);
                        if (result > 0) {
                            result += 1;
                        }
                    } else if (op->for_type == ForType::Serial) {
                        auto after_acquires = skip_acquires(op->body);
                        if (after_acquires.second > 0 &&
                            !expr_uses_var(op->body.as<Acquire>()->count, op->name)) {
                            after_acquires.first.accept(this);
                            result++;
                        } else {
                            IRVisitor::visit(op);
                        }
                    } else {
                        IRVisitor::visit(op);
                    }
                }

                // This is a "standalone" Acquire and will result in its own task.
                // Treat it requiring one more thread than its body.
                void visit(const Acquire *op) override {
                    result = 0;
                    auto after_inner_acquires = skip_acquires(op);
                    after_inner_acquires.first.accept(this);
                    result = result + 1;
                }

                void visit(const Block *op) override {
                    result = 0;
                    op->first.accept(this);
                    int result_first = result;
                    result = 0;
                    op->rest.accept(this);
                    result = std::max(result, result_first);
                }

            public:
                int result = 0;
            };
            MinThreads min_threads;
            t.body.accept(&min_threads);

#if 0
            // Decide if we're going to call do_par_for or
            // do_parallel_tasks. halide_do_par_for is simpler, but
            // assumes a bunch of things. Programs that don't use async
            // can also enter the task system via do_par_for.
            Value *task_parent = sym_get("__task_parent", false);
            bool use_do_par_for = (num_tasks == 1 &&
                                   min_threads.result == 0 &&
                                   t.semaphores.empty() &&
                                   !task_parent);

            // Make the array of semaphore acquisitions this task needs to do before it runs.
            Value *semaphores;
            Value *num_semaphores = ConstantInt::get(i32_t, (int)t.semaphores.size());
            if (!t.semaphores.empty()) {
                semaphores = create_alloca_at_entry(semaphore_acquire_t_type, (int)t.semaphores.size());
                for (int i = 0; i < (int)t.semaphores.size(); i++) {
                    Value *semaphore = codegen(t.semaphores[i].semaphore);
                    semaphore = builder->CreatePointerCast(semaphore, semaphore_t_type->getPointerTo());
                    Value *count = codegen(t.semaphores[i].count);
                    Value *slot_ptr = builder->CreateConstGEP2_32(semaphore_acquire_t_type, semaphores, i, 0);
                    builder->CreateStore(semaphore, slot_ptr);
                    slot_ptr = builder->CreateConstGEP2_32(semaphore_acquire_t_type, semaphores, i, 1);
                    builder->CreateStore(count, slot_ptr);
                }
            } else {
                semaphores = ConstantPointerNull::get(semaphore_acquire_t_type->getPointerTo());
            }

            FunctionType *fn_type = use_do_par_for ? task_t : loop_task_t;
            int closure_arg_idx = use_do_par_for ? 2 : 3;

            // Make a new function that does the body
            llvm::Function *containing_function = function;
            function = llvm::Function::Create(fn_type, llvm::Function::InternalLinkage,
                                              t.name, module.get());

            llvm::Value *task_ptr = builder->CreatePointerCast(function, fn_type->getPointerTo());

            function->addParamAttr(closure_arg_idx, Attribute::NoAlias);

            set_function_attributes_for_target(function, target);

            // Make the initial basic block and jump the builder into the new function
            IRBuilderBase::InsertPoint call_site = builder->saveIP();
            BasicBlock *block = BasicBlock::Create(*context, "entry", function);
            builder->SetInsertPoint(block);

            // Save the destructor block
            BasicBlock *parent_destructor_block = destructor_block;
            destructor_block = nullptr;

            // Make a new scope to use
            Scope<Value *> saved_symbol_table;
            symbol_table.swap(saved_symbol_table);

            // Get the function arguments

            // The user context is first argument of the function; it's
            // important that we override the name to be "__user_context",
            // since the LLVM function has a random auto-generated name for
            // this argument.
            llvm::Function::arg_iterator iter = function->arg_begin();
            sym_push("__user_context", iterator_to_pointer(iter));

            if (use_do_par_for) {
                // Next is the loop variable.
                ++iter;
                sym_push(t.loop_var, iterator_to_pointer(iter));
            } else if (!t.loop_var.empty()) {
                // We peeled off a loop. Wrap a new loop around the body
                // that just does the slice given by the arguments.
                std::string loop_min_name = unique_name('t');
                std::string loop_extent_name = unique_name('t');
                t.body = For::make(t.loop_var,
                                   Variable::make(Int(32), loop_min_name),
                                   Variable::make(Int(32), loop_extent_name),
                                   ForType::Serial,
                                   DeviceAPI::None,
                                   t.body);
                ++iter;
                sym_push(loop_min_name, iterator_to_pointer(iter));
                ++iter;
                sym_push(loop_extent_name, iterator_to_pointer(iter));
            } else {
                // This task is not any kind of loop, so skip these args.
                ++iter;
                ++iter;
            }

            // The closure pointer is either the last (for halide_do_par_for) or
            // second to last argument (for halide_do_parallel_tasks).
            ++iter;
            iter->setName("closure");
            Value *closure_handle = builder->CreatePointerCast(iterator_to_pointer(iter),
                                                               closure_t->getPointerTo());

            // Load everything from the closure into the new scope
            unpack_closure(closure, symbol_table, closure_t, closure_handle, builder);

            if (!use_do_par_for) {
                // For halide_do_parallel_tasks the threading runtime task parent
                // is the last argument.
                ++iter;
                iter->setName("task_parent");
                sym_push("__task_parent", iterator_to_pointer(iter));
            }

            // Generate the new function body
            codegen(t.body);

            // Return success
            return_with_error_code(ConstantInt::get(i32_t, 0));

            // Move the builder back to the main function.
            builder->restoreIP(call_site);

            // Now restore the scope
            symbol_table.swap(saved_symbol_table);
            function = containing_function;

            // Restore the destructor block
            destructor_block = parent_destructor_block;

            Value *min = codegen(t.min);
            Value *extent = codegen(t.extent);
            Value *serial = codegen(cast(UInt(8), t.serial));

            if (use_do_par_for) {
                llvm::Function *do_par_for = module->getFunction("halide_do_par_for");
                internal_assert(do_par_for) << "Could not find halide_do_par_for in initial module\n";
                do_par_for->addParamAttr(4, Attribute::NoAlias);
                Value *args[] = {get_user_context(), task_ptr, min, extent, closure_ptr};
                debug(4) << "Creating call to do_par_for\n";
                result = builder->CreateCall(do_par_for, args);
            } else {
                // Populate the task struct
                Value *slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 0);
                builder->CreateStore(task_ptr, slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 1);
                builder->CreateStore(closure_ptr, slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 2);
                builder->CreateStore(create_string_constant(t.name), slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 3);
                builder->CreateStore(semaphores, slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 4);
                builder->CreateStore(num_semaphores, slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 5);
                builder->CreateStore(min, slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 6);
                builder->CreateStore(extent, slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 7);
                builder->CreateStore(ConstantInt::get(i32_t, min_threads.result), slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 8);
                builder->CreateStore(serial, slot_ptr);
            }
        }

        Expr tasks_allocation = Allocate::    static Stmt make(const std::string &name, Type type, MemoryType memory_type,
                         const std::vector<Expr> &extents,
                         Expr condition, Stmt body,
                         Expr new_expr = Expr(), const std::string &free_function = std::string());

        if (!result) {
            llvm::Function *do_parallel_tasks = module->getFunction("halide_do_parallel_tasks");
            internal_assert(do_parallel_tasks) << "Could not find halide_do_parallel_tasks in initial module\n";
            do_parallel_tasks->addParamAttr(2, Attribute::NoAlias);
            Value *task_parent = sym_get("__task_parent", false);
            if (!task_parent) {
                task_parent = ConstantPointerNull::get(i8_t->getPointerTo());  // void*
            }
            Value *args[] = {get_user_context(),
                             ConstantInt::get(i32_t, num_tasks),
                             task_stack_ptr,
                             task_parent};
            result = builder->CreateCall(do_parallel_tasks, args);
        }

        // Check for success
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32_t, 0));
        create_assertion(did_succeed, Expr(), result);
        return result;
#else
        }
#endif

        return body;
    }

    void get_parallel_tasks(const Stmt &s, std::vector<ParallelTask> &result, std::pair<std::string, int> prefix) {
        const For *loop = s.as<For>();
        const Acquire *acquire = loop ? loop->body.as<Acquire>() : s.as<Acquire>();
        if (const Fork *f = s.as<Fork>()) {
            add_fork(prefix);
            get_parallel_tasks(f->first, result, prefix);
            get_parallel_tasks(f->rest, result, prefix);
        } else if (!loop && acquire) {
            const Variable *v = acquire->semaphore.as<Variable>();
            internal_assert(v);
            add_suffix(prefix, "." + v->name);
            ParallelTask t{s, {}, "", 0, 1, const_false(), task_debug_name(prefix)};
            while (acquire) {
                t.semaphores.push_back({acquire->semaphore, acquire->count});
                t.body = acquire->body;
                acquire = t.body.as<Acquire>();
            }
            result.push_back(t);
        } else if (loop && loop->for_type == ForType::Parallel) {
            add_suffix(prefix, ".par_for." + loop->name);
            result.push_back(ParallelTask{loop->body, {}, loop->name, loop->min, loop->extent, const_false(), task_debug_name(prefix)});
        } else if (loop &&
                   loop->for_type == ForType::Serial &&
                   acquire &&
                   !expr_uses_var(acquire->count, loop->name)) {
            const Variable *v = acquire->semaphore.as<Variable>();
            internal_assert(v);
            add_suffix(prefix, ".for." + v->name);
            ParallelTask t{loop->body, {}, loop->name, loop->min, loop->extent, const_true(), task_debug_name(prefix)};
            while (acquire) {
                t.semaphores.push_back({acquire->semaphore, acquire->count});
                t.body = acquire->body;
                acquire = t.body.as<Acquire>();
            }
            result.push_back(t);
        } else {
            add_suffix(prefix, "." + std::to_string(result.size()));
            result.push_back(ParallelTask{s, {}, "", 0, 1, const_false(), task_debug_name(prefix)});
        }
    }

    Stmt do_as_parallel_task(const Stmt &s) {
        std::vector<ParallelTask> tasks;
        get_parallel_tasks(s, tasks, {function_name, 0});
        return rewrite_parallel_tasks(tasks);
    }

    LowerParallelTasks(const std::string &name) : function_name(name) { }

    std::string function_name;
    std::vector<LoweredFunc> closure_implementations;
};

Stmt lower_parallel_tasks(Stmt s, std::vector<LoweredFunc> &closure_implementations, const std::string &name) {
    LowerParallelTasks lowering_mutator(name);
    Stmt result = lowering_mutator.mutate(s);
    closure_implementations = std::move(lowering_mutator.closure_implementations);
    return result;
}  

}  // namespace Internal
}  // namespace Halide
