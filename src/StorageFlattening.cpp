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

#include <optional>
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

// One axis of a Func's storage layout -- there is one per dimension of the
// allocated halide_buffer_t. coord and extent are written symbolically in terms
// of per-arg placeholders: the relative coordinate "__ss_rel.<j>" (the j'th pure
// arg minus its min) and the extent "__ss_ext.<j>", which are substituted with
// concrete values at each use site. With no storage splits there is one axis per
// storage dim (a permutation of the pure args, plus a ring-buffer axis if any);
// split_storage expands a storage dim into two axes.
struct StorageAxis {
    Expr coord;
    Expr extent;
    Expr bound;       // explicit bound_storage, may be undefined
    Expr alignment;   // explicit align_storage, may be undefined
    int arg;          // the pure arg this axis derives from (or the ring dim)
    bool split;       // did this axis come from split_storage?
    std::string var;  // the storage-dim name (for error messages)
};

// The storage axes of f, innermost first, replaying any storage splits.
std::vector<StorageAxis> storage_layout(const Function &f, bool ring_buffered) {
    const std::vector<StorageDim> &sdims = f.schedule().storage_dims();
    const std::vector<StorageSplit> &splits = f.schedule().storage_splits();
    const std::vector<std::string> &args = f.args();
    const int num_args = (int)args.size();

    struct Info {
        Expr coord, extent;
        int arg;
        bool split;
    };
    std::map<std::string, Info> info;
    for (int j = 0; j < num_args; j++) {
        info[args[j]] = Info{Variable::make(Int(32), "__ss_rel." + std::to_string(j)),
                             Variable::make(Int(32), "__ss_ext." + std::to_string(j)),
                             j, false};
    }
    for (const StorageSplit &s : splits) {
        Info p = info.at(s.old_var);
        info.erase(s.old_var);
        Expr fac = s.factor;
        info[s.inner] = Info{p.coord % fac, fac, p.arg, true};
        info[s.outer] = Info{p.coord / fac, (p.extent + fac - 1) / fac, p.arg, true};
    }

    std::vector<StorageAxis> result;
    result.reserve(sdims.size() + (ring_buffered ? 1 : 0));
    for (const StorageDim &d : sdims) {
        const Info &i = info.at(d.var);
        result.push_back({i.coord, i.extent, d.bound, d.alignment, i.arg, i.split, d.var});
    }
    if (ring_buffered) {
        // The ring buffer is an extra outermost axis, beyond the pure args.
        result.push_back({Variable::make(Int(32), "__ss_rel." + std::to_string(num_args)),
                          Variable::make(Int(32), "__ss_ext." + std::to_string(num_args)),
                          Expr(), Expr(), num_args, false, "__ring_buffer"});
    }
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

    const map<string, pair<Function, int>> &env;
    set<string> outputs;
    set<string> textures;
    const Target &target;
    Scope<> realizations;
    bool in_gpu = false;

    // The name of the Function whose Provide node is currently being
    // flattened, if any. Used to avoid streaming self-loads.
    string current_provide_name;

    // Whether the Provide node currently being flattened is wrapped in a
    // StreamingStore node (see Stage::stream_stores), i.e. whether the Store
    // node(s) it becomes should be marked non-temporal.
    bool in_streaming_store = false;

    // The stream_loads() request (see Stage::stream_loads) of the
    // StreamingLoads node currently wrapping the Provide being flattened.
    // The default (present, empty), used whenever no such node is in
    // scope, means stream nothing; nullopt (only ever set by an active
    // StreamingLoads scope) means every direct load of another Func should
    // stream; otherwise only loads of the named Funcs should. Names
    // actually matched are recorded in stream_loads_matched (when
    // non-null) so we can warn about any requested name that never turned
    // out to be a direct load.
    std::optional<std::set<std::string>> stream_loads_names = std::set<std::string>{};
    std::set<std::string> *stream_loads_matched = nullptr;

    Expr make_shape_var(string name, const string &field, size_t dim,
                        const Buffer<> &buf, const Parameter &param) {
        ReductionDomain rdom;
        name = name + "." + field + "." + std::to_string(dim);
        return Variable::make(Int(32), name, buf, param, rdom);
    }

    Expr flatten_args(const string &name, vector<Expr> args,
                      const Buffer<> &buf, const Parameter &param) {
        const bool internal = realizations.contains(name);
        const bool wide = target.has_large_buffers();
        const Expr zero = wide ? make_zero(Int(64)) : make_zero(Int(32));

        if (!internal) {
            // External buffer (input or output): the mins and strides come
            // from the buffer/param, one per pure arg. Storage splits are
            // internal-only, so this path is unchanged.
            auto it = env.find(name);
            user_assert(it == env.end() || !func_has_storage_splits(it->second.first))
                << "split_storage is only supported for internal allocations, but "
                << name << " has external storage (it is an output or extern stage).\n";
            vector<Expr> mins(args.size()), strides(args.size());
            for (size_t i = 0; i < args.size(); i++) {
                strides[i] = make_shape_var(name, "stride", i, buf, param);
                mins[i] = make_shape_var(name, "min", i, buf, param);
                if (wide) {
                    strides[i] = cast<int64_t>(strides[i]);
                }
            }

            // Peel off constant offsets so that multiple stencil taps can
            // share the same base address.
            Expr constant_term = zero;
            for (size_t i = 0; i < args.size(); i++) {
                const Add *add = args[i].as<Add>();
                if (add && is_const(add->b)) {
                    constant_term += strides[i] * add->b;
                    args[i] = add->a;
                }
            }

            // f(x, y) -> f[x*stride + y*ystride - (xstride*xmin +
            // ystride*ymin)]. The last term will be pulled outside the inner
            // loop. The mins and strides are likely to be symbolic.
            Expr idx = zero, base = zero;
            for (size_t i = 0; i < args.size(); i++) {
                idx += args[i] * strides[i];
                base += mins[i] * strides[i];
            }
            idx -= base;
            if (!is_const_zero(constant_term)) {
                idx += constant_term;
            }
            return idx;
        }

        // Internal allocation: build the index from the storage layout. With
        // no splits this is one axis per pure arg with arg-indexed strides, so
        // it reduces to f[(x-xmin)*xstride + (y-ymin)*ystride ...] as before.
        auto it = env.find(name);
        internal_assert(it != env.end()) << "Internal allocation " << name << " not in environment.\n";
        const Function &f = it->second.first;
        const bool ring = f.schedule().ring_buffer().defined();
        const bool splits = func_has_storage_splits(f);
        vector<StorageAxis> layout = storage_layout(f, ring);

        // The buffer dimension an axis lives in: its pure arg when there are no
        // splits (so the buffer stays in arg order), else its storage position.
        auto buffer_dim = [&](int p) { return splits ? p : layout[p].arg; };

        // Peel constant offsets off args feeding a linear (non-split) axis, so
        // that stencil taps can share a base address.
        Expr constant_term = zero;
        if (!splits) {
            for (size_t p = 0; p < layout.size(); p++) {
                int a = layout[p].arg;
                const Add *add = args[a].as<Add>();
                if (add && is_const(add->b)) {
                    Expr stride = make_shape_var(name, "stride", a, buf, param);
                    if (wide) {
                        stride = cast<int64_t>(stride);
                    }
                    constant_term += stride * add->b;
                    args[a] = add->a;
                }
            }
        }

        Expr idx = zero;
        for (size_t p = 0; p < layout.size(); p++) {
            int a = layout[p].arg;
            // For split funcs the buffer mins are zero, so the arg min is bound
            // separately as name.arg_min.<a>; otherwise it is the buffer min.
            Expr arg_min = splits ? Variable::make(Int(32), name + ".arg_min." + std::to_string(a)) : make_shape_var(name, "min", a, buf, param);
            Expr coord = substitute("__ss_rel." + std::to_string(a), args[a] - arg_min, layout[p].coord);
            Expr stride = make_shape_var(name, "stride", buffer_dim(p), buf, param);
            if (wide) {
                coord = cast<int64_t>(coord);
                stride = cast<int64_t>(stride);
            }
            idx += coord * stride;
        }
        if (!is_const_zero(constant_term)) {
            idx += constant_term;
        }
        return idx;
    }

    static bool func_has_storage_splits(const Function &f) {
        return !f.schedule().storage_splits().empty();
    }

    using IRMutator::visit;

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

        internal_assert(op->types.size() == 1);
        auto iter = env.find(op->name);
        internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
        const Function &f = iter->second.first;
        const bool ring = f.schedule().ring_buffer().defined();
        const bool splits = func_has_storage_splits(f);
        user_assert(!(splits && ring))
            << "split_storage cannot currently be combined with ring_buffer() (" << op->name << ").\n";

        const vector<StorageAxis> layout = storage_layout(f, ring);
        const int n = (int)layout.size();
        const int num_args = (int)f.args().size();

        // The buffer dimension each axis lives in: its pure arg (so the buffer
        // stays in arg order) when there are no splits, else its storage
        // position. With no splits n == op->bounds.size() and this is a
        // permutation of the pure args, just as before.
        auto buffer_dim = [&](int p) { return splits ? p : layout[p].arg; };

        // The extent placeholders resolve to the (mutated) per-arg extents.
        map<string, Expr> ext_subs;
        for (int j = 0; j < (int)op->bounds.size(); j++) {
            ext_subs["__ss_ext." + std::to_string(j)] = extents[j];
        }

        // The stored extent (taking bound_storage into account) and the
        // allocated extent (also taking align_storage into account) of each
        // buffer dimension.
        vector<Expr> stored_extent(n), alloc_extent(n);
        vector<Stmt> bound_asserts;
        for (int p = 0; p < n; p++) {
            int d = buffer_dim(p);
            Expr e = substitute(ext_subs, layout[p].extent);
            if (layout[p].bound.defined()) {
                Expr bound = layout[p].bound;
                if (can_prove(e > bound)) {
                    user_error << "Explicit storage bound (" << bound << ") for variable "
                               << layout[p].var << " of function " << op->name
                               << " is smaller than required (" << e << ")\n";
                }
                Expr err =
                    Call::make(Int(32), "halide_error_storage_bound_too_small",
                               {StringImm::make(op->name), StringImm::make(layout[p].var), bound, e},
                               Call::Extern);
                bound_asserts.push_back(AssertStmt::make(e <= bound, err));
                stored_extent[d] = bound;
            } else {
                stored_extent[d] = e;
            }
            if (layout[p].alignment.defined()) {
                Expr a = layout[p].alignment;
                alloc_extent[d] = ((stored_extent[d] + a - 1) / a) * a;
            } else {
                alloc_extent[d] = stored_extent[d];
            }
        }

        // Names and vars for the buffer's mins, extents and strides.
        vector<string> min_name(n), extent_name(n), stride_name(n);
        vector<Expr> min_var(n), extent_var(n), stride_var(n);
        for (int d = 0; d < n; d++) {
            string ds = std::to_string(d);
            min_name[d] = op->name + ".min." + ds;
            extent_name[d] = op->name + ".extent." + ds;
            stride_name[d] = op->name + ".stride." + ds;
            min_var[d] = Variable::make(Int(32), min_name[d]);
            extent_var[d] = Variable::make(Int(32), extent_name[d]);
            stride_var[d] = Variable::make(Int(32), stride_name[d]);
        }

        // Create a halide_buffer_t object for this allocation. When there are
        // splits the buffer is in storage order and addressed with relative
        // coordinates, so its mins are zero; otherwise it is in arg order with
        // the args' mins, exactly as before.
        BufferBuilder builder;
        builder.host = Variable::make(Handle(), op->name);
        builder.type = op->types[0];
        builder.dimensions = n;
        for (int d = 0; d < n; d++) {
            builder.mins.push_back(min_var[d]);
            builder.extents.push_back(extent_var[d]);
            builder.strides.push_back(stride_var[d]);
        }

        Stmt stmt = body;
        stmt = LetStmt::make(op->name + ".buffer", builder.build(), stmt);
        stmt = Allocate::make(op->name, op->types[0], op->memory_type, alloc_extent, condition, stmt);
        if (!bound_asserts.empty()) {
            stmt = Block::make(Block::make(bound_asserts), stmt);
        }

        // Strides, innermost (layout position 0) first.
        for (int p = n - 1; p > 0; p--) {
            stmt = LetStmt::make(stride_name[buffer_dim(p)],
                                 stride_var[buffer_dim(p - 1)] * alloc_extent[buffer_dim(p - 1)], stmt);
        }
        if (n > 0) {
            stmt = LetStmt::make(stride_name[buffer_dim(0)], 1, stmt);
        }

        // Mins and extents of each buffer dimension.
        for (int p = n - 1; p >= 0; p--) {
            int d = buffer_dim(p);
            Expr min = splits ? Expr(0) : op->bounds[layout[p].arg].min;
            stmt = LetStmt::make(min_name[d], min, stmt);
            stmt = LetStmt::make(extent_name[d], stored_extent[d], stmt);
        }

        // Split funcs address the buffer with relative coordinates, so the
        // per-arg mins used by flatten_args are bound separately.
        if (splits) {
            for (int j = num_args; j > 0; j--) {
                stmt = LetStmt::make(op->name + ".arg_min." + std::to_string(j - 1),
                                     op->bounds[j - 1].min, stmt);
            }
        }

        return stmt;
    }

    Stmt visit(const StreamingStore *op) override {
        ScopedValue old_in_streaming_store(in_streaming_store, true);
        return mutate(op->body);
    }

    Stmt visit(const StreamingLoads *op) override {
        std::optional<std::set<std::string>> requested;
        if (op->names) {
            requested = std::set(op->names->begin(), op->names->end());
        }
        ScopedValue old_names(stream_loads_names, requested);
        std::set<std::string> matched;
        ScopedValue old_matched(stream_loads_matched, op->names ? &matched : nullptr);

        Stmt result = mutate(op->body);

        if (requested) {
            for (const std::string &requested_name : *requested) {
                if (!matched.count(requested_name)) {
                    user_warning << "stream_loads({" << requested_name << "}) was requested, "
                                 << "but no direct load of \"" << requested_name
                                 << "\" was found; did you mean to call stream_loads on a "
                                    "different Stage?\n";
                }
            }
        }
        return result;
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

        ScopedValue old_provide_name(current_provide_name, op->name);

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
            return Store::make(op->name, value, idx, output_buf, predicate, ModulusRemainder(), in_streaming_store);
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
                // successive passes.
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
                bool is_streaming = false;
                if ((op->call_type == Call::Halide || op->param.defined()) &&
                    op->name != current_provide_name) {
                    bool matches = !stream_loads_names || stream_loads_names->count(op->name) != 0;
                    is_streaming |= matches;
                    if (matches && stream_loads_matched) {
                        stream_loads_matched->insert(op->name);
                    }
                }
                return Load::make(op->type, op->name, idx, op->image, op->param,
                                  const_true(op->type.lanes()), ModulusRemainder(), is_streaming);
            }

        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Prefetch *op) override {
        internal_assert(op->types.size() == 1)
            << "Prefetch from multi-dimensional halide tuple should have been split\n";

        {
            auto iter = env.find(op->name);
            if (iter != env.end()) {
                user_assert(!func_has_storage_splits(iter->second.first))
                    << "prefetch is not supported for functions with split_storage (" << op->name << ").\n";
            }
        }

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
        ScopedValue old_in_gpu(in_gpu, in_gpu || is_gpu(op->for_type));
        return IRMutator::visit(op);
    }
};

