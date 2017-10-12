#include "StorageFlattening.h"

#include "Bounds.h"
#include "FuseGPUThreadLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Parameter.h"
#include "Scope.h"

#include <sstream>

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
    Scope<int> realizations, shader_scope_realizations;
    bool in_shader = false;

    Expr make_shape_var(string name, string field, size_t dim,
                        const Buffer<> &buf, const Parameter &param) {
        ReductionDomain rdom;
        name = name + "." + field + "." + std::to_string(dim);
        if (scope.contains(name + ".constrained")) {
            name = name + ".constrained";
        }
        return Variable::make(Int(32), name, buf, param, rdom);
    }

    Expr flatten_args(const string &name, const vector<Expr> &args,
                      const Buffer<> &buf, const Parameter &param) {
        bool internal = realizations.contains(name);
        Expr idx = target.has_large_buffers() ? make_zero(Int(64)) : 0;
        vector<Expr> mins(args.size()), strides(args.size());

        for (size_t i = 0; i < args.size(); i++) {
            strides[i] = make_shape_var(name, "stride", i, buf, param);
            mins[i] = make_shape_var(name, "min", i, buf, param);
        }

        if (internal) {
            // f(x, y) -> f[(x-xmin)*xstride + (y-ymin)*ystride] This
            // strategy makes sense when we expect x to cancel with
            // something in xmin.  We use this for internal allocations
            for (size_t i = 0; i < args.size(); i++) {
                if (target.has_large_buffers()) {
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
            Expr base = target.has_large_buffers() ? make_zero(Int(64)) : 0;
            for (size_t i = 0; i < args.size(); i++) {
                if (target.has_large_buffers()) {
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

        if (in_shader) {
            shader_scope_realizations.push(op->name, 0);
        }

        Stmt body = mutate(op->body);

        // Compute the size
        vector<Expr> extents;
        for (size_t i = 0; i < op->bounds.size(); i++) {
            extents.push_back(op->bounds[i].extent);
            extents[i] = mutate(extents[i]);
        }
        Expr condition = mutate(op->condition);

        realizations.pop(op->name);

        if (in_shader) {
            shader_scope_realizations.pop(op->name);
        }

        // The allocation extents of the function taken into account of
        // the align_storage directives. It is only used to determine the
        // host allocation size and the strides in buffer_t objects (which
        // also affects the device allocation in some backends).
        vector<Expr> allocation_extents(extents.size());
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
                            allocation_extents[j] = ((extents[j] + alignment - 1)/alignment)*alignment;
                        } else {
                            allocation_extents[j] = extents[j];
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
        stmt = Allocate::make(op->name, op->types[0], allocation_extents, condition, stmt);

        // Compute the strides
        for (int i = (int)op->bounds.size()-1; i > 0; i--) {
            int prev_j = storage_permutation[i-1];
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

        Expr value = mutate(op->values[0]);
        if (in_shader && !shader_scope_realizations.contains(op->name)) {
            user_assert(op->args.size() == 3)
                << "Image stores require three coordinates.\n";
            Expr buffer_var =
                Variable::make(type_of<halide_buffer_t *>(), op->name + ".buffer", output_buf);
            vector<Expr> args = {
                op->name, buffer_var,
                op->args[0], op->args[1], op->args[2],
                value};
            Expr store = Call::make(value.type(), Call::image_store,
                                    args, Call::Intrinsic);
            stmt = Evaluate::make(store);
        } else {
            Expr idx = mutate(flatten_args(op->name, op->args, Buffer<>(), output_buf));
            stmt = Store::make(op->name, value, idx, output_buf, const_true(value.type().lanes()));
        }
    }

    void visit(const Call *op) {
        if (op->call_type == Call::Halide ||
            op->call_type == Call::Image) {

            internal_assert(op->value_index == 0);

            if (in_shader && !shader_scope_realizations.contains(op->name)) {
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
                for (size_t i = op->args.size(); i < 3; i++) {
                    args.push_back(0);
                    args.push_back(1);
                }

                expr = Call::make(op->type,
                                  Call::image_load,
                                  args,
                                  Call::PureIntrinsic,
                                  FunctionPtr(),
                                  0,
                                  op->image,
                                  op->param);
            } else {
                Expr idx = mutate(flatten_args(op->name, op->args, op->image, op->param));
                expr = Load::make(op->type, op->name, idx, op->image, op->param,
                                  const_true(op->type.lanes()));
            }

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

    void visit(const For *loop) {
        bool old_in_shader = in_shader;
        if ((loop->for_type == ForType::GPUBlock ||
             loop->for_type == ForType::GPUThread) &&
            loop->device_api == DeviceAPI::GLSL) {
            in_shader = true;
        }
        IRMutator::visit(loop);
        in_shader = old_in_shader;
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
    // The OpenGL backend requires loop mins to be zero'd at this point.
    s = zero_gpu_loop_mins(s);

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
