#include "LowerParallelTasks.h"

#include <string>

#include "Argument.h"
#include "Closure.h"
#include "DebugArguments.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Module.h"
#include "Param.h"

namespace Halide {
namespace Internal {

Expr maybe_print(const std::string &label, Expr e) {
#if 0
    std::string n = unique_name('t');
    Expr v = Variable::make(e.type(), n);
    return Let::make(n, e, Call::make(e.type(), Internal::Call::return_second,
        {print(StringImm::make(label), v), v}, Internal::Call::PureIntrinsic));
#else
    return e;
#endif
}

// TODO(zalman): Find a better place for this code to live.
LoweredFunc GenerateClosureIR(const std::string &name, const Closure &closure,
                              std::vector<LoweredArgument> &args, int closure_arg_index,
                              const Stmt &body) {
  
  debug(0) << "Generating closure for " << name << ".\n";
    // Figure out if user_context has to be dealt with here.
    std::string closure_arg_name = unique_name("closure_arg");
    args[closure_arg_index] = LoweredArgument(closure_arg_name, Argument::Kind::InputScalar,
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
                                     maybe_print("Reading " + v.first + " ",
                                     Call::make(v.second, Call::load_struct_member,
                                                { closure_arg, struct_type, make_const(UInt(32), struct_index) },
                                                Call::Intrinsic)),
                                     wrapped_body);
        struct_index++;
    }
    for (const auto &b : closure.buffers) {
        type_args[struct_index + 1] = make_zero(type_of<void *>());
        type_args[struct_index + 2] = make_zero(type_of<halide_buffer_t *>());
        wrapped_body = LetStmt::make(b.first,
                                     maybe_print("Reading buffer arg 1 " + b.first + " ",
                                     Call::make(type_of<void *>(), Call::load_struct_member,
                                                { closure_arg, struct_type, make_const(UInt(32), struct_index) },
                                                Call::PureIntrinsic)),
                                     wrapped_body);
        wrapped_body = LetStmt::make(b.first + ".buffer",
                                     maybe_print("Reading buffer arg 2 " + b.first + ".buffer ",
                                     Call::make(type_of<halide_buffer_t *>(), Call::load_struct_member,
                                                { closure_arg, struct_type, make_const(UInt(32), struct_index + 1) },
                                                Call::Intrinsic)),
                                     wrapped_body);
        struct_index += 2;
    }

    Expr struct_type_decl = Call::make(Handle(), Call::make_struct_type, type_args, Call::PureIntrinsic);
    wrapped_body = Block::make(Evaluate::make(maybe_print("closure arg incoming.", closure_arg)), wrapped_body);
    wrapped_body = LetStmt::make(closure_type_name, struct_type_decl, wrapped_body);

