#include "StorageFlattening.h"

#include "Bounds.h"
#include "CSE.h"
#include "Function.h"
#include "FuseGPUThreadLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Parameter.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

#include <sstream>

namespace Halide {
namespace Internal {

using std::map;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

class ExpandExpr : public IRMutator {
    using IRMutator::visit;
    const Scope<Expr> &scope;

    Expr visit(const Variable *var) override {
        if (scope.contains(var->name)) {
            Expr expr = scope.get(var->name);
            // Mutate the expression, so lets can get replaced recursively.
            expr = mutate(expr);
            debug(4) << "Fully expanded " << var->name << " -> " << expr << "\n";
            return expr;
        } else {
            return var;
        }
    }

public:
    ExpandExpr(const Scope<Expr> &s)
        : scope(s) {
    }
};

// Perform all the substitutions in a scope
Expr expand_expr(const Expr &e, const Scope<Expr> &scope) {
    ExpandExpr ee(scope);
    Expr result = ee.mutate(e);
    debug(4) << "Expanded " << e << " into " << result << "\n";
    return result;
}

class FlattenDimensions : public IRMutator {
public:
    FlattenDimensions(const map<string, pair<Function, int>> &e,
                      const vector<Function> &o,
                      const Target &t)
        : env(e), target(t) {
        for (const auto &f : o) {
            outputs.insert(f.name());
        }
    }

private:
    struct HoistedAllocationInfo {
        string name;
        Type type;
        MemoryType memory_type;
        vector<Expr> extents;
        Expr condition;

        HoistedAllocationInfo(const string &name, Type type,
                              MemoryType memory_type,
                              const vector<Expr> &extents, Expr condition)
            : name(name),
              type(type),
              memory_type(memory_type),
              extents(extents),
              condition(std::move(condition)) {
        }
    };

    struct HoistedStorageData {
        string name;
        vector<HoistedAllocationInfo> hoisted_allocations;
        Scope<Interval> loop_vars;
        Scope<Expr> scope;

        HoistedStorageData(const string &n)
            : name(n) {
        }
    };

    const map<string, pair<Function, int>> &env;
    set<string> outputs;
    set<string> textures;
    const Target &target;
    Scope<> realizations;
    bool in_gpu = false;
    vector<HoistedStorageData> hoisted_storages;
    map<string, int> hoisted_storages_map;

    Expr make_shape_var(string name, const string &field, size_t dim,
                        const Buffer<> &buf, const Parameter &param) {
        ReductionDomain rdom;
        name = name + "." + field + "." + std::to_string(dim);
        return Variable::make(Int(32), name, buf, param, rdom);
    }

    Expr flatten_args(const string &name, vector<Expr> args,
                      const Buffer<> &buf, const Parameter &param) {
        bool internal = realizations.contains(name);
        Expr idx = target.has_large_buffers() ? make_zero(Int(64)) : 0;
        vector<Expr> mins(args.size()), strides(args.size());

        for (size_t i = 0; i < args.size(); i++) {
            strides[i] = make_shape_var(name, "stride", i, buf, param);
            mins[i] = make_shape_var(name, "min", i, buf, param);
            if (target.has_large_buffers()) {
                strides[i] = cast<int64_t>(strides[i]);
            }
        }

        Expr zero = target.has_large_buffers() ? make_zero(Int(64)) : 0;

        // We peel off constant offsets so that multiple stencil
        // taps can share the same base address.
        Expr constant_term = zero;
        for (size_t i = 0; i < args.size(); i++) {
            const Add *add = args[i].as<Add>();
            if (add && is_const(add->b)) {
                constant_term += strides[i] * add->b;
                args[i] = add->a;
            }
        }

        if (internal) {
            // f(x, y) -> f[(x-xmin)*xstride + (y-ymin)*ystride] This
            // strategy makes sense when we expect x to cancel with
            // something in xmin.  We use this for internal allocations.
            for (size_t i = 0; i < args.size(); i++) {
                idx += (args[i] - mins[i]) * strides[i];
            }
        } else {
            // f(x, y) -> f[x*stride + y*ystride - (xstride*xmin +
            // ystride*ymin)]. The idea here is that the last term
            // will be pulled outside the inner loop. We use this for
            // external buffers, where the mins and strides are likely
            // to be symbolic
            Expr base = zero;
            for (size_t i = 0; i < args.size(); i++) {
                idx += args[i] * strides[i];
                base += mins[i] * strides[i];
            }
            idx -= base;
        }

        if (!is_const_zero(constant_term)) {
            idx += constant_term;
        }

        return idx;
    }

