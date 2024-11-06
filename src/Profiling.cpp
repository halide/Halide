#include <algorithm>
#include <map>
#include <string>

#include "CodeGen_Internal.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "InjectHostDevBufferCopies.h"
#include "Profiling.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include "UniquifyVariableNames.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {

// All names that need to be unique, just in case someone does something
// perverse like naming a func "profiler_instance".
struct Names {
    const std::string &pipeline_name;
    std::string profiler_instance;
    std::string profiler_local_sampling_token;
    std::string profiler_shared_sampling_token;
    std::string hvx_profiler_instance;
    std::string profiler_func_names;
    std::string profiler_func_stack_peak_buf;
    std::string profiler_start_error_code;

    Names(const std::string &pipeline_name)
        : pipeline_name(pipeline_name),
          profiler_instance(unique_name("profiler_instance")),
          profiler_local_sampling_token(unique_name("profiler_local_sampling_token")),
          profiler_shared_sampling_token(unique_name("profiler_shared_sampling_token")),
          hvx_profiler_instance(unique_name("hvx_profiler_instance")),
          profiler_func_names(unique_name("profiler_func_names")),
          profiler_func_stack_peak_buf(unique_name("profiler_func_stack_peak_buf")),
          profiler_start_error_code(unique_name("profiler_start_error_code")) {
    }
};

Stmt incr_active_threads(const Expr &profiler_instance) {
    return Evaluate::make(Call::make(Int(32), "halide_profiler_incr_active_threads",
                                     {profiler_instance}, Call::Extern));
}

Stmt decr_active_threads(const Expr &profiler_instance) {
    return Evaluate::make(Call::make(Int(32), "halide_profiler_decr_active_threads",
                                     {profiler_instance}, Call::Extern));
}

Stmt acquire_sampling_token(const Expr &shared_token, const Expr &local_token) {
    return Evaluate::make(Call::make(Int(32), "halide_profiler_acquire_sampling_token",
                                     {shared_token, local_token}, Call::Extern));
}

Stmt release_sampling_token(const Expr &shared_token, const Expr &local_token) {
    return Evaluate::make(Call::make(Int(32), "halide_profiler_release_sampling_token",
                                     {shared_token, local_token}, Call::Extern));
}

Stmt claim_sampling_token(const Stmt &s, const Expr &shared_token, const Expr &local_token) {
    return LetStmt::make(local_token.as<Variable>()->name,
                         Call::make(Handle(), Call::alloca, {Int(32).bytes()}, Call::Intrinsic),
                         Block::make({acquire_sampling_token(shared_token, local_token),
                                      s,
                                      release_sampling_token(shared_token, local_token)}));
}

class InjectProfiling : public IRMutator {

public:
    map<string, int> indices;  // maps from func name -> index in buffer.

    vector<int> stack;  // What produce nodes are we currently inside of.

    const Names &names;
    const map<string, Function> &env;

    bool in_fork = false;
    bool in_parallel = false;
    bool in_leaf_task = false;

    InjectProfiling(const Names &names, const map<std::string, Function> &env)
        : names(names), env(env) {
        stack.push_back(get_func_id("overhead"));
        // ID 0 is treated specially in the runtime as overhead
        internal_assert(stack.back() == 0);

        waiting_on_tasks_id = get_func_id("waiting for parallel tasks to finish");
        malloc_id = get_func_id("halide_malloc");
        free_id = get_func_id("halide_free");

        profiler_instance = Variable::make(Handle(), names.profiler_instance);
        profiler_local_sampling_token = Variable::make(Handle(), names.profiler_local_sampling_token);
        profiler_shared_sampling_token = Variable::make(Handle(), names.profiler_shared_sampling_token);
    }

    map<int, uint64_t> func_stack_current;  // map from func id -> current stack allocation
    map<int, uint64_t> func_stack_peak;     // map from func id -> peak stack allocation

    Stmt activate_thread(const Stmt &s) {
        return activate_thread_helper(s, waiting_on_tasks_id);
    }

    Stmt activate_main_thread(const Stmt &s) {
        // The same as a child task, except when we finish (but before the
        // instances get popped), bill anything as overhead.
        return activate_thread_helper(s, 0);
    }

