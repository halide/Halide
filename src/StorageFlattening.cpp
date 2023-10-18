#include "StorageFlattening.h"

#include "Bounds.h"
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

// Track the set of variables used by the inner loop
class CollectVars : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Variable *op) override {
        vars.insert(op->name);
    }

public:
    set<string> vars;
};

class LiftDependantLets : public IRMutator {
public:
    vector<pair<string, Expr>> lifted_lets;

    LiftDependantLets(CollectVars *collect_vars)
        : collect_vars_(collect_vars) {
    }

private:
    CollectVars *collect_vars_;

    using IRMutator::visit;

    // Expr visit(const Let *op) override {
    //     // if (op->value.as<Variable>()) {
    //     //     return mutate(substitute(op->name, op->value, op->body));
    //     // } else {
    //     if (collect_vars_->vars.count(op->name))
    //     return IRMutator::visit(op);
    //     // }
    // }

    Stmt visit(const LetStmt *op) override {
        Stmt body = mutate(op->body);
        Expr value = mutate(op->value);
        if (collect_vars_->vars.count(op->name)) {
            op->value.accept(collect_vars_);
            lifted_lets.push_back({op->name, mutate(op->value)});
            return body;
        } else {
            return LetStmt::make(op->name, value, body);
        }
    }
};

class ExpandExpr : public IRMutator {
    using IRMutator::visit;
    const Scope<Expr> &scope;

    Expr visit(const Variable *var) override {
        if (scope.contains(var->name)) {
            Expr expr = scope.get(var->name);
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
    struct HoistedStorageInfo {
        string name;
        Type type;
        MemoryType memory_type;
        vector<Expr> extents;
        Expr condition;

        HoistedStorageInfo(string name, Type type,
                           MemoryType memory_type,
                           const vector<Expr> &extents, Expr condition)
            : name(name),
              type(type),
              memory_type(memory_type),
              extents(extents),
              condition(condition) {
        }
    };

    const map<string, pair<Function, int>> &env;
    set<string> outputs;
    set<string> textures;
    const Target &target;
    Scope<> realizations;
    bool in_gpu = false;
    map<string, vector<HoistedStorageInfo>> hoisted_storages;
    Scope<Expr> scope;

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
        debug(0) << "Found a hoisted storage - " << op->name << "\n";
        hoisted_storages.emplace(op->name, vector<HoistedStorageInfo>());
        Stmt body = mutate(op->body);
        internal_assert(hoisted_storages[op->name].size() == 1);
        const auto &alloc_info = hoisted_storages[op->name].front();
        body = Allocate::make(alloc_info.name, alloc_info.type, alloc_info.memory_type, alloc_info.extents, alloc_info.condition, body);
        // CollectVars collect_vars;
        // for (const auto &e : alloc_info.extents) {
        //     e.accept(&collect_vars);
        // }
        // alloc_info.condition.accept(&collect_vars);

        // LiftDependantLets lift_lets(&collect_vars);
        // body = lift_lets.mutate(body);

        // for (const auto &v : collect_vars.vars) {
        //     debug(0) << "Depends on: " << v << "\n";
        // }
        // for (int ix = lift_lets.lifted_lets.size() - 1; ix >= 0; ix--) {
        //     body = LetStmt::make(lift_lets.lifted_lets[ix].first, lift_lets.lifted_lets[ix].second, body);
        // }
        hoisted_storages.erase(op->name);
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
        {
            auto iter = env.find(op->name);
            internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
            Function f = iter->second.first;
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

        if (hoisted_storages.count(op->name) > 0) {
            vector<Expr> expanded_extents;
            for (const auto &e : allocation_extents) {
                expanded_extents.push_back(simplify(substitute_in_all_lets(expand_expr(e, scope))));
            }
            HoistedStorageInfo hoisted_alloc(op->name, op->types[0], op->memory_type, expanded_extents, condition);

            debug(0) << "Inside of the corresponding hoisted storage" << op->name << "\n";
            hoisted_storages[op->name].push_back(hoisted_alloc);
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
        bool old_in_gpu = in_gpu;
        if (op->for_type == ForType::GPUBlock ||
            op->for_type == ForType::GPUThread) {
            in_gpu = true;
        }
        Stmt stmt = IRMutator::visit(op);
        in_gpu = old_in_gpu;
        return stmt;
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<Expr> bind(scope, op->name, simplify(expand_expr(op->value, scope)));
        return IRMutator::visit(op);
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

class FindAndRemoveAllAllocates : public IRMutator {
    using IRMutator::visit;

    std::string name_;

    Stmt visit(const Allocate *op) override {
        if (op->name == name_) {
            debug(0) << "Found allocate with the name " << op->name << "\n";
            allocates.push_back(Allocate::make(op->name, op->type, op->memory_type, op->extents,
                                               op->condition, op->body, op->new_expr, op->free_function,
                                               op->padding));
            return mutate(op->body);
        }
        return IRMutator::visit(op);
    }

public:
    std::vector<Stmt> allocates;
    FindAndRemoveAllAllocates(const string &name)
        : name_(name) {
    }
};

class HoistStorage : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const HoistedStorage *op) override {
        debug(0) << "Found hoisted storage " << op->name << "\n";
        Stmt body = mutate(op->body);
        FindAndRemoveAllAllocates allocates_finder(op->name);
        body = allocates_finder.mutate(body);
        internal_assert(allocates_finder.allocates.size() == 1);
        for (const auto &allocate : allocates_finder.allocates) {
            debug(0) << allocate.as<Allocate>()->name << "\n";
        }
        const auto *allocate = allocates_finder.allocates[0].as<Allocate>();
        return Allocate::make(allocate->name, allocate->type, allocate->memory_type, allocate->extents,
                              allocate->condition, body, allocate->new_expr, allocate->free_function,
                              allocate->padding);
        // return body;
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
    debug(0) << s << "\n";
    s = FlattenDimensions(tuple_env, outputs, target).mutate(s);
    s = PromoteToMemoryType().mutate(s);
    // s = HoistStorage().mutate(s);
    debug(0) << s << "\n";
    return s;
}

}  // namespace Internal
}  // namespace Halide