    using IRMutator::visit;

    Stmt visit(const HoistedStorage *op) override {
        hoisted_storages.emplace_back(op->name);
        // Record index in the stack.
        hoisted_storages_map[op->name] = hoisted_storages.size() - 1;
        Stmt body = mutate(op->body);
        internal_assert(!hoisted_storages.back().hoisted_allocations.empty()) << "Couldn't find a matching Realize node for Hoisted storage " << op->name << "\n";
        const auto &alloc_info = hoisted_storages.back().hoisted_allocations.front();
        vector<Expr> extents = alloc_info.extents;
        for (int i = 1; i < (int)hoisted_storages.back().hoisted_allocations.size(); i++) {
            const auto &ai = hoisted_storages.back().hoisted_allocations[i];
            internal_assert(ai.extents.size() == alloc_info.extents.size());
            for (int j = 0; j < (int)extents.size(); j++) {
                extents[j] = Max::make(extents[j], ai.extents[j]);
            }
        }
        body = Allocate::make(alloc_info.name, alloc_info.type, alloc_info.memory_type, extents, alloc_info.condition, body);
        hoisted_storages_map.erase(op->name);
        hoisted_storages.pop_back();
        return body;
    }

    Stmt visit(const Realize *op) override {
        realizations.push(op->name);

        if (op->memory_type == MemoryType::GPUTexture) {
            textures.insert(op->name);
            debug(2) << "found texture " << op->name << "\n";
        }

        Stmt body = mutate(op->body);

        // Compute the size
        vector<Expr> extents(op->bounds.size());
        for (size_t i = 0; i < op->bounds.size(); i++) {
            extents[i] = mutate(op->bounds[i].extent);
        }
        Expr condition = mutate(op->condition);

        realizations.pop(op->name);

        // The allocation extents of the function taken into account of
        // the align_storage directives. It is only used to determine the
        // host allocation size and the strides in halide_buffer_t objects (which
        // also affects the device allocation in some backends).
        vector<Expr> allocation_extents(extents.size());
        vector<int> storage_permutation;
        vector<Stmt> bound_asserts;
        bool is_ring_buffered = false;
        {
            auto iter = env.find(op->name);
            internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
            Function f = iter->second.first;
            is_ring_buffered = f.schedule().ring_buffer().defined();
            const vector<StorageDim> &storage_dims = f.schedule().storage_dims();
            const vector<string> &args = f.args();
            for (size_t i = 0; i < storage_dims.size(); i++) {
                for (size_t j = 0; j < args.size(); j++) {
                    if (args[j] == storage_dims[i].var) {
                        storage_permutation.push_back((int)j);
                        Expr bound = storage_dims[i].bound;
                        if (bound.defined()) {
                            if (can_prove(extents[j] > bound)) {
                                user_error << "Explicit storage bound (" << bound << ") for variable " << args[j] << " of function " << op->name << " is smaller than required (" << extents[j] << ")\n";
                            }
                            Expr bound_too_small_error =
                                Call::make(Int(32),
                                           "halide_error_storage_bound_too_small",
                                           {StringImm::make(op->name), StringImm::make(args[j]), bound, extents[j]},
                                           Call::Extern);
                            Stmt size_to_small_check = AssertStmt::make(extents[j] <= bound, bound_too_small_error);
                            bound_asserts.push_back(size_to_small_check);
                            extents[j] = bound;
                        }
                        Expr alignment = storage_dims[i].alignment;
                        if (alignment.defined()) {
                            allocation_extents[j] = ((extents[j] + alignment - 1) / alignment) * alignment;
                        } else {
                            allocation_extents[j] = extents[j];
                        }
                    }
                }
                internal_assert(storage_permutation.size() == i + 1);
            }
            if (is_ring_buffered) {
                storage_permutation.push_back(storage_dims.size());
                allocation_extents[storage_dims.size()] = extents[storage_dims.size()];
            }
        }

        internal_assert(storage_permutation.size() == op->bounds.size());

        Stmt stmt = body;
        internal_assert(op->types.size() == 1);

        // Make the names for the mins, extents, and strides
        int dims = op->bounds.size();
        vector<string> min_name(dims), extent_name(dims), stride_name(dims);
        for (int i = 0; i < dims; i++) {
            string d = std::to_string(i);
            min_name[i] = op->name + ".min." + d;
            stride_name[i] = op->name + ".stride." + d;
            extent_name[i] = op->name + ".extent." + d;
        }
        vector<Expr> min_var(dims), extent_var(dims), stride_var(dims);
        for (int i = 0; i < dims; i++) {
            min_var[i] = Variable::make(Int(32), min_name[i]);
            extent_var[i] = Variable::make(Int(32), extent_name[i]);
            stride_var[i] = Variable::make(Int(32), stride_name[i]);
        }

        // Create a halide_buffer_t object for this allocation.
        BufferBuilder builder;
        builder.host = Variable::make(Handle(), op->name);
        builder.type = op->types[0];
        builder.dimensions = dims;

        for (int i = 0; i < dims; i++) {
            builder.mins.push_back(min_var[i]);
            builder.extents.push_back(extent_var[i]);
            builder.strides.push_back(stride_var[i]);
        }
        stmt = LetStmt::make(op->name + ".buffer", builder.build(), stmt);
        if (hoisted_storages_map.count(op->name) > 0) {
            HoistedStorageData &hoisted_storage_data = hoisted_storages[hoisted_storages_map[op->name]];
            vector<Expr> bounded_extents;
            for (const auto &e : allocation_extents) {
                Expr expanded_extent = e;
                // Iterate from innermost outwards
                for (auto it = hoisted_storages.rbegin(); it != hoisted_storages.rend(); it++) {
                    expanded_extent = expand_expr(expanded_extent, it->scope);
                    if (it->name == op->name) {
                        break;
                    }
                }
                expanded_extent = simplify(common_subexpression_elimination(expanded_extent));
                Interval bounds = bounds_of_expr_in_scope(expanded_extent, hoisted_storage_data.loop_vars);
                user_assert(bounds.max.defined()) << "Couldn't infer the upper bound for the storage size of " << op->name << ", consider using bound_storage.\n";
                bounded_extents.push_back(bounds.max);
            }

            HoistedAllocationInfo hoisted_alloc(op->name, op->types[0], op->memory_type, bounded_extents, condition);

            hoisted_storage_data.hoisted_allocations.push_back(hoisted_alloc);
        } else {
            // Make the allocation node
            stmt = Allocate::make(op->name, op->types[0], op->memory_type, allocation_extents, condition, stmt);
        }

        // Wrap it into storage bound asserts.
        if (!bound_asserts.empty()) {
            stmt = Block::make(Block::make(bound_asserts), stmt);
        }

        // Compute the strides
        for (int i = (int)op->bounds.size() - 1; i > 0; i--) {
            int prev_j = storage_permutation[i - 1];
            int j = storage_permutation[i];
            Expr stride = stride_var[prev_j] * allocation_extents[prev_j];
            stmt = LetStmt::make(stride_name[j], stride, stmt);
        }

        // Innermost stride is one
        if (dims > 0) {
            int innermost = storage_permutation.empty() ? 0 : storage_permutation[0];
            stmt = LetStmt::make(stride_name[innermost], 1, stmt);
        }

        // Assign the mins and extents stored
        for (size_t i = op->bounds.size(); i > 0; i--) {
            stmt = LetStmt::make(min_name[i - 1], op->bounds[i - 1].min, stmt);
            stmt = LetStmt::make(extent_name[i - 1], extents[i - 1], stmt);
        }

        return stmt;
    }

