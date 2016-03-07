#include <algorithm>
#include <map>
#include <string>
#include <limits>

#include "Profiling.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

class InjectProfiling : public IRMutator {
public:
    map<string, int> indices;   // maps from func name -> index in buffer.

    vector<int> stack; // What produce nodes are we currently inside of.

    string pipeline_name;

    Scope<Expr> func_memory_sizes;

    InjectProfiling(const string& pipeline_name) : pipeline_name(pipeline_name) {
        indices["overhead"] = 0;
        stack.push_back(0);
    }

private:
    using IRMutator::visit;

    int get_func_id(const string& name) {
        int idx = -1;
        map<string, int>::iterator iter = indices.find(name);
        if (iter == indices.end()) {
            idx = (int)indices.size();
            indices[name] = idx;
        } else {
            idx = iter->second;
        }
        return idx;
    }

    bool constant_allocation_size(const std::vector<Expr> &extents, int32_t &size) {
        int64_t result = 1;
        for (size_t i = 0; i < extents.size(); i++) {
            if (const IntImm *int_size = extents[i].as<IntImm>()) {
                result *= int_size->value;
                if (result > (static_cast<int64_t>(1)<<31) - 1) { // Out of memory
                    size = 0;
                    return true;
                }
            } else {
                return false;
            }
        }
        size = static_cast<int32_t>(result);
        return true;
    }

    Expr compute_allocation_size(const vector<Expr> &extents, const Expr& condition, const Type& type) {
        int32_t constant_size;
        if (constant_allocation_size(extents, constant_size)) {
            int64_t stack_bytes = constant_size * type.bytes();
            if (stack_bytes > ((int64_t(1) << 31) - 1)) { // Out of memory
                return 0;
            } else if (stack_bytes <= 1024 * 8) { // Allocation on stack
                return 0;
            }
        }
        // Check that the allocation is not scalar (if it were scalar
        // it would have constant size).
        internal_assert(extents.size() > 0);

        Expr size = extents[0];
        for (size_t i = 1; i < extents.size(); i++) {
            size *= extents[i];
        }
        size = Select::make(condition, size * type.bytes(), 0);
        return size;
    }

    void visit(const Allocate *op) {
        int idx = get_func_id(op->name);

        vector<Expr> new_extents;
        bool all_extents_unmodified = true;
        for (size_t i = 0; i < op->extents.size(); i++) {
            new_extents.push_back(mutate(op->extents[i]));
            all_extents_unmodified &= new_extents[i].same_as(op->extents[i]);
        }
        Expr condition = mutate(op->condition);

        Expr size = compute_allocation_size(new_extents, condition, op->type);
        debug(1) << "  Injecting profiler into Allocate " << op->name << "(" << size << ") in pipeline " << pipeline_name << "\n";
        func_memory_sizes.push(op->name, size);

        Stmt body = mutate(op->body);
        Expr new_expr;
        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
        }
        if (all_extents_unmodified &&
            body.same_as(op->body) &&
            condition.same_as(op->condition) &&
            new_expr.same_as(op->new_expr)) {
            stmt = op;
        } else {
            stmt = Allocate::make(op->name, op->type, new_extents, condition, body, new_expr, op->free_function);
        }

        //debug(0) << stmt << "\n\n";

        Expr set_task = Call::make(Int(32), "halide_profiler_memory_allocate",
                                   {pipeline_name, idx, size}, Call::Extern);

        stmt = Block::make(Evaluate::make(set_task), stmt);
    }

    void visit(const Free *op) {
        int idx = get_func_id(op->name);

        Expr size = func_memory_sizes.get(op->name);
        debug(1) << "  Injecting profiler into Free " << op->name << "(" << size << ") in pipeline " << pipeline_name << "\n";
        Expr set_task = Call::make(Int(32), "halide_profiler_memory_free",
                                   {pipeline_name, idx, size}, Call::Extern);

        IRMutator::visit(op);

        stmt = Block::make(Evaluate::make(set_task), stmt);

        func_memory_sizes.pop(op->name);
    }

    void visit(const ProducerConsumer *op) {
        //debug(1) << "  Injecting profiler into ProducerConsumer " << op->name << " in pipeline " << pipeline_name << "\n";
        int idx = get_func_id(op->name);

        stack.push_back(idx);
        Stmt produce = mutate(op->produce);
        Stmt update = op->update.defined() ? mutate(op->update) : Stmt();
        stack.pop_back();

        Stmt consume = mutate(op->consume);

        Expr profiler_token = Variable::make(Int(32), "profiler_token");
        Expr profiler_state = Variable::make(Handle(), "profiler_state");

        // This call gets inlined and becomes a single store instruction.
        Expr set_task = Call::make(Int(32), "halide_profiler_set_current_func",
                                   {profiler_state, profiler_token, idx}, Call::Extern);

        // At the beginning of the consume step, set the current task
        // back to the outer one.
        Expr set_outer_task = Call::make(Int(32), "halide_profiler_set_current_func",
                                         {profiler_state, profiler_token, stack.back()}, Call::Extern);

        produce = Block::make(Evaluate::make(set_task), produce);
        consume = Block::make(Evaluate::make(set_outer_task), consume);

        stmt = ProducerConsumer::make(op->name, produce, update, consume);
    }

    void visit(const For *op) {
        // We profile by storing a token to global memory, so don't enter GPU loops
        if (op->device_api == DeviceAPI::Parent ||
            op->device_api == DeviceAPI::Host) {
            IRMutator::visit(op);
        } else {
            stmt = op;
        }
    }
};

Stmt inject_profiling(Stmt s, string pipeline_name) {
    InjectProfiling profiling(pipeline_name);
    s = profiling.mutate(s);

    int num_funcs = (int)(profiling.indices.size());

    Expr func_names_buf = Load::make(Handle(), "profiling_func_names", 0, Buffer(), Parameter());
    func_names_buf = Call::make(Handle(), Call::address_of, {func_names_buf}, Call::Intrinsic);

    Expr start_profiler = Call::make(Int(32), "halide_profiler_pipeline_start",
                                     {pipeline_name, num_funcs, func_names_buf}, Call::Extern);

    Expr get_state = Call::make(Handle(), "halide_profiler_get_state", {}, Call::Extern);

    Expr profiler_token = Variable::make(Int(32), "profiler_token");

    Expr stop_profiler = Call::make(Int(32), Call::register_destructor,
                                    {Expr("halide_profiler_pipeline_end"), get_state}, Call::Intrinsic);


    s = LetStmt::make("profiler_state", get_state, s);
    // If there was a problem starting the profiler, it will call an
    // appropriate halide error function and then return the
    // (negative) error code as the token.
    s = Block::make(AssertStmt::make(profiler_token >= 0, profiler_token), s);
    s = LetStmt::make("profiler_token", start_profiler, s);

    for (std::pair<string, int> p : profiling.indices) {
        s = Block::make(Store::make("profiling_func_names", p.first, p.second), s);
    }

    s = Block::make(s, Free::make("profiling_func_names"));
    s = Allocate::make("profiling_func_names", Handle(), {num_funcs}, const_true(), s);
    s = Block::make(Evaluate::make(stop_profiler), s);

    return s;
}

}
}