class HoistStorage : public IRMutator {
protected:
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
        vector<pair<string, Interval>> loop_vars;
        Scope<Expr> scope;

        HoistedStorageData(const string &n)
            : name(n) {
        }
    };

    vector<HoistedStorageData> hoisted_storages;
    map<string, int> hoisted_storages_map;

    // Perform all the substitutions in a scope
    static Expr expand_expr(const Expr &e, const Scope<Expr> &scope) {
        Expr result = mutate_with(
            e,
            [&](auto *self, const Variable *var) -> Expr {
                if (const Expr *value = scope.find(var->name)) {
                    // Mutate the expression, so lets can get replaced recursively.
                    Expr expr = self->mutate(*value);
                    debug(4) << "Fully expanded " << var->name << " -> " << expr << "\n";
                    return expr;
                }
                return var;
            });
        debug(4) << "Expanded " << e << " into " << result << "\n";
        return result;
    }

    using IRMutator::visit;

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

    Stmt visit(const HoistedStorage *op) override {
        hoisted_storages.emplace_back(op->name);
        // Record index in the stack.
        hoisted_storages_map[op->name] = hoisted_storages.size() - 1;
        Stmt body = mutate(op->body);
        if (hoisted_storages.back().hoisted_allocations.empty()) {
            // Nothing in here allocates it. A Func hoisted into the loops of a
            // Func with update definitions gets one of these nodes per stage,
            // but is only realized inside the stages that use it.
            hoisted_storages_map.erase(op->name);
            hoisted_storages.pop_back();
            return body;
        }
        const auto &alloc_info = hoisted_storages.back().hoisted_allocations.front();
        vector<Expr> extents = alloc_info.extents;
        Expr condition = alloc_info.condition;
        for (int i = 1; i < (int)hoisted_storages.back().hoisted_allocations.size(); i++) {
            const auto &ai = hoisted_storages.back().hoisted_allocations[i];
            internal_assert(ai.extents.size() == alloc_info.extents.size());
            for (int j = 0; j < (int)extents.size(); j++) {
                extents[j] = Max::make(extents[j], ai.extents[j]);
            }
            condition = condition || ai.condition;
        }
        body = Allocate::make(alloc_info.name, alloc_info.type, alloc_info.memory_type, extents, condition, body);
        hoisted_storages_map.erase(op->name);
        hoisted_storages.pop_back();
        return body;
    }

    Stmt visit(const Allocate *op) override {
        if (auto it = hoisted_storages_map.find(op->name);
            it != hoisted_storages_map.end()) {
            HoistedStorageData &hoisted_storage_data = hoisted_storages[it->second];

            auto expand_and_bound = [&](Expr e) {
                // Iterate from innermost outwards
                for (const auto &storage : reverse_view(hoisted_storages)) {
                    e = expand_expr(e, storage.scope);
                    if (storage.name == op->name) {
                        break;
                    }
                }

                e = simplify(common_subexpression_elimination(e));
                // Find bounds of expression using the intervals of the loop
                // variables. The loop variables may depend on the other loop
                // variables, so we just call bounds_of_expr_in_scope for each
                // loop variable separately in a reverse order.
                for (const auto &[var, interval] : reverse_view(hoisted_storage_data.loop_vars)) {
                    Scope<Interval> one_loop_var;
                    one_loop_var.push(var, interval);
                    Interval bounds = bounds_of_expr_in_scope(e, one_loop_var);
                    e = bounds.max;
                }

                return e;
            };

            vector<Expr> bounded_extents;
            for (const auto &e : op->extents) {
                Expr expanded_extent = expand_and_bound(e);
                user_assert(expanded_extent.defined() &&
                            !expanded_extent.same_as(Interval::pos_inf()))
                    << "Couldn't infer the upper bound for the storage size of " << op->name << ", consider using bound_storage.\n";
                bounded_extents.push_back(expanded_extent);
            }

            Expr expanded_condition = expand_and_bound(op->condition);
            if (!expanded_condition.defined() ||
                expanded_condition.same_as(Interval::pos_inf())) {
                expanded_condition = const_true();
            }

            HoistedAllocationInfo hoisted_alloc(op->name, op->type, op->memory_type, bounded_extents, expanded_condition);

            hoisted_storage_data.hoisted_allocations.push_back(hoisted_alloc);
            return mutate(op->body);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const For *op) override {
        Expr expanded_min = op->min;
        Expr expanded_max = op->max;
        // Iterate from innermost outwards
        for (auto &storage : reverse_view(hoisted_storages)) {
            expanded_min = simplify(expand_expr(expanded_min, storage.scope));
            expanded_max = expand_expr(expanded_max, storage.scope);
            auto loop_bounds = Interval(expanded_min, expanded_max);
            storage.loop_vars.emplace_back(op->name, loop_bounds);
        }

        Stmt stmt = IRMutator::visit(op);

        for (auto &p : hoisted_storages) {
            p.loop_vars.pop_back();
        }

        return stmt;
    }
};