    Stmt visit(const Provide *op) override {
        internal_assert(op->values.size() == 1);

        Parameter output_buf;
        auto it = env.find(op->name);
        if (it != env.end()) {
            const Function &f = it->second.first;
            int idx = it->second.second;

            // We only want to do this for actual pipeline outputs,
            // even though every Function has an output buffer. Any
            // constraints you set on the output buffer of a Func that
            // isn't actually an output is ignored. This is a language
            // wart.
            if (outputs.count(f.name())) {
                output_buf = f.output_buffers()[idx];
            }
        }

        if (output_buf.defined()) {
            if (output_buf.memory_type() == MemoryType::GPUTexture) {
                textures.insert(op->name);
            }
        }

        Expr value = mutate(op->values[0]);
        Expr predicate = mutate(op->predicate);
        if (in_gpu && textures.count(op->name)) {
            Expr buffer_var =
                Variable::make(type_of<halide_buffer_t *>(), op->name + ".buffer", output_buf);
            vector<Expr> args(2);
            args[0] = op->name;
            args[1] = buffer_var;
            for (size_t i = 0; i < op->args.size(); i++) {
                Expr min = Variable::make(Int(32), op->name + ".min." + std::to_string(i));
                args.push_back(op->args[i] - min);
            }
            args.push_back(value);
            Expr store = Call::make(value.type(), Call::image_store,
                                    args, Call::Intrinsic);
            Stmt result = Evaluate::make(store);
            if (!is_const_one(op->predicate)) {
                result = IfThenElse::make(predicate, result);
            }
            return result;
        } else {
            Expr idx = mutate(flatten_args(op->name, op->args, Buffer<>(), output_buf));
            return Store::make(op->name, value, idx, output_buf, predicate, ModulusRemainder());
        }
    }

