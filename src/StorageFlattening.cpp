#include <sstream>

#include "StorageFlattening.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Bounds.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::map;

namespace {
// Visitor and helper function to test if a piece of IR uses an extern image.
class UsesExternImage : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *c) {
        if (c->call_type == Call::Image) {
            result = true;
        } else {
            IRVisitor::visit(c);
        }
    }
public:
    UsesExternImage() : result(false) {}
    bool result;
};

inline bool uses_extern_image(Stmt s) {
    UsesExternImage uses;
    s.accept(&uses);
    return uses.result;
}
}

class FlattenDimensions : public IRMutator {
public:
    FlattenDimensions(const vector<Function> &outputs, const map<string, Function> &e)
        : outputs(outputs), env(e) {}
    Scope<int> scope;
private:
    const vector<Function> &outputs;
    const map<string, Function> &env;
    Scope<int> realizations;

    Expr flatten_args(const string &name, const vector<Expr> &args,
                      bool internal) {
        Expr idx = 0;
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
        realizations.push(realize->name, 1);

        Stmt body = mutate(realize->body);

        // Compute the size
        std::vector<Expr> extents;
        for (size_t i = 0; i < realize->bounds.size(); i++) {
          extents.push_back(realize->bounds[i].extent);
          extents[i] = mutate(extents[i]);
        }
        Expr condition = mutate(realize->condition);

        realizations.pop(realize->name);

        vector<int> storage_permutation;
        {
            map<string, Function>::const_iterator iter = env.find(realize->name);
            internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
            const vector<string> &storage_dims = iter->second.schedule().storage_dims();
            const vector<string> &args = iter->second.args();
            for (size_t i = 0; i < storage_dims.size(); i++) {
                for (size_t j = 0; j < args.size(); j++) {
                    if (args[j] == storage_dims[i]) {
                        storage_permutation.push_back((int)j);
                    }
                }
                internal_assert(storage_permutation.size() == i+1);
            }
        }

        internal_assert(storage_permutation.size() == realize->bounds.size());

        stmt = body;
        for (size_t idx = 0; idx < realize->types.size(); idx++) {
            string buffer_name = realize->name;
            if (realize->types.size() > 1) {
                buffer_name = buffer_name + '.' + std::to_string(idx);
            }

            // Make the names for the mins, extents, and strides
            int dims = realize->bounds.size();
            vector<string> min_name(dims), extent_name(dims), stride_name(dims);
            for (int i = 0; i < dims; i++) {
                string d = std::to_string(i);
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

            // Promote the type to be a multiple of 8 bits
            Type t = realize->types[idx];
            t.bits = t.bytes() * 8;

            // Create a buffer_t object for this allocation.
            vector<Expr> args(dims*3 + 2);
            //args[0] = Call::make(Handle(), Call::null_handle, vector<Expr>(), Call::Intrinsic);
            Expr first_elem = Load::make(t, buffer_name, 0, Buffer(), Parameter());
            args[0] = Call::make(Handle(), Call::address_of, {first_elem}, Call::Intrinsic);
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

            // Make the allocation node
            stmt = Allocate::make(buffer_name, t, extents, condition, stmt);

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

    struct ProvideValue {
        Expr value;
        string name;
    };

    void flatten_provide_values(vector<ProvideValue> &values, const Provide *provide) {
        values.resize(provide->values.size());

        for (size_t i = 0; i < values.size(); i++) {
            Expr value = mutate(provide->values[i]);

            // Promote the type to be a multiple of 8 bits
            Type t = value.type();
            t.bits = t.bytes() * 8;
            if (t.bits != value.type().bits) {
                value = Cast::make(t, value);
            }

            values[i].value = value;
            if (values.size() > 1) {
                values[i].name = provide->name + "." + std::to_string(i);
            } else {
                values[i].name = provide->name;
            }
        }
    }

    // Lower a set of provides
    Stmt flatten_provide_atomic(const Provide *provide) {
        vector<ProvideValue> values;
        flatten_provide_values(values, provide);

        bool is_output = false;
        for (Function f : outputs) {
            is_output |= f.name() == provide->name;
        }

        Stmt result;
        for (size_t i = 0; i < values.size(); i++) {
            const ProvideValue &cv = values[i];

            Expr idx = mutate(flatten_args(cv.name, provide->args, !is_output));
            Expr var = Variable::make(cv.value.type(), cv.name + ".value");
            Stmt store = Store::make(cv.name, var, idx);

            if (result.defined()) {
                result = Block::make(result, store);
            } else {
                result = store;
            }
        }

        for (size_t i = values.size(); i > 0; i--) {
            const ProvideValue &cv = values[i-1];

            result = LetStmt::make(cv.name + ".value", cv.value, result);
        }
        return result;
    }

    Stmt flatten_provide(const Provide *provide) {
        vector<ProvideValue> values;
        flatten_provide_values(values, provide);

        bool is_output = false;
        for (Function f : outputs) {
            is_output |= f.name() == provide->name;
        }

        Stmt result;
        for (size_t i = 0; i < values.size(); i++) {
            const ProvideValue &cv = values[i];

            Expr idx = mutate(flatten_args(cv.name, provide->args, !is_output));
            Stmt store = Store::make(cv.name, cv.value, idx);

            if (result.defined()) {
                result = Block::make(result, store);
            } else {
                result = store;
            }
        }
        return result;
    }

    void visit(const Provide *provide) {
        Stmt result;

        // Handle the provide atomically if necessary. This logic is
        // currently very conservative, it will lower many provides
        // atomically that do not require it.
        if (provide->values.size() == 1) {
            // If there is only one value, we don't need to worry
            // about atomicity.
            result = flatten_provide(provide);
        } else if (!realizations.contains(provide->name) &&
                   uses_extern_image(provide)) {
            // If the provide is not a realization and it uses an
            // input image, it might be aliased. Flatten it atomically
            // because we can't prove the boxes don't overlap.
            result = flatten_provide_atomic(provide);
        } else {
            Box provided = box_provided(Stmt(provide), provide->name);
            Box required = box_required(Stmt(provide), provide->name);

            if (boxes_overlap(provided, required)) {
                // The boxes provided and required might overlap, so
                // the provide must be done atomically.
                result = flatten_provide_atomic(provide);
            } else {
                // The boxes don't overlap.
                result = flatten_provide(provide);
            }
        }
        stmt = result;
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
                name = name + '.' + std::to_string(call->value_index);
            }

            bool is_output = false;
            for (Function f : outputs) {
                is_output |= f.name() == call->name;
            }

            bool is_input = env.find(call->name) == env.end();

            // Promote the type to be a multiple of 8 bits
            Type t = call->type;
            t.bits = t.bytes() * 8;

            Expr idx = mutate(flatten_args(name, call->args, !(is_output || is_input)));
            expr = Load::make(t, name, idx, call->image, call->param);

            if (call->type.bits != t.bits) {
                expr = Cast::make(call->type, expr);
            }
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


Stmt storage_flattening(Stmt s,
                        const vector<Function> &outputs,
                        const map<string, Function> &env) {
    return FlattenDimensions(outputs, env).mutate(s);
}

}
}