    LoweredFunc result{name, args, wrapped_body, LinkageType::External, NameMangling::Default };
    //    debug_arguments(&result, Target("host-debug"));
    return result;
}

Expr AllocateClosure(const std::string &name, const Closure &closure) {
    std::vector<Expr> closure_elements;
    // TODO(zalman): Ensure this is unique within scopes it is used in.
    for (const auto &v : closure.vars) {
        closure_elements.push_back(maybe_print("Storing " + v.first + " ", Variable::make(v.second, v.first)));
    }
    for (const auto &b : closure.buffers) {
        // TODO(zalman): Verify types here...
        closure_elements.push_back(maybe_print("Storing buffer arg 1 " + b.first + " ", Variable::make(type_of<void *>(), b.first)));
        // TODO(zalman): this has to allow a failed lookup.
        closure_elements.push_back(maybe_print("Storing buffer arg 2 " + b.first + ".buffer ", Call::make(type_of<halide_buffer_t *>(), Call::get_pointer_symbol_or_null,
                                              { StringImm::make(b.first + ".buffer"),
                                                Call::make(Handle(), Call::make_struct_type, { StringImm::make("halide_buffer_t") }, Call::PureIntrinsic) },
                                                    Call::Intrinsic)));
    }    
    return maybe_print("Closure after allocation: ", Call::make(type_of<void *>(), Call::make_struct, closure_elements, Call::Intrinsic));
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
        return IRMutator::visit(op);
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
        // TODO(zalman): Should probably move this to HalideRuntime.h or come up
        // with a better way to do it, though this number is encoded in the code
        // below as well so...
        std::vector<Expr> tasks_array_args(2);
        tasks_array_args[0] = Call::make(type_of<halide_parallel_task_t *>(), Call::make_struct_type, { StringImm::make("halide_parallel_task_t") }, Call::PureIntrinsic);
        tasks_array_args[1] = num_tasks;


        std::string closure_name = unique_name("parallel_closure");
        Expr closure_struct_allocation = AllocateClosure(closure_name, closure);
        Expr closure_struct = Variable::make(Handle(), closure_name);
        
        Expr result;
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

            // TODO(zalman): Need to figure out how to make this choice in pure Halide IR.
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
#else
            bool use_parallel_for = false;
#endif

            Expr semaphore_type = Call::make(type_of<halide_semaphore_acquire_t *>(), Call::make_struct_type,
                                             { StringImm::make("halide_semaphore_acquire_t") }, Call::PureIntrinsic);
            std::string semaphores_array_name = unique_name("task_semaphores");
            Expr semaphores_array;
            if (!t.semaphores.empty()) {
                std::vector<Expr> semaphore_args(2 + t.semaphores.size() * 2);
                semaphore_args[0] = semaphore_type;
                semaphore_args[1] = (int)t.semaphores.size();
                for (int i = 0; i < (int)t.semaphores.size(); i++) {
                    semaphore_args[2 + i * 2] = t.semaphores[i].semaphore;
                    semaphore_args[2 + i * 2 + 1] = t.semaphores[i].count;
                }
                semaphores_array = Call::make(type_of<halide_semaphore_acquire_t *>(), Call::make_typed_struct, semaphore_args, Call::PureIntrinsic);
            }

            std::vector<LoweredArgument> closure_args(use_parallel_for ? 3 : 5);
            int closure_arg_index;
            closure_args[0] = LoweredArgument("__user_context", Argument::Kind::InputScalar,
                                              type_of<void *>(), 0, ArgumentEstimates());
            if (use_parallel_for) {
                closure_arg_index = 2;
                closure_args[1] = LoweredArgument(t.loop_var, Argument::Kind::InputScalar,
                                                  Int(32), 0, ArgumentEstimates());
            } else {
                closure_arg_index = 3;
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
                closure_args[1] = LoweredArgument(loop_min_name, Argument::Kind::InputScalar,
                                                  Int(32), 0, ArgumentEstimates());
                closure_args[2] = LoweredArgument(loop_extent_name, Argument::Kind::InputScalar,
                                                  Int(32), 0, ArgumentEstimates());
                closure_args[4] = LoweredArgument("__task_parent", Argument::Kind::InputScalar,
                                                  type_of<void *>(), 0, ArgumentEstimates());
            }

            {              
                ScopedValue<std::string> save_name(function_name, t.name);
                t.body = mutate(t.body);
            }

            closure_implementations.emplace_back(GenerateClosureIR(t.name, closure, closure_args, closure_arg_index, t.body));

            if (use_parallel_for) {
              debug(0) << "Declaring function name " << t.name << " (0).\n";
                std::vector<Expr> function_decl_args(5);
                function_decl_args[0] = t.name;
                function_decl_args[1] = make_zero(Int(32));
                function_decl_args[2] = make_zero(type_of<int8_t *>());
                function_decl_args[3] = make_zero(Int(32));
                function_decl_args[4] = make_zero(type_of<int8_t *>());

                std::vector<Expr> args(5);
                args[0] = Call::make(type_of<void *>(), Call::get_user_context, {}, Call::PureIntrinsic);
                args[1] = Call::make(Handle(), Call::resolve_function_name, function_decl_args, Call::PureIntrinsic);
                args[2] = t.min;
                args[3] = t.extent;
                args[4] = closure_struct;
                result = Call::make(Int(32), "halide_do_par_for", args, Call::Extern);
            } else {
              debug(0) << "Declaring function name " << t.name << " (1).\n";
                std::vector<Expr> function_decl_args(7);
                function_decl_args[0] = t.name;
                function_decl_args[1] = make_zero(Int(32));
                function_decl_args[2] = make_zero(type_of<int8_t *>());
                function_decl_args[3] = make_zero(Int(32));
                function_decl_args[4] = make_zero(Int(32));
                function_decl_args[5] = make_zero(type_of<int8_t *>());
                function_decl_args[6] = make_zero(type_of<int8_t *>());

                tasks_array_args.push_back(Call::make(Handle(), Call::resolve_function_name, function_decl_args, Call::PureIntrinsic));
                tasks_array_args.push_back(Cast::make(type_of<uint8_t *>(), closure_struct));
                tasks_array_args.push_back(StringImm::make(t.name));
                tasks_array_args.push_back(semaphores_array.defined() ? semaphores_array : semaphore_type);
                tasks_array_args.push_back((int)t.semaphores.size());
                tasks_array_args.push_back(t.min);
                tasks_array_args.push_back(t.extent);
                tasks_array_args.push_back(min_threads.result);
                tasks_array_args.push_back(Cast::make(UInt(8), t.serial));
            }
        }

        if (tasks_array_args.size() > 2) {
            // Allocate task list array
            Expr tasks_list = Call::make(Handle(), Call::make_typed_struct, tasks_array_args, Call::PureIntrinsic);
            result = Call::make(Int(32), "halide_do_parallel_tasks", { Call::make(type_of<void *>(), Call::get_user_context, {}, Call::PureIntrinsic),
                                                                      make_const(Int(32), num_tasks), tasks_list,
                                                                      Call::make(Handle(), Call::get_pointer_symbol_or_null,
                                                                                 { StringImm::make("_task_parent"), make_zero(Handle()) }, Call::Intrinsic) }, Call::Extern);
        }

        result = Let::make(closure_name, closure_struct_allocation, result);
        return AssertStmt:: make(result == 0, result);
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
#if 0
    for (const auto &lf : lowering_mutator.closure_implementations) {
        debug(0) << "Lowered function " << lf.name << ":\n" << lf.body << "\n\n";
    }
#endif
    closure_implementations = std::move(lowering_mutator.closure_implementations);
#if 0
    debug(0) << "Main body:\n" << result << "\n\n";
#endif
    return result;
}  

}  // namespace Internal
}  // namespace Halide