    Stmt activate_thread_helper(const Stmt &s, int final_id) {
        return Block::make({incr_active_threads(profiler_instance),
                            unconditionally_set_current_func(stack.back()),
                            s,
                            decr_active_threads(profiler_instance),
                            unconditionally_set_current_func(final_id)});
    }

    Stmt suspend_thread(const Stmt &s) {
        return Block::make({decr_active_threads(profiler_instance),
                            unconditionally_set_current_func(waiting_on_tasks_id),
                            s,
                            incr_active_threads(profiler_instance),
                            unconditionally_set_current_func(stack.back())});
    }

    Stmt suspend_thread_but_keep_task_id(const Stmt &s) {
        return Block::make({decr_active_threads(profiler_instance),
                            s,
                            incr_active_threads(profiler_instance)});
    }

private:
    using IRMutator::visit;

    int malloc_id, free_id, waiting_on_tasks_id;
    Expr profiler_instance;
    Expr profiler_local_sampling_token;
    Expr profiler_shared_sampling_token;

    // May need to be set to -1 at the start of control flow blocks
    // that have multiple incoming edges, if all sources don't have
    // the same most_recently_set_func.
    int most_recently_set_func = -1;

    struct AllocSize {
        bool on_stack;
        Expr size;
    };

    Scope<AllocSize> func_alloc_sizes;

    bool profiling_memory = true;

    // Strip down the tuple name, e.g. f.0 into f
    string normalize_name(const string &name) const {
        size_t idx = name.find('.');
        if (idx != std::string::npos) {
            internal_assert(idx != 0);
            return name.substr(0, idx);
        } else {
            return name;
        }
    }

    Function lookup_function(const string &name) const {
        auto it = env.find(name);
        if (it != env.end()) {
            return it->second;
        }
        string norm_name = normalize_name(name);
        it = env.find(norm_name);
        if (it != env.end()) {
            return it->second;
        }
        internal_error << "No function in the environment found for name '" << name << "'.\n";
        return {};
    }

    int get_func_id(const string &name) {
        string norm_name = normalize_name(name);
        int idx = -1;
        map<string, int>::iterator iter = indices.find(norm_name);
        if (iter == indices.end()) {
            idx = (int)indices.size();
            indices[norm_name] = idx;
        } else {
            idx = iter->second;
        }
        return idx;
    }

    Stmt unconditionally_set_current_func(int id) {
        Stmt s = Evaluate::make(Call::make(Int(32), "halide_profiler_set_current_func",
                                           {profiler_instance, id, reinterpret(Handle(), cast<uint64_t>(0))}, Call::Extern));
        return s;
    }

    Stmt set_current_func(int id) {
        if (most_recently_set_func == id) {
            return Evaluate::make(0);
        }
        most_recently_set_func = id;
        Expr last_arg = in_leaf_task ? profiler_local_sampling_token : reinterpret(Handle(), cast<uint64_t>(0));
        // This call gets inlined and becomes a single store instruction.
        Stmt s = Evaluate::make(Call::make(Int(32), "halide_profiler_set_current_func",
                                           {profiler_instance, id, last_arg}, Call::Extern));

        return s;
    }

