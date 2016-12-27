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
    FlattenDimensions(const map<string, pair<Function, int>> &e, const Target &t)
        : env(e), target(t) {}
    Scope<int> scope;
private:
    const map<string, pair<Function, int>> &env;
    const Target &target;
    Scope<int> realizations;

    Expr flatten_args(const string &name, const vector<Expr> &args) {
        bool internal = realizations.contains(name);
        Expr idx = target.has_feature(Target::LargeBuffers) ? make_zero(Int(64)) : 0;
        vector<Expr> mins(args.size()), strides(args.size());

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
            strides[i] = Variable::make(Int(32), stride_name);
            mins[i] = Variable::make(Int(32), min_name);
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
        std::vector<Expr> extents;
        for (size_t i = 0; i < op->bounds.size(); i++) {
            extents.push_back(op->bounds[i].extent);
            extents[i] = mutate(extents[i]);
        }
        Expr condition = mutate(op->condition);

        realizations.pop(op->name);

        vector<int> storage_permutation;
        {
            auto iter = env.find(op->name);
            Function f = iter->second.first;
            internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
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
        vector<Expr> args(dims*3 + 2);
        Expr first_elem = Load::make(op->types[0], op->name, 0, BufferPtr(), Parameter());
        args[0] = Call::make(Handle(), Call::address_of, {first_elem}, Call::PureIntrinsic);
        args[1] = make_zero(op->types[0]);
        for (int i = 0; i < dims; i++) {
            args[3*i+2] = min_var[i];
            args[3*i+3] = extent_var[i];
            args[3*i+4] = stride_var[i];
        }
        Expr buf = Call::make(type_of<struct halide_buffer_t *>(), Call::create_buffer_t,
                              args, Call::Intrinsic);
        stmt = LetStmt::make(op->name + ".buffer", buf, stmt);

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

        Expr idx = mutate(flatten_args(op->name, op->args));
        Expr value = mutate(op->values[0]);
        stmt = Store::make(op->name, value, idx, Parameter());
    }

    void visit(const Call *op) {
        if (op->call_type == Call::Halide ||
            op->call_type == Call::Image) {
            internal_assert(op->value_index == 0);
            Expr idx = mutate(flatten_args(op->name, op->args));
            expr = Load::make(op->type, op->name, idx, op->image, op->param);
        } else {
            IRMutator::visit(op);
        }
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

    void visit(const Call *op) {
        if (op->is_intrinsic(Call::address_of)) {
            Expr load = mutate(op->args[0]);
            if (const Cast *cast = load.as<Cast>()) {
                load = cast->value;
            }
            expr = Call::make(op->type, op->name, {load}, op->call_type);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Load *op) {
        Type t = upgrade(op->type);
        if (t != op->type) {
            expr = Cast::make(op->type, Load::make(t, op->name, mutate(op->index), op->image, op->param));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        Type t = upgrade(op->value.type());
        if (t != op->value.type()) {
            stmt = Store::make(op->name, Cast::make(t, mutate(op->value)), mutate(op->index), op->param);
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

// Connect Store nodes to their output buffers
class ConnectOutputBuffers : public IRMutator {
    using IRMutator::visit;

    void visit(const Store *op) {
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
            stmt = Store::make(op->name, op->value, op->index, output_buf);
        } else {
            stmt = op;
        }
    }

    const map<string, pair<Function, int>> &env;
    set<string> outputs;

public:
    ConnectOutputBuffers(const std::map<string, pair<Function, int>> &e,
                         const vector<Function> &o) : env(e) {
        for (auto &f : o) {
            outputs.insert(f.name());
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

    s = FlattenDimensions(tuple_env, target).mutate(s);
    s = PromoteToMemoryType().mutate(s);
    s = ConnectOutputBuffers(tuple_env, outputs).mutate(s);
    return s;
}

}
}
