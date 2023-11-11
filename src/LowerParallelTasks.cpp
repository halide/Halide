#include "LowerParallelTasks.h"

#include <string>

#include "Argument.h"
#include "Closure.h"
#include "DebugArguments.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LoopPartitioningDirective.h"
#include "Module.h"
#include "Param.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

namespace {

LoweredArgument make_scalar_arg(const std::string &name, const Type &type) {
    return LoweredArgument(name, Argument::Kind::InputScalar, type, 0, ArgumentEstimates());
}

template<typename T>
LoweredArgument make_scalar_arg(const std::string &name) {
    return make_scalar_arg(name, type_of<T>());
}

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

int calculate_min_threads(const Stmt &body) {
    MinThreads min_threads;
    body.accept(&min_threads);
    return min_threads.result;
}

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
        Partition partition_policy;
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
            closure.include(s);
        }

        // The same name can appear as a var and a buffer. Remove the var name in this case.
        for (auto const &b : closure.buffers) {
            closure.vars.erase(b.first);
        }

        int num_tasks = (int)(tasks.size());
        std::vector<Expr> tasks_array_args;
        tasks_array_args.reserve(num_tasks * 9);

        std::string closure_name = unique_name("parallel_closure");
        Expr closure_struct_allocation = closure.pack_into_struct();
        Expr closure_struct = Variable::make(Handle(), closure_name);

        const bool has_task_parent = !task_parents.empty() && task_parents.top_ref().defined();

        Expr result;
        for (int i = 0; i < num_tasks; i++) {
            ParallelTask t = tasks[i];

            const int min_threads = calculate_min_threads(t.body);

            // Decide if we're going to call do_par_for or
            // do_parallel_tasks. halide_do_par_for is simpler, but
            // assumes a bunch of things. Programs that don't use async
            // can also enter the task system via do_par_for.
            const bool use_parallel_for = (num_tasks == 1 &&
                                           min_threads == 0 &&
                                           t.semaphores.empty() &&
                                           !has_task_parent);

            Expr closure_task_parent;

            const std::string closure_arg_name = unique_name("closure_arg");
            auto closure_arg = make_scalar_arg<uint8_t *>(closure_arg_name);

            Type closure_function_type;

            std::vector<LoweredArgument> closure_args(use_parallel_for ? 3 : 5);
            closure_args[0] = make_scalar_arg<void *>("__user_context");
            if (use_parallel_for) {
                // The closure will be a halide_task_t, with arguments like:
                //
                //   typedef int (*halide_task_t)(void *user_context, int task_number, uint8_t *closure);
                //
                closure_function_type = type_of<halide_task_t>();

                closure_args[1] = make_scalar_arg<int32_t>(t.loop_var);
                closure_args[2] = closure_arg;
                // closure_task_parent remains undefined here.
            } else {
                // The closure will be a halide_loop_task_t, with arguments like:
                //
                //   typedef int (*halide_loop_task_t)(void *user_context, int min, int extent, uint8_t *closure, void *task_parent);
                //
                closure_function_type = type_of<halide_loop_task_t>();

                const std::string closure_task_parent_name = unique_name("__task_parent");
                closure_task_parent = Variable::make(type_of<void *>(), closure_task_parent_name);
                // We peeled off a loop. Wrap a new loop around the body
                // that just does the slice given by the arguments.
                std::string loop_min_name = unique_name('t');
                std::string loop_extent_name = unique_name('t');
                if (!t.loop_var.empty()) {
                    t.body = For::make(t.loop_var,
                                       Variable::make(Int(32), loop_min_name),
                                       Variable::make(Int(32), loop_extent_name),
                                       ForType::Serial,
                                       t.partition_policy,
                                       DeviceAPI::None,
                                       t.body);
                } else {
                    internal_assert(is_const_one(t.extent));
                }
                closure_args[1] = make_scalar_arg<int32_t>(loop_min_name);
                closure_args[2] = make_scalar_arg<int32_t>(loop_extent_name);
                closure_args[3] = closure_arg;
                closure_args[4] = make_scalar_arg<void *>(closure_task_parent_name);
            }

            {
                ScopedValue<std::string> save_name(function_name, t.name);

                task_parents.push(closure_task_parent);
                t.body = mutate(t.body);
                task_parents.pop();
            }

            const std::string new_function_name = c_print_name(unique_name(t.name), false);
            {
                Expr closure_arg_var = Variable::make(closure_struct_allocation.type(), closure_arg_name);
                Stmt wrapped_body = closure.unpack_from_struct(closure_arg_var, t.body);

                // TODO(zvookin): Figure out how we want to handle name mangling of closures.
                // For now, the C++ backend makes them extern "C" so they have to be NameMangling::C.
                LoweredFunc closure_func{new_function_name, closure_args, std::move(wrapped_body), LinkageType::Internal, NameMangling::C};
                if (target.has_feature(Target::Debug)) {
                    debug_arguments(&closure_func, target);
                }
                closure_implementations.emplace_back(std::move(closure_func));
            }

            // Codegen will add user_context for us
            // Prefix the function name with "::" as we would in C++ to make
            // it clear we're talking about something in global scope in
            // case some joker names an intermediate Func or Var the same
            // name as the pipeline. This prefix works transparently in the
            // C++ backend.
            Expr new_function_name_arg = Variable::make(closure_function_type, "::" + new_function_name);
            Expr closure_struct_arg = Cast::make(type_of<uint8_t *>(), closure_struct);

            if (use_parallel_for) {
                std::vector<Expr> args = {
                    std::move(new_function_name_arg),
                    t.min,
                    t.extent,
                    std::move(closure_struct_arg)};
                result = Call::make(Int(32), "halide_do_par_for", args, Call::Extern);
            } else {
                const int semaphores_size = (int)t.semaphores.size();
                std::vector<Expr> semaphore_args(semaphores_size * 2);
                for (int i = 0; i < semaphores_size; i++) {
                    semaphore_args[i * 2] = t.semaphores[i].semaphore;
                    semaphore_args[i * 2 + 1] = t.semaphores[i].count;
                }
                Expr semaphores_array = Call::make(type_of<halide_semaphore_acquire_t *>(), Call::make_struct, semaphore_args, Call::PureIntrinsic);

                tasks_array_args.emplace_back(std::move(new_function_name_arg));
                tasks_array_args.emplace_back(std::move(closure_struct_arg));
                tasks_array_args.emplace_back(StringImm::make(t.name));
                tasks_array_args.emplace_back(std::move(semaphores_array));
                tasks_array_args.emplace_back((int)t.semaphores.size());
                tasks_array_args.emplace_back(t.min);
                tasks_array_args.emplace_back(t.extent);
                tasks_array_args.emplace_back(min_threads);
                tasks_array_args.emplace_back(Cast::make(Bool(), t.serial));
            }
        }

        if (!tasks_array_args.empty()) {
            // Allocate task list array
            Expr tasks_list = Call::make(type_of<halide_parallel_task_t *>(), Call::make_struct, tasks_array_args, Call::PureIntrinsic);
            Expr user_context = Call::make(type_of<void *>(), Call::get_user_context, {}, Call::PureIntrinsic);
            Expr task_parent = has_task_parent ? task_parents.top() : make_zero(Handle());
            result = Call::make(Int(32), "halide_do_parallel_tasks",
                                {user_context, make_const(Int(32), num_tasks), tasks_list, task_parent},
                                Call::Extern);
        }

        std::string closure_result_name = unique_name("closure_result");
        Expr closure_result = Variable::make(Int(32), closure_result_name);
        Stmt stmt = AssertStmt::make(closure_result == 0, closure_result);
        stmt = LetStmt::make(closure_result_name, result, stmt);
        stmt = LetStmt::make(closure_name, closure_struct_allocation, stmt);
        return stmt;
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
            ParallelTask t{s, {}, "", 0, 1, const_false(), task_debug_name(prefix), Partition::Never};
            while (acquire) {
                t.semaphores.push_back({acquire->semaphore, acquire->count});
                t.body = acquire->body;
                acquire = t.body.as<Acquire>();
            }
            result.emplace_back(std::move(t));
        } else if (loop && loop->for_type == ForType::Parallel) {
            add_suffix(prefix, ".par_for." + loop->name);
            ParallelTask t{loop->body, {}, loop->name, loop->min, loop->extent, const_false(), task_debug_name(prefix), loop->partition_policy};
            result.emplace_back(std::move(t));
        } else if (loop &&
                   loop->for_type == ForType::Serial &&
                   acquire &&
                   !expr_uses_var(acquire->count, loop->name)) {
            const Variable *v = acquire->semaphore.as<Variable>();
            internal_assert(v);
            add_suffix(prefix, ".for." + v->name);
            ParallelTask t{loop->body, {}, loop->name, loop->min, loop->extent, const_true(), task_debug_name(prefix), loop->partition_policy};
            while (acquire) {
                t.semaphores.push_back({acquire->semaphore, acquire->count});
                t.body = acquire->body;
                acquire = t.body.as<Acquire>();
            }
            result.emplace_back(std::move(t));
        } else {
            add_suffix(prefix, "." + std::to_string(result.size()));
            ParallelTask t{s, {}, "", 0, 1, const_false(), task_debug_name(prefix), Partition::Never};
            result.emplace_back(std::move(t));
        }
    }

    Stmt do_as_parallel_task(const Stmt &s) {
        std::vector<ParallelTask> tasks;
        get_parallel_tasks(s, tasks, {function_name, 0});
        return rewrite_parallel_tasks(tasks);
    }

    LowerParallelTasks(const std::string &name, const Target &t)
        : function_name(name), target(t) {
    }

    std::string function_name;
    const Target &target;
    std::vector<LoweredFunc> closure_implementations;
    SmallStack<Expr> task_parents;
};

}  // namespace

Stmt lower_parallel_tasks(const Stmt &s, std::vector<LoweredFunc> &closure_implementations,
                          const std::string &name, const Target &t) {
    LowerParallelTasks lowering_mutator(name, t);
    Stmt result = lowering_mutator.mutate(s);

    // Main body will be dumped as part of standard lowering debugging, but closures will not be.
    if (debug::debug_level() >= 2) {
        for (const auto &lf : lowering_mutator.closure_implementations) {
            debug(2) << "lower_parallel_tasks generated closure lowered function " << lf.name << ":\n"
                     << lf.body << "\n\n";
        }
    }

    // Append to the end rather than replacing the list entirely.
    closure_implementations.insert(closure_implementations.end(),
                                   lowering_mutator.closure_implementations.begin(),
                                   lowering_mutator.closure_implementations.end());

    return result;
}

}  // namespace Internal
}  // namespace Halide
