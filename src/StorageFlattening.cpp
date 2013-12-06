#include "StorageFlattening.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::map;

class FlattenDimensions : public IRMutator {
public:
    FlattenDimensions(const map<string, Function> &e) : env(e) {}
    Scope<int> scope;
    Scope<int> need_buffer_t;
private:
    const map<string, Function> &env;

    Expr flatten_args(const string &name, const vector<Expr> &args) {
        Expr idx = 0;
        vector<Expr> mins(args.size()), strides(args.size());

        for (size_t i = 0; i < args.size(); i++) {
            string dim = int_to_string(i);
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

        if (env.find(name) != env.end()) {
            // f(x, y) -> f[(x-xmin)*xstride + (y-ymin)*ystride] This
            // strategy makes sense when we expect x to cancel with
            // something in xmin.  We use this for internal allocations
            for (size_t i = 0; i < args.size(); i++) {
                idx += (args[i] - mins[i]) * strides[i];
            }
        } else {
            // f(x, y) -> f[x*stride + y*ystride - (xstride*xmin +
            // ystride*ymin)]. The idea here is that the last term
            // will be pulled outside the inner loop. We use this for
            // external buffers, where the mins and strides are likely
            // to be symbolic
            Expr base = 0;
            for (size_t i = 0; i < args.size(); i++) {
                idx += args[i] * strides[i];
                base += mins[i] * strides[i];
            }
            idx -= base;
        }

        return idx;
    }

    using IRMutator::visit;

    void visit(const Realize *realize) {
        Stmt body = mutate(realize->body);

        // Check if we need to create a buffer_t for this realization
        vector<bool> make_buffer_t(realize->types.size());
        while (need_buffer_t.contains(realize->name)) {
            int idx = need_buffer_t.get(realize->name);
            assert(idx < (int)make_buffer_t.size());
            make_buffer_t[idx] = true;
            need_buffer_t.pop(realize->name);
        }

        // Compute the size
        Expr size = 1;
        for (size_t i = 0; i < realize->bounds.size(); i++) {
            size *= realize->bounds[i].extent;
        }

        vector<int> storage_permutation;
        {
            map<string, Function>::const_iterator iter = env.find(realize->name);
            assert(iter != env.end() && "Realize node refers to function not in environment");
            const vector<string> &storage_dims = iter->second.schedule().storage_dims;
            const vector<string> &args = iter->second.args();
            for (size_t i = 0; i < storage_dims.size(); i++) {
                for (size_t j = 0; j < args.size(); j++) {
                    if (args[j] == storage_dims[i]) {
                        storage_permutation.push_back((int)j);
                    }
                }
                assert(storage_permutation.size() == i+1);
            }
        }

        assert(storage_permutation.size() == realize->bounds.size());

        size = mutate(size);

        stmt = body;

        // Is this function dynamically scheduled?
        // If so, allocate a memoization bitmask.
        if (realize->lazy) {
            string bitmask_name = realize->name + ".result_computed";
            string loop_var_name = realize->name + ".init_loop_var";

            // Allocate and initialize a bitmask to track what's been computed.
            // TODO(bblum): Make the init loop more efficient?
            Expr loop_var = Variable::make(Int(32), loop_var_name);
            Stmt body = Store::make(bitmask_name, const_false(), loop_var);
            Stmt loop = For::make(loop_var_name, Expr(0), size, For::Serial, body);
            stmt = Allocate::make(bitmask_name, Bool(), size, Block::make(loop, stmt));
        }

        for (size_t idx = 0; idx < realize->types.size(); idx++) {
            string buffer_name = realize->name;
            if (realize->types.size() > 1) {
                buffer_name = buffer_name + '.' + int_to_string(idx);
            }

            // Make the names for the mins, extents, and strides
            int dims = realize->bounds.size();
            vector<string> min_name(dims), extent_name(dims), stride_name(dims);
            for (int i = 0; i < dims; i++) {
                string d = int_to_string(i);
                min_name[i] = buffer_name + ".min." + d;
                stride_name[i] = buffer_name + ".stride." + d;
                extent_name[i] = buffer_name + ".extent." + d;
            }
            vector<Expr> min_var(dims), extent_var(dims), stride_var(dims);
            for (int i = 0; i < dims; i++) {
                min_var[i] = Variable::make(Int(32), min_name[i]);
                extent_var[i] = Variable::make(Int(32), extent_name[i]);
                stride_var[i] = Variable::make(Int(32), stride_name[i]);
            }

            if (make_buffer_t[idx]) {
                // We need to make a buffer_t for this buffer
                vector<Expr> args(dims*3 + 2);
                args[0] = Variable::make(Handle(), buffer_name);
                args[1] = realize->types[idx].bytes();
                for (int i = 0; i < dims; i++) {
                    args[3*i+2] = min_var[i];
                    args[3*i+3] = extent_var[i];
                    args[3*i+4] = stride_var[i];
                }
                Expr buf = Call::make(Handle(), Call::create_buffer_t,
                                      args, Call::Intrinsic);
                stmt = LetStmt::make(buffer_name + ".buffer",
                                     buf,
                                     stmt);
            }

            // Make the allocation node
            stmt = Allocate::make(buffer_name, realize->types[idx], size, stmt);

            // Compute the strides
            for (int i = (int)realize->bounds.size()-1; i > 0; i--) {
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
            for (size_t i = realize->bounds.size(); i > 0; i--) {
                stmt = LetStmt::make(min_name[i-1], realize->bounds[i-1].min, stmt);
                stmt = LetStmt::make(extent_name[i-1], realize->bounds[i-1].extent, stmt);
            }
        }
    }

    void visit(const DynamicStmt *dynamic) {
        string bitmask_name = dynamic->name + ".result_computed";
        string index_name = dynamic->name + ".result_computed_index";

        Stmt body = mutate(dynamic->body);
        Expr index_val = flatten_args(dynamic->name, dynamic->indices);
        Expr index_var = Variable::make(Int(32), index_name);

        // TODO(bblum): Emit a release barrier here (and below) in case there
        // is concurrency between the compute level and the store level
        Stmt store = Store::make(bitmask_name, const_true(), index_var);

        // Wrap it with an if that skips the computation if it was already done.
        // TODO(bblum): An acquire barrier will also need to go before stmt.
        Expr load = Load::make(Bool(), bitmask_name, index_var);
        stmt = IfThenElse::make(Not::make(load), Block::make(body, store));
        stmt = LetStmt::make(index_name, index_val, stmt);
    }

    void visit(const Provide *provide) {

        vector<Expr> values(provide->values.size());
        for (size_t i = 0; i < values.size(); i++) {
            values[i] = mutate(provide->values[i]);
        }

        if (values.size() == 1) {
            Expr idx = mutate(flatten_args(provide->name, provide->args));
            stmt = Store::make(provide->name, values[0], idx);
        } else {

            vector<string> names(provide->values.size());
            Stmt result;

            // Store the values by name
            for (size_t i = 0; i < provide->values.size(); i++) {
                string name = provide->name + "." + int_to_string(i);
                Expr idx = mutate(flatten_args(name, provide->args));
                names[i] = name + ".value";
                Expr var = Variable::make(values[i].type(), names[i]);
                Stmt store = Store::make(name, var, idx);
                if (result.defined()) {
                    result = Block::make(result, store);
                } else {
                    result = store;
                }
            }

            // Add the let statements that define the values
            for (size_t i = provide->values.size(); i > 0; i--) {
                result = LetStmt::make(names[i-1], values[i-1], result);
            }

            stmt = result;
        }
    }

    void visit(const Call *call) {

        if (call->call_type == Call::Extern || call->call_type == Call::Intrinsic) {
            vector<Expr> args(call->args.size());
            bool changed = false;
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(call->args[i]);
                if (!args[i].same_as(call->args[i])) changed = true;
            }
            if (!changed) {
                expr = call;
            } else {
                expr = Call::make(call->type, call->name, args, call->call_type);
            }
        } else {
            string name = call->name;
            if (call->call_type == Call::Halide &&
                call->func.outputs() > 1) {
                name = name + '.' + int_to_string(call->value_index);

            }

            Expr idx = mutate(flatten_args(name, call->args));
            expr = Load::make(call->type, name, idx, call->image, call->param);
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

Stmt storage_flattening(Stmt s, const map<string, Function> &env) {
    return FlattenDimensions(env).mutate(s);
}

}
}
