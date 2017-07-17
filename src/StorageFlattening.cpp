#include <sstream>

#include "StorageFlattening.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Bounds.h"
#include "Parameter.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::map;
using std::pair;
using std::set;

namespace {

class FlattenDimensions : public IRMutator {
public:
    FlattenDimensions(const map<string, pair<Function, int>> &e,
                      const vector<Function> &o,
                      const Target &t)
        : env(e), target(t) {
        for (auto &f : o) {
            outputs.insert(f.name());
        }
    }
    Scope<int> scope;
private:
    const map<string, pair<Function, int>> &env;
    set<string> outputs;
    const Target &target;
    Scope<int> realizations;

    Expr flatten_args(const string &name, const vector<Expr> &args,
                      const Buffer<> &buf, const Parameter &param) {
        bool internal = realizations.contains(name);
        Expr idx = target.has_feature(Target::LargeBuffers) ? make_zero(Int(64)) : 0;
        vector<Expr> mins(args.size()), strides(args.size());

        ReductionDomain rdom;
        for (size_t i = 0; i < args.size(); i++) {
            string dim = std::to_string(i);
            string stride_name = name + ".stride." + dim;
            string min_name = name + ".min." + dim;
            string stride_name_constrained = stride_name + ".constrained";
            string min_name_constrained = min_name + ".constrained";
            if (scope.contains(stride_name_constrained)) {
                stride_name = stride_name_constrained;
            }
            if (scope.contains(min_name_constrained)) {
                min_name = min_name_constrained;
            }
            strides[i] = Variable::make(Int(32), stride_name, buf, param, rdom);
            mins[i] = Variable::make(Int(32), min_name, buf, param, rdom);
        }

        if (internal) {
            // f(x, y) -> f[(x-xmin)*xstride + (y-ymin)*ystride] This
            // strategy makes sense when we expect x to cancel with
            // something in xmin.  We use this for internal allocations
            for (size_t i = 0; i < args.size(); i++) {
                if (target.has_feature(Target::LargeBuffers)) {
                    idx += cast<int64_t>(args[i] - mins[i]) * cast<int64_t>(strides[i]);
                } else {
                    idx += (args[i] - mins[i]) * strides[i];
                }
            }
        } else {
            // f(x, y) -> f[x*stride + y*ystride - (xstride*xmin +
            // ystride*ymin)]. The idea here is that the last term
            // will be pulled outside the inner loop. We use this for
            // external buffers, where the mins and strides are likely
            // to be symbolic
            Expr base = target.has_feature(Target::LargeBuffers) ? make_zero(Int(64)) : 0;
            for (size_t i = 0; i < args.size(); i++) {
                if (target.has_feature(Target::LargeBuffers)) {
                    idx += cast<int64_t>(args[i]) * cast<int64_t>(strides[i]);
                    base += cast<int64_t>(mins[i]) * cast<int64_t>(strides[i]);
                } else {
                    idx += args[i] * strides[i];
                    base += mins[i] * strides[i];
                }
            }
            idx -= base;
        }

        return idx;
    }

    using IRMutator::visit;