    Expr visit(const Call *op) override {
        if (op->call_type == Call::Halide ||
            op->call_type == Call::Image) {

            debug(2) << " load call to " << op->name << " " << textures.count(op->name) << "\n";
            if (op->param.defined()) {
                debug(2) << "     is param: "
                         << " " << op->param.name() << " " << op->param.memory_type()
                         << "\n";

                if (op->param.memory_type() == MemoryType::GPUTexture) {
                    textures.insert(op->name);
                }
            }

            internal_assert(op->value_index == 0);

            if (in_gpu && textures.count(op->name)) {
                ReductionDomain rdom;
                Expr buffer_var =
                    Variable::make(type_of<halide_buffer_t *>(), op->name + ".buffer",
                                   op->image, op->param, rdom);

                // Create image_load("name", name.buffer, x - x_min, x_extent,
                // y - y_min, y_extent, ...).  Extents can be used by
                // successive passes. OpenGL, for example, uses them
                // for coordinate normalization.
                vector<Expr> args(2);
                args[0] = op->name;
                args[1] = buffer_var;
                for (size_t i = 0; i < op->args.size(); i++) {
                    Expr min = make_shape_var(op->name, "min", i, op->image, op->param);
                    Expr extent = make_shape_var(op->name, "extent", i, op->image, op->param);
                    args.push_back(mutate(op->args[i]) - min);
                    args.push_back(extent);
                }

                return Call::make(op->type,
                                  Call::image_load,
                                  args,
                                  Call::PureIntrinsic,
                                  FunctionPtr(),
                                  0,
                                  op->image,
                                  op->param);
            } else {
                Expr idx = mutate(flatten_args(op->name, op->args, op->image, op->param));
                return Load::make(op->type, op->name, idx, op->image, op->param,
                                  const_true(op->type.lanes()), ModulusRemainder());
            }

        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Prefetch *op) override {
        internal_assert(op->types.size() == 1)
            << "Prefetch from multi-dimensional halide tuple should have been split\n";

        Expr condition = mutate(op->condition);

        vector<Expr> prefetch_min(op->bounds.size());
        vector<Expr> prefetch_extent(op->bounds.size());
        vector<Expr> prefetch_stride(op->bounds.size());
        for (size_t i = 0; i < op->bounds.size(); i++) {
            prefetch_min[i] = mutate(op->bounds[i].min);
            prefetch_extent[i] = mutate(op->bounds[i].extent);
            prefetch_stride[i] = Variable::make(Int(32), op->name + ".stride." + std::to_string(i), op->prefetch.param);
        }

        Expr base_offset = mutate(flatten_args(op->name, prefetch_min, Buffer<>(), op->prefetch.param));
        Expr base_address = Variable::make(Handle(), op->name);
        vector<Expr> args = {base_address, base_offset};

        auto iter = env.find(op->name);
        if (iter != env.end()) {
            // Order the <min, extent> args based on the storage dims
            // (i.e. innermost dimension should be first in args)
            vector<int> storage_permutation;
            {
                Function f = iter->second.first;
                const vector<StorageDim> &storage_dims = f.schedule().storage_dims();
                const vector<string> &args = f.args();
                for (size_t i = 0; i < storage_dims.size(); i++) {
                    for (size_t j = 0; j < args.size(); j++) {
                        if (args[j] == storage_dims[i].var) {
                            storage_permutation.push_back((int)j);
                        }
                    }
                    internal_assert(storage_permutation.size() == i + 1);
                }
            }
            internal_assert(storage_permutation.size() == op->bounds.size());

            for (size_t i = 0; i < op->bounds.size(); i++) {
                internal_assert(storage_permutation[i] < (int)op->bounds.size());
                args.push_back(prefetch_extent[storage_permutation[i]]);
                args.push_back(prefetch_stride[storage_permutation[i]]);
            }
        } else {
            for (size_t i = 0; i < op->bounds.size(); i++) {
                args.push_back(prefetch_extent[i]);
                args.push_back(prefetch_stride[i]);
            }
        }

        // TODO: Consider generating a prefetch call for each tuple element.
        Stmt prefetch_call = Evaluate::make(Call::make(op->types[0], Call::prefetch, args, Call::Intrinsic));
        if (!is_const_one(condition)) {
            prefetch_call = IfThenElse::make(condition, prefetch_call);
        }
        Stmt body = mutate(op->body);
        return Block::make(prefetch_call, body);
    }

    Stmt visit(const For *op) override {
        Expr expanded_min = op->min;
        Expr expanded_extent = op->extent;
        // Iterate from innermost outwards
        for (auto it = hoisted_storages.rbegin(); it != hoisted_storages.rend(); it++) {
            expanded_min = simplify(expand_expr(expanded_min, it->scope));
            expanded_extent = expand_expr(expanded_extent, it->scope);
            Interval loop_bounds = Interval(expanded_min, simplify(expanded_min + expanded_extent - 1));
            it->loop_vars.push(op->name, loop_bounds);
        }
        bool old_in_gpu = in_gpu;
        if (op->for_type == ForType::GPUBlock ||
            op->for_type == ForType::GPUThread) {
            in_gpu = true;
        }
        Stmt stmt = IRMutator::visit(op);
        in_gpu = old_in_gpu;

        for (auto &p : hoisted_storages) {
            p.loop_vars.pop(op->name);
        }

        return stmt;
    }

    Stmt visit(const LetStmt *op) override {
        if (!hoisted_storages.empty()) {
            hoisted_storages.back().scope.push(op->name, op->value);
        }

        Stmt stmt = IRMutator::visit(op);

        if (!hoisted_storages.empty()) {
            hoisted_storages.back().scope.pop(op->name);
        }
        return stmt;
    }
};

// Realizations, stores, and loads must all be on types that are
// multiples of 8-bits. This really only affects bools
class PromoteToMemoryType : public IRMutator {
    using IRMutator::visit;