    Expr compute_allocation_size(const vector<Expr> &extents,
                                 const Expr &condition,
                                 const Type &type,
                                 const std::string &name,
                                 bool &can_fit_on_stack) {
        can_fit_on_stack = true;

        Expr cond = simplify(condition);
        if (is_const_zero(cond)) {  // Condition always false
            return make_zero(UInt(64));
        }

        int64_t constant_size = Allocate::constant_allocation_size(extents, name);
        if (constant_size > 0) {
            int64_t stack_bytes = constant_size * type.bytes();
            if (can_allocation_fit_on_stack(stack_bytes)) {  // Allocation on stack
                return make_const(UInt(64), stack_bytes);
            }
        }

        // Check that the allocation is not scalar (if it were scalar
        // it would have constant size).
        internal_assert(!extents.empty());

        can_fit_on_stack = false;
        Expr size = cast<uint64_t>(extents[0]);
        for (size_t i = 1; i < extents.size(); i++) {
            size *= extents[i];
        }
        size = simplify(Select::make(condition, size * type.bytes(), make_zero(UInt(64))));
        return size;
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::profiling_enable_instance_marker)) {
            // We're out of the bounds query code. This instance should be
            // tracked (including any samples taken before this point.
            return Call::make(Int(32), "halide_profiler_enable_instance",
                              {profiler_instance}, Call::Extern);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {

        auto [new_extents, changed] = mutate_with_changes(op->extents);
        Expr condition = mutate(op->condition);

        bool can_fit_on_stack;
        Expr size = compute_allocation_size(new_extents, condition, op->type, op->name, can_fit_on_stack);
        internal_assert(size.type() == UInt(64));

        bool on_stack = can_fit_on_stack && !op->new_expr.defined();

        func_alloc_sizes.push(op->name, {on_stack, size});

        // compute_allocation_size() might return a zero size, if the allocation is
        // always conditionally false. remove_dead_allocations() is called after
        // inject_profiling() so this is a possible scenario.
        if (!is_const_zero(size) && on_stack) {
            int idx;
            Function func = lookup_function(op->name);
            if (func.should_not_profile()) {
                idx = stack.back();  // Attribute the stack size contribution to the deepest _profiled_ func.
            } else {
                idx = get_func_id(op->name);
            }
            const uint64_t *int_size = as_const_uint(size);
            internal_assert(int_size != nullptr);  // Stack size is always a const int
            func_stack_current[idx] += *int_size;
            func_stack_peak[idx] = std::max(func_stack_peak[idx], func_stack_current[idx]);
            debug(3) << "  Allocation on stack: " << op->name
                     << "(" << size << ") in pipeline " << names.pipeline_name
                     << "; current: " << func_stack_current[idx]
                     << "; peak: " << func_stack_peak[idx] << "\n";
        }

        vector<Stmt> tasks;
        bool track_heap_allocation = !is_const_zero(size) && !on_stack && profiling_memory;
        if (track_heap_allocation) {
            int idx = get_func_id(op->name);
            debug(3) << "  Allocation on heap: " << op->name
                     << "(" << size << ") in pipeline "
                     << names.pipeline_name << "\n";

            tasks.push_back(set_current_func(malloc_id));
            tasks.push_back(Evaluate::make(Call::make(Int(32), "halide_profiler_memory_allocate",
                                                      {profiler_instance, idx, size}, Call::Extern)));
        }

        Stmt body = mutate(op->body);

        Expr new_expr;
        Stmt stmt;
        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
        }
        if (!changed &&
            body.same_as(op->body) &&
            condition.same_as(op->condition) &&
            new_expr.same_as(op->new_expr)) {
            stmt = op;
        } else {
            stmt = Allocate::make(op->name, op->type, op->memory_type,
                                  new_extents, condition, body, new_expr,
                                  op->free_function, op->padding);
        }

        tasks.push_back(stmt);

        return Block::make(tasks);
    }

    Stmt visit(const Free *op) override {
        AllocSize alloc = func_alloc_sizes.get(op->name);
        internal_assert(alloc.size.type() == UInt(64));
        func_alloc_sizes.pop(op->name);

        Stmt stmt = IRMutator::visit(op);

        if (!is_const_zero(alloc.size)) {
            if (!alloc.on_stack) {
                if (profiling_memory) {
                    int idx = get_func_id(op->name);
                    debug(3) << "  Free on heap: " << op->name << "(" << alloc.size << ") in pipeline " << names.pipeline_name << "\n";

                    vector<Stmt> tasks{
                        set_current_func(free_id),
                        Evaluate::make(Call::make(Int(32), "halide_profiler_memory_free",
                                                  {profiler_instance, idx, alloc.size}, Call::Extern)),
                        stmt,
                        set_current_func(stack.back())};

                    stmt = Block::make(tasks);
                }
            } else {
                const uint64_t *int_size = as_const_uint(alloc.size);
                internal_assert(int_size != nullptr);

                int idx;
                Function func = lookup_function(op->name);
                if (func.should_not_profile()) {
                    idx = stack.back();  // Attribute the stack size contribution to the deepest _profiled_ func.
                } else {
                    idx = get_func_id(op->name);
                }
                func_stack_current[idx] -= *int_size;
                debug(3) << "  Free on stack: " << op->name
                         << "(" << alloc.size << ") in pipeline " << names.pipeline_name
                         << "; current: " << func_stack_current[idx]
                         << "; peak: " << func_stack_peak[idx] << "\n";
            }
        }
        return stmt;
    }

    Stmt visit(const ProducerConsumer *op) override {
        int idx;
        Stmt body;
        if (op->is_producer) {
            Function func = lookup_function(op->name);
            if (func.should_not_profile()) {
                body = mutate(op->body);
                if (body.same_as(op->body)) {
                    return op;
                }
            } else {
                idx = get_func_id(op->name);
                stack.push_back(idx);
                Stmt set_current = set_current_func(idx);
                body = Block::make(set_current, mutate(op->body));
                stack.pop_back();
            }
        } else {
            // At the beginning of the consume step, set the current task
            // back to the outer one.
            Stmt set_current = set_current_func(stack.back());
            body = Block::make(set_current, mutate(op->body));
        }

        return ProducerConsumer::make(op->name, op->is_producer, body);
    }

    Stmt visit_parallel_task(Stmt s) {
        int old = most_recently_set_func;
        if (const Fork *f = s.as<Fork>()) {
            s = Fork::make(visit_parallel_task(f->first), visit_parallel_task(f->rest));
        } else if (const Acquire *a = s.as<Acquire>()) {
            s = Acquire::make(a->semaphore, a->count, visit_parallel_task(a->body));
        } else {
            s = activate_thread(mutate(s));
        }
        if (most_recently_set_func != old) {
            most_recently_set_func = -1;
        }
        return s;
    }

    Stmt visit(const Acquire *op) override {
        Stmt s = visit_parallel_task(op);
        return suspend_thread(s);
    }

    Stmt visit(const Fork *op) override {
        ScopedValue<bool> bind(in_fork, true);
        Stmt s = visit_parallel_task(op);
        return suspend_thread(s);
    }

    Stmt visit(const For *op) override {
        Stmt body = op->body;

        // The for loop indicates a device transition or a
        // parallel job launch. Decrement the number of active
        // threads outside the loop, and increment it inside the
        // body.
        bool update_active_threads = (op->device_api == DeviceAPI::Hexagon ||
                                      op->is_unordered_parallel());

        ScopedValue<bool> bind_in_parallel(in_parallel, in_parallel || op->is_unordered_parallel());

        bool leaf_task = false;
        if (update_active_threads) {

            class ContainsParallelOrBlockingNode : public IRVisitor {
                using IRVisitor::visit;
                void visit(const For *op) override {
                    result |= (op->is_unordered_parallel() ||
                               op->device_api != DeviceAPI::None);
                    IRVisitor::visit(op);
                }
                void visit(const Fork *op) override {
                    result = true;
                }
                void visit(const Acquire *op) override {
                    result = true;
                }

            public:
                bool result = false;
            } contains_parallel_or_blocking_node;

            body.accept(&contains_parallel_or_blocking_node);
            leaf_task = !contains_parallel_or_blocking_node.result;

            if (leaf_task) {
                body = claim_sampling_token(body, profiler_shared_sampling_token, profiler_local_sampling_token);
            }

            body = activate_thread(body);
        }
        ScopedValue<bool> bind_leaf_task(in_leaf_task, in_leaf_task || leaf_task);

        int old = most_recently_set_func;

        // We profile by storing a token to global memory, so don't enter GPU loops
        if (op->device_api == DeviceAPI::Hexagon) {
            // TODO: This is for all offload targets that support
            // limited internal profiling, which is currently just
            // hexagon. We don't support per-func stats remotely,
            // which means we can't do memory accounting.
            bool old_profiling_memory = profiling_memory;
            profiling_memory = false;
            body = mutate(body);
            profiling_memory = old_profiling_memory;

            // Get the profiler state pointer from scratch inside the
            // kernel. There will be a separate copy of the state on
            // the DSP that the host side will periodically query.
            Expr get_state = Call::make(Handle(), "halide_hexagon_remote_profiler_get_global_instance", {}, Call::Extern);
            body = substitute(names.profiler_instance, Variable::make(Handle(), names.hvx_profiler_instance), body);
            body = LetStmt::make(names.hvx_profiler_instance, get_state, body);
        } else if (op->device_api == DeviceAPI::None ||
                   op->device_api == DeviceAPI::Host) {
            body = mutate(body);
        } else {
            body = op->body;
        }

        if (old != most_recently_set_func) {
            most_recently_set_func = -1;
        }

        Stmt stmt = For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api, body);

        if (update_active_threads) {
            if (Internal::is_gpu(op->for_type)) {
                stmt = suspend_thread_but_keep_task_id(stmt);
            } else {
                stmt = suspend_thread(stmt);
            }
        }

        return stmt;
    }

    Stmt visit(const IfThenElse *op) override {
        int old = most_recently_set_func;
        Expr condition = mutate(op->condition);
        Stmt then_case = mutate(op->then_case);
        int func_computed_in_then = most_recently_set_func;
        most_recently_set_func = old;
        Stmt else_case = mutate(op->else_case);
        if (most_recently_set_func != func_computed_in_then) {
            most_recently_set_func = -1;
        }
        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        }
        return IfThenElse::make(std::move(condition), std::move(then_case), std::move(else_case));
    }

    Stmt visit(const LetStmt *op) override {
        if (const Call *call = op->value.as<Call>()) {
            Stmt start_profiler;
            if (call->name == "halide_copy_to_host" || call->name == "halide_copy_to_device") {
                std::string buffer_name;
                if (const Variable *var = call->args.front().as<Variable>()) {
                    buffer_name = var->name;
                    if (ends_with(buffer_name, ".buffer")) {
                        buffer_name = buffer_name.substr(0, buffer_name.size() - 7);
                    } else {
                        internal_error << "Expected to find a variable ending in .buffer as first argument to function call " << call->name << "\n";
                    }
                } else {
                    internal_error << "Expected to find a variable as first argument of the function call " << call->name << ".\n";
                }
                bool requires_sync = false;
                if (call->name == "halide_copy_to_host") {
                    int copy_to_host_id = get_func_id(buffer_name + " (copy to host)");
                    start_profiler = set_current_func(copy_to_host_id);
                    requires_sync = false;
                } else if (call->name == "halide_copy_to_device") {
                    int copy_to_device_id = get_func_id(buffer_name + " (copy to device)");
                    start_profiler = set_current_func(copy_to_device_id);
                    requires_sync = true;
                } else {
                    internal_error << "Unexpected function name.\n";
                }
                if (start_profiler.defined()) {
                    // The copy functions are followed by an assert, which we will wrap in the timed body.
                    const AssertStmt *copy_assert = nullptr;
                    Stmt other;
                    if (const Block *block = op->body.as<Block>()) {
                        if (const AssertStmt *assert = block->first.as<AssertStmt>()) {
                            copy_assert = assert;
                            other = block->rest;
                        }
                    } else if (const AssertStmt *assert = op->body.as<AssertStmt>()) {
                        copy_assert = assert;
                    }
                    if (copy_assert) {
                        std::vector<Stmt> steps;
                        steps.push_back(AssertStmt::make(copy_assert->condition, copy_assert->message));
                        if (requires_sync) {
                            internal_assert(call->name == "halide_copy_to_device");
                            Expr device_interface = call->args.back();  // The last argument to the copy_to_device calls is the device_interface.
                            Stmt sync_and_assert = call_extern_and_assert("halide_device_sync_global", {device_interface});
                            steps.push_back(sync_and_assert);
                        }
                        steps.push_back(set_current_func(stack.back()));

                        if (other.defined()) {
                            steps.push_back(mutate(other));
                        }
                        return Block::make(start_profiler,
                                           LetStmt::make(op->name, mutate(op->value),
                                                         Block::make(steps)));
                    } else {
                        internal_error << "No assert found after buffer copy.\n";
                    }
                }
            }
        }

        Stmt body = mutate(op->body);
        Expr value = mutate(op->value);
        if (body.same_as(op->body) && value.same_as(op->value)) {
            return op;
        }
        return LetStmt::make(op->name, value, body);
    }
};

}  // namespace