    void visit(const Realize *op) {
        realizations.push(op->name, 0);

        Stmt body = mutate(op->body);

        // Compute the size
        vector<Expr> extents;
        for (size_t i = 0; i < op->bounds.size(); i++) {
            extents.push_back(op->bounds[i].extent);
            extents[i] = mutate(extents[i]);
        }
        Expr condition = mutate(op->condition);

        realizations.pop(op->name);

        vector<int> storage_permutation;
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
                        Expr alignment = storage_dims[i].alignment;
                        if (alignment.defined()) {
                            extents[j] = ((extents[j] + alignment - 1)/alignment)*alignment;
                        }
                    }
                }
                internal_assert(storage_permutation.size() == i+1);
            }
        }

        internal_assert(storage_permutation.size() == op->bounds.size());

        stmt = body;
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

        // Create a buffer_t object for this allocation.
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

        // Make the allocation node
        stmt = Allocate::make(op->name, op->types[0], extents, condition, stmt);

        // Compute the strides
        for (int i = (int)op->bounds.size()-1; i > 0; i--) {
            int prev_j = storage_permutation[i-1];
            int j = storage_permutation[i];
            Expr stride = stride_var[prev_j] * extent_var[prev_j];
            stmt = LetStmt::make(stride_name[j], stride, stmt);
        }

        // Innermost stride is one
        if (dims > 0) {
            int innermost = storage_permutation.empty() ? 0 : storage_permutation[0];
            stmt = LetStmt::make(stride_name[innermost], 1, stmt);
        }

        // Assign the mins and extents stored
        for (size_t i = op->bounds.size(); i > 0; i--) {
            stmt = LetStmt::make(min_name[i-1], op->bounds[i-1].min, stmt);
            stmt = LetStmt::make(extent_name[i-1], extents[i-1], stmt);
        }
    }

    void visit(const Provide *op) {
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

        Expr idx = mutate(flatten_args(op->name, op->args, Buffer<>(), output_buf));
        Expr value = mutate(op->values[0]);
        stmt = Store::make(op->name, value, idx, output_buf, const_true(value.type().lanes()));
    }

    void visit(const Call *op) {
        if (op->call_type == Call::Halide ||
            op->call_type == Call::Image) {
            internal_assert(op->value_index == 0);
            Expr idx = mutate(flatten_args(op->name, op->args, op->image, op->param));
            expr = Load::make(op->type, op->name, idx, op->image, op->param,
                              const_true(op->type.lanes()));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Prefetch *op) {
        internal_assert(op->types.size() == 1)
            << "Prefetch from multi-dimensional halide tuple should have been split\n";

        vector<Expr> prefetch_min(op->bounds.size());
        vector<Expr> prefetch_extent(op->bounds.size());
        vector<Expr> prefetch_stride(op->bounds.size());
        for (size_t i = 0; i < op->bounds.size(); i++) {
            prefetch_min[i] = mutate(op->bounds[i].min);
            prefetch_extent[i] = mutate(op->bounds[i].extent);
            prefetch_stride[i] = Variable::make(Int(32), op->name + ".stride." + std::to_string(i), op->param);
        }

        Expr base_offset = mutate(flatten_args(op->name, prefetch_min, Buffer<>(), op->param));
        Expr base_address = Variable::make(Handle(), op->name);
        vector<Expr> args = {base_address, base_offset};

        auto iter = env.find(op->name);
        if (iter != env.end()) {
            // Order the <min, extent> args based on the storage dims (i.e. innermost
            // dimension should be first in args)
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
                    internal_assert(storage_permutation.size() == i+1);
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

        stmt = Evaluate::make(Call::make(op->types[0], Call::prefetch, args, Call::Intrinsic));
    }

    void visit(const LetStmt *let) {
        // Discover constrained versions of things.
        bool constrained_version_exists = ends_with(let->name, ".constrained");
        if (constrained_version_exists) {
            scope.push(let->name, 0);
        }

        IRMutator::visit(let);

        if (constrained_version_exists) {
            scope.pop(let->name);
        }
    }
};

// Realizations, stores, and loads must all be on types that are
// multiples of 8-bits. This really only affects bools
class PromoteToMemoryType : public IRMutator {
    using IRMutator::visit;

    Type upgrade(Type t) {
        return t.with_bits(((t.bits() + 7)/8)*8);
    }

    void visit(const Load *op) {
        Type t = upgrade(op->type);
        if (t != op->type) {
            expr = Cast::make(op->type, Load::make(t, op->name, mutate(op->index),
                                                   op->image, op->param, mutate(op->predicate)));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        Type t = upgrade(op->value.type());
        if (t != op->value.type()) {
            stmt = Store::make(op->name, Cast::make(t, mutate(op->value)), mutate(op->index),
                                                    op->param, mutate(op->predicate));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Allocate *op) {
        Type t = upgrade(op->type);
        if (t != op->type) {
            vector<Expr> extents;
            for (Expr e : op->extents) {
                extents.push_back(mutate(e));
            }
            stmt = Allocate::make(op->name, t, extents,
                                  mutate(op->condition), mutate(op->body),
                                  mutate(op->new_expr), op->free_function);
        } else {
            IRMutator::visit(op);
        }
    }
};

}  // namespace

Stmt storage_flattening(Stmt s,
                        const vector<Function> &outputs,
                        const map<string, Function> &env,
                        const Target &target) {
    // Make an environment that makes it easier to figure out which
    // Function corresponds to a tuple component. foo.0, foo.1, foo.2,
    // all point to the function foo.
    map<string, pair<Function, int>> tuple_env;
    for (auto p : env) {
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

}
}