// Realizations, stores, and loads must all be on types that are
// multiples of 8-bits. This really only affects bools
class PromoteToMemoryType : public IRMutator {
protected:
    using IRMutator::visit;

    Type upgrade(Type t) {
        return t.with_bits(((t.bits() + 7) / 8) * 8);
    }

    Expr visit(const Load *op) override {
        Type t = upgrade(op->type);
        if (t != op->type) {
            return Cast::make(op->type,
                              Load::make(t, op->name, mutate(op->index),
                                         op->image, op->param, mutate(op->predicate), ModulusRemainder(), op->is_streaming));
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        Type t = upgrade(op->value.type());
        if (t != op->value.type()) {
            return op->with(Cast::make(t, mutate(op->value)), mutate(op->index),
                            mutate(op->predicate), ModulusRemainder());
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

// Strips the markers mark_specialization_branch() (ScheduleFunctions.cpp)
// brackets each specialize() branch with. Safe here because by the time
// storage flattening has run, sibling branches have diverged for real (e.g.
// a stride-1 specialization now has literal contiguous Store/Load nodes
// where the generic fallback has stride-multiplied ones).
Stmt remove_specialization_branch_markers(const Stmt &s) {
    return mutate_with(s, [](auto *self, const Call *op) -> Expr {
        if (op->is_intrinsic(Call::specialization_branch_marker)) {
            return make_zero(op->type);
        }
        return self->visit_base(op);
    });
}

}  // namespace

Stmt storage_flattening(Stmt s,
                        const vector<Function> &outputs,
                        const map<string, Function> &env,
                        const Target &target) {
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
    s = FlattenDimensions(tuple_env, outputs, target)(s);
    s = HoistStorage()(s);
    s = PromoteToMemoryType()(s);
    s = remove_specialization_branch_markers(s);

    return s;
}

}  // namespace Internal
}  // namespace Halide