Stmt inject_profiling(const Stmt &stmt, const string &pipeline_name, const std::map<string, Function> &env) {
    Names names(pipeline_name);

    InjectProfiling profiling(names, env);
    Stmt s = profiling.mutate(stmt);

    int num_funcs = (int)(profiling.indices.size());

    // TODO: unique_name all these strings

    Expr instance = Variable::make(Handle(), names.profiler_instance);

    Expr func_names_buf = Variable::make(Handle(), names.profiler_func_names);

    Expr start_profiler = Call::make(Int(32), "halide_profiler_instance_start",
                                     {pipeline_name, num_funcs, func_names_buf, instance}, Call::Extern);

    Expr profiler_start_error_code = Variable::make(Int(32), names.profiler_start_error_code);

    Expr stop_profiler = Call::make(Handle(), Call::register_destructor,
                                    {Expr("halide_profiler_instance_end"), instance}, Call::Intrinsic);

    bool no_stack_alloc = profiling.func_stack_peak.empty();
    if (!no_stack_alloc) {
        Expr func_stack_peak_buf = Variable::make(Handle(), names.profiler_func_stack_peak_buf);

        Stmt update_stack = Evaluate::make(Call::make(Int(32), "halide_profiler_stack_peak_update",
                                                      {instance, func_stack_peak_buf}, Call::Extern));
        s = Block::make(update_stack, s);
    }

    s = profiling.activate_main_thread(s);

    // Initialize the shared sampling token
    Expr shared_sampling_token_var = Variable::make(Handle(), names.profiler_shared_sampling_token);
    Expr init_sampling_token =
        Call::make(Int(32), "halide_profiler_init_sampling_token", {shared_sampling_token_var, 0}, Call::Extern);
    s = Block::make({Evaluate::make(init_sampling_token), s});
    s = LetStmt::make(names.profiler_shared_sampling_token,
                      Call::make(Handle(), Call::alloca, {Int(32).bytes()}, Call::Intrinsic), s);

    // If there was a problem starting the profiler, it will call an
    // appropriate halide error function and then return the
    // (negative) error code as the token.
    s = Block::make(AssertStmt::make(profiler_start_error_code == 0, profiler_start_error_code), s);
    s = LetStmt::make(names.profiler_start_error_code, start_profiler, s);

    if (!no_stack_alloc) {
        for (int i = num_funcs - 1; i >= 0; --i) {
            s = Block::make(Store::make(names.profiler_func_stack_peak_buf,
                                        make_const(UInt(64), profiling.func_stack_peak[i]),
                                        i, Parameter(), const_true(), ModulusRemainder()),
                            s);
        }
        s = Block::make(s, Free::make(names.profiler_func_stack_peak_buf));
        s = Allocate::make(names.profiler_func_stack_peak_buf, UInt(64),
                           MemoryType::Auto, {num_funcs}, const_true(), s);
    }

    for (const auto &p : profiling.indices) {
        s = Block::make(Store::make(names.profiler_func_names, p.first, p.second, Parameter(), const_true(), ModulusRemainder()), s);
    }

    s = Block::make(s, Free::make(names.profiler_func_names));
    s = Allocate::make(names.profiler_func_names, Handle(),
                       MemoryType::Auto, {num_funcs}, const_true(), s);
    s = Block::make(Evaluate::make(stop_profiler), s);

    // Allocate memory for the profiler instance state

    // Check there isn't going to be end-of-struct padding to worry about.
    static_assert((sizeof(halide_profiler_func_stats) & 7) == 0);

    const int instance_size_bytes = sizeof(halide_profiler_instance_state) + num_funcs * sizeof(halide_profiler_func_stats);

    s = Allocate::make(names.profiler_instance, UInt(64), MemoryType::Auto,
                       {(instance_size_bytes + 7) / 8}, const_true(), s);

    // We have nested definitions of the sampling token
    s = uniquify_variable_names(s);

    return s;
}

}  // namespace Internal
}  // namespace Halide