    Type upgrade(Type t) {
        return t.with_bits(((t.bits() + 7) / 8) * 8);
    }

    Expr visit(const Load *op) override {
        Type t = upgrade(op->type);
        if (t != op->type) {
            return Cast::make(op->type,
                              Load::make(t, op->name, mutate(op->index),
                                         op->image, op->param, mutate(op->predicate), ModulusRemainder()));
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        Type t = upgrade(op->value.type());
        if (t != op->value.type()) {
            return Store::make(op->name, Cast::make(t, mutate(op->value)), mutate(op->index),
                               op->param, mutate(op->predicate), ModulusRemainder());
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        Type t = upgrade(op->type);
        if (t != op->type) {
            return Allocate::make(op->name, t, op->memory_type, mutate(op->extents),
                                  mutate(op->condition), mutate(op->body),
                                  mutate(op->new_expr), op->free_function, op->padding);
        } else {
            return IRMutator::visit(op);
        }
    }
};

}  // namespace

Stmt storage_flattening(Stmt s,
                        const vector<Function> &outputs,
                        const map<string, Function> &env,
                        const Target &target) {
    // The OpenGL backend requires loop mins to be zero'd at this point.
    s = zero_gpu_loop_mins(s);

    // Make an environment that makes it easier to figure out which
    // Function corresponds to a tuple component. foo.0, foo.1, foo.2,
    // all point to the function foo.
    map<string, pair<Function, int>> tuple_env;
    for (const auto &p : env) {
        if (p.second.outputs() > 1) {
            for (int i = 0; i < p.second.outputs(); i++) {
                tuple_env[p.first + "." + std::to_string(i)] = {p.second, i};
            }
        } else {
            tuple_env[p.first] = {p.second, 0};
        }
    }
    s = FlattenDimensions(tuple_env, outputs, target).mutate(s);
    s = PromoteToMemoryType().mutate(s);

    return s;
}

}  // namespace Internal
}  // namespace Halide
