#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "Bounds.h"
#include "ExprUsesVar.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Prefetch.h"
#include "Scope.h"
#include "Simplify.h"
#include "Target.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

/**
 * The steps by which prefetch directives are injected and lowered are a bit nonobvious;
 * here's the overall flow at the time this comment was written:
 *
 *  - When the .prefetch() schedule directive is used, a PrefetchDirective is
 *    added to the relevant Function
 *  - At the start of lowering (schedule_functions()), a placeholder Prefetch IR nodes (w/ no region) are inserted
 *  - Various lowering passes mutate the placeholder Prefetch IR nodes as appropriate
 *  - After storage folding, the Prefetch IR nodes are updated with a proper region (via inject_prefetch())
 *  - In storage_flattening(), Prefetch IR nodes transformed to Call::prefetch intrinsics:
 *    - These calls are always wrapped in Evaluate IR Nodes
 *    - The Evaluate IR nodes are, in turn, possibly in IfTheElse (if condition != const_true())
 *    - After this point, no Prefetch IR nodes should remain in the IR (only prefetch intrinsics)
 *  - reduce_prefetch_dimension() is later called to reduce prefetch the dimensionality of the prefetch intrinsic.
 */

namespace {

// Collect the bounds of all the externally referenced buffers in a stmt.
class CollectExternalBufferBounds : public IRVisitor {
public:
    map<string, Box> buffers;

    using IRVisitor::visit;

    void add_buffer_bounds(const string &name, const Buffer<> &image, const Parameter &param, int dims) {
        Box b;
        for (int i = 0; i < dims; ++i) {
            string dim_name = std::to_string(i);
            Expr buf_min_i = Variable::make(Int(32), name + ".min." + dim_name,
                                            image, param, ReductionDomain());
            Expr buf_extent_i = Variable::make(Int(32), name + ".extent." + dim_name,
                                               image, param, ReductionDomain());
            Expr buf_max_i = buf_min_i + buf_extent_i - 1;
            b.push_back(Interval(buf_min_i, buf_max_i));
        }
        buffers.emplace(name, b);
    }

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        add_buffer_bounds(op->name, op->image, op->param, (int)op->args.size());
    }

    void visit(const Variable *op) override {
        if (op->param.defined() && op->param.is_buffer()) {
            add_buffer_bounds(op->name, Buffer<>(), op->param, op->param.dimensions());
        }
    }
};

class InjectPrefetch : public IRMutator {
public:
    InjectPrefetch(const map<string, Function> &e, const map<string, Box> &buffers)
        : env(e), external_buffers(buffers) {
    }

private:
    const map<string, Function> &env;
    const map<string, Box> &external_buffers;
    Scope<Box> buffer_bounds;

    using IRMutator::visit;

    Box get_buffer_bounds(const string &name, int dims) {
        if (buffer_bounds.contains(name)) {
            const Box &b = buffer_bounds.ref(name);
            internal_assert((int)b.size() == dims);
            return b;
        }

        // It is an external buffer.
        user_assert(env.find(name) == env.end())
            << "Prefetch to buffer \"" << name << "\" which has not been allocated\n";

        const auto &iter = external_buffers.find(name);
        internal_assert(iter != external_buffers.end());
        return iter->second;
    }

    Stmt visit(const Realize *op) override {
        Box b;
        b.used = op->condition;
        for (const auto &r : op->bounds) {
            b.push_back(Interval(r.min, r.min + r.extent - 1));
        }
        ScopedBinding<Box> bind(buffer_bounds, op->name, b);
        return IRMutator::visit(op);
    }

    Stmt visit(const Prefetch *op) override {
        Stmt body = mutate(op->body);

        const PrefetchDirective &p = op->prefetch;
        Expr at = Variable::make(Int(32), p.at);
        Expr from = Variable::make(Int(32), p.from);

        // Add loop variable + prefetch offset to interval scope for box computation
        Expr fetch_at = from + p.offset;
        map<string, Box> boxes_rw = boxes_touched(LetStmt::make(p.from, fetch_at, body));

        // TODO(psuriana): Only prefetch the newly accessed data. We
        // should subtract the box accessed during previous iteration
        // from the one accessed during this iteration.

        // TODO(psuriana): Add a new PrefetchBoundStrategy::ShiftInwards
        // that shifts the base address of the prefetched box so that
        // the box is completely within the bounds.
        const auto &b = boxes_rw.find(p.name);
        if (b != boxes_rw.end()) {
            Box prefetch_box = b->second;
            // Only prefetch the region that is in bounds.
            Box bounds = get_buffer_bounds(b->first, b->second.size());
            internal_assert(prefetch_box.size() == bounds.size());

            if (p.strategy == PrefetchBoundStrategy::Clamp) {
                prefetch_box = box_intersection(prefetch_box, bounds);
            } else if (p.strategy == PrefetchBoundStrategy::GuardWithIf) {
                Expr predicate = prefetch_box.used.defined() ? prefetch_box.used : const_true();
                for (size_t i = 0; i < bounds.size(); ++i) {
                    predicate = predicate && (prefetch_box[i].min >= bounds[i].min) &&
                                (prefetch_box[i].max <= bounds[i].max);
                }
                prefetch_box.used = simplify(predicate);
            } else {
                internal_assert(p.strategy == PrefetchBoundStrategy::NonFaulting);
                // Assume the prefetch won't fault when accessing region
                // outside the bounds.
            }

            // Construct the region to be prefetched.
            Region new_bounds;
            for (size_t i = 0; i < prefetch_box.size(); i++) {
                Expr extent = prefetch_box[i].max - prefetch_box[i].min + 1;
                new_bounds.emplace_back(simplify(prefetch_box[i].min), simplify(extent));
            }
            Expr condition = op->condition;
            if (prefetch_box.maybe_unused()) {
                condition = simplify(prefetch_box.used && condition);
            }
            internal_assert(!new_bounds.empty());
            return Prefetch::make(op->name, op->types, new_bounds, op->prefetch, std::move(condition), std::move(body));
        }

        if (!body.same_as(op->body)) {
            return Prefetch::make(op->name, op->types, op->bounds, op->prefetch, op->condition, std::move(body));
        } else if (op->bounds.empty()) {
            // Remove the Prefetch IR since it is prefetching an empty region
            user_warning << "Removing prefetch of " << p.name
                         << " at loop nest of " << p.at
                         << " from location " << p.from
                         << " + offset " << p.offset
                         << ") since the prefetched area will always be empty.\n";
            return body;
        } else {
            return op;
        }
    }
};

class InjectPlaceholderPrefetch : public IRMutator {
public:
    InjectPlaceholderPrefetch(const map<string, Function> &e, const string &prefix,
                              const vector<PrefetchDirective> &prefetches)
        : env(e), prefix(prefix), prefetch_list(prefetches) {
    }

private:
    const map<string, Function> &env;
    const string &prefix;
    const vector<PrefetchDirective> &prefetch_list;
    std::vector<string> loop_nest;

    using IRMutator::visit;

    Stmt add_placeholder_prefetch(const string &at, const string &from, PrefetchDirective p, Stmt body) {
        debug(5) << "...Injecting placeholder prefetch for loop " << at << " from " << from << "\n";
        p.at = at;
        p.from = from;
        internal_assert(body.defined());
        if (p.param.defined()) {
            return Prefetch::make(p.name, {p.param.type()}, Region(), p, const_true(), std::move(body));
        } else {
            const auto &it = env.find(p.name);
            internal_assert(it != env.end());
            return Prefetch::make(p.name, it->second.output_types(), Region(), p, const_true(), std::move(body));
        }
    }

    Stmt visit(const For *op) override {
        loop_nest.push_back(op->name);

        Stmt body = mutate(op->body);

        if (!prefetch_list.empty() && starts_with(op->name, prefix)) {
            // If there are multiple prefetches of the same Func or ImageParam,
            // use the most recent one
            set<string> seen;
            for (int i = prefetch_list.size() - 1; i >= 0; --i) {
                const PrefetchDirective &p = prefetch_list[i];
                if (!ends_with(op->name, "." + p.at) || (seen.find(p.name) != seen.end())) {
                    continue;
                }
                seen.insert(p.name);

                // We pass op->name for the prefetch 'at', so that should always be a fully-qualified loop variable name
                // at this point; however, 'from' will be just the left-name of the loop var and must be qualified further.
                // Look through the loop_nest list to find the most recent loop that starts with 'prefix' and ends with 'from'.
                // Note that it is not good enough to just prepend use 'prefix + from', as there may be splits involved, e.g.,
                // prefix = g.s0, from = xo, but the var we seek is actually g.s0.x.xo (because 'g' was split at x).
                string from_var;
                for (int j = (int)loop_nest.size() - 1; j >= 0; --j) {
                    if (starts_with(loop_nest[j], prefix) && ends_with(loop_nest[j], "." + p.from)) {
                        from_var = loop_nest[j];
                        debug(5) << "Prefetch from " << p.from << " -> from_var " << from_var << "\n";
                        break;
                    }
                }
                if (from_var.empty()) {
                    user_error << "Prefetch 'from' variable '" << p.from << "' could not be found in an active loop. (Are the 'at' and 'from' variables swapped?)";
                }
                body = add_placeholder_prefetch(op->name, from_var, p, std::move(body));
            }
        }

        Stmt stmt;
        if (!body.same_as(op->body)) {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, std::move(body));
        } else {
            stmt = op;
        }

        internal_assert(loop_nest.back() == op->name);
        loop_nest.pop_back();
        return stmt;
    }
};

// Reduce the prefetch dimension if bigger than 'max_dim'. It keeps the 'max_dim'
// innermost dimensions and replaces the rests with for-loops.
class ReducePrefetchDimension : public IRMutator {
    using IRMutator::visit;

    const size_t max_dim;

    Stmt visit(const Evaluate *op) override {
        Stmt stmt = IRMutator::visit(op);
        op = stmt.as<Evaluate>();
        internal_assert(op);
        const Call *prefetch = Call::as_intrinsic(op->value, {Call::prefetch});

        // TODO(psuriana): Ideally, we want to keep the loop size minimal to
        // minimize the number of prefetch calls. We probably want to lift
        // the dimensions with larger strides and keep the smaller ones in
        // the prefetch call.

        const size_t max_arg_size = 2 + 2 * max_dim;  // Prefetch: {base, offset, extent0, stride0, extent1, stride1, ...}
        if (prefetch && (prefetch->args.size() > max_arg_size)) {
            const Expr &base_address = prefetch->args[0];
            const Expr &base_offset = prefetch->args[1];

            const Variable *base = base_address.as<Variable>();
            internal_assert(base && base->type.is_handle());

            vector<string> index_names;
            Expr new_offset = base_offset;
            for (size_t i = max_arg_size; i < prefetch->args.size(); i += 2) {
                // const Expr &extent = prefetch->args[i + 0];  // unused
                const Expr &stride = prefetch->args[i + 1];
                string index_name = "prefetch_reduce_" + base->name + "." + std::to_string((i - 1) / 2);
                index_names.push_back(index_name);
                new_offset += Variable::make(Int(32), index_name) * stride;
            }

            vector<Expr> args = {base, new_offset};
            for (size_t i = 2; i < max_arg_size; ++i) {
                args.push_back(prefetch->args[i]);
            }

            stmt = Evaluate::make(Call::make(prefetch->type, Call::prefetch, args, Call::Intrinsic));
            for (size_t i = 0; i < index_names.size(); ++i) {
                stmt = For::make(index_names[i], 0, prefetch->args[(i + max_dim) * 2 + 2],
                                 ForType::Serial, DeviceAPI::None, stmt);
            }
            debug(5) << "\nReduce prefetch to " << max_dim << " dim:\n"
                     << "Before:\n"
                     << Expr(prefetch) << "\nAfter:\n"
                     << stmt << "\n";
        }
        return stmt;
    }

public:
    ReducePrefetchDimension(size_t dim)
        : max_dim(dim) {
    }
};

// If the prefetched data is larger than 'max_byte_size', we need to tile the
// prefetch. This will split the prefetch call into multiple calls by adding
// an outer for-loop around the prefetch.
class SplitPrefetch : public IRMutator {
    using IRMutator::visit;

    Expr max_byte_size;

    Stmt visit(const Evaluate *op) override {
        Stmt stmt = IRMutator::visit(op);
        op = stmt.as<Evaluate>();
        internal_assert(op);
        if (const Call *prefetch = Call::as_intrinsic(op->value, {Call::prefetch})) {
            const Expr &base_address = prefetch->args[0];
            const Expr &base_offset = prefetch->args[1];

            const Variable *base = base_address.as<Variable>();
            internal_assert(base && base->type.is_handle());

            int elem_size = prefetch->type.bytes();

            vector<string> index_names;
            vector<Expr> extents;
            Expr new_offset = base_offset;
            for (size_t i = 2; i < prefetch->args.size(); i += 2) {
                Expr extent = prefetch->args[i];
                Expr stride = prefetch->args[i + 1];
                Expr stride_bytes = stride * elem_size;

                string index_name = "prefetch_split_" + base->name + "." + std::to_string((i - 1) / 2);
                index_names.push_back(index_name);

                Expr is_negative_stride = (stride < 0);
                Expr outer_var = Variable::make(Int(32), index_name);
                Expr outer_extent;
                if (can_prove(max_byte_size < stride_bytes) || can_prove(max_byte_size < -stride_bytes)) {
                    // If 'max_byte_size' is smaller than the absolute value of the
                    // stride bytes, we can only prefetch one element per iteration.
                    outer_extent = extent;
                    new_offset += outer_var * stride_bytes;
                } else {
                    // Otherwise, we just prefetch 'max_byte_size' per iteration.
                    Expr abs_stride_bytes = Call::make(stride_bytes.type(), Call::abs, {stride_bytes}, Call::PureIntrinsic);
                    outer_extent = simplify((extent * abs_stride_bytes + max_byte_size - 1) / max_byte_size);
                    new_offset += outer_var * simplify(select(is_negative_stride, -max_byte_size, max_byte_size));
                }
                extents.push_back(outer_extent);
            }

            Expr new_extent = 1;
            Expr new_stride = simplify(max_byte_size / elem_size);
            vector<Expr> args = {base, std::move(new_offset), std::move(new_extent), std::move(new_stride)};
            stmt = Evaluate::make(Call::make(prefetch->type, Call::prefetch, args, Call::Intrinsic));
            for (size_t i = 0; i < index_names.size(); ++i) {
                stmt = For::make(index_names[i], 0, extents[i],
                                 ForType::Serial, DeviceAPI::None, stmt);
            }
            debug(5) << "\nSplit prefetch to max of " << max_byte_size << " bytes:\n"
                     << "Before:\n"
                     << Expr(prefetch) << "\nAfter:\n"
                     << stmt << "\n";
        }
        return stmt;
    }

public:
    SplitPrefetch(Expr bytes)
        : max_byte_size(std::move(bytes)) {
    }
};

template<typename Fn>
void traverse_block(const Stmt &s, Fn &&f) {
    const Block *b = s.as<Block>();
    if (!b) {
        f(s);
    } else {
        traverse_block(b->first, f);
        traverse_block(b->rest, f);
    }
}

class HoistPrefetches : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Block *op) override {
        Stmt s = op;

        Stmt prefetches, body;
        traverse_block(s, [this, &prefetches, &body](const Stmt &s_in) {
            Stmt s = IRMutator::mutate(s_in);
            const Evaluate *eval = s.as<Evaluate>();
            if (eval && Call::as_intrinsic(eval->value, {Call::prefetch})) {
                prefetches = prefetches.defined() ? Block::make(prefetches, s) : s;
            } else {
                body = body.defined() ? Block::make(body, s) : s;
            }
        });
        if (prefetches.defined()) {
            if (body.defined()) {
                return Block::make(prefetches, body);
            } else {
                return prefetches;
            }
        } else {
            return body;
        }
    }
};

}  // anonymous namespace

Stmt inject_placeholder_prefetch(const Stmt &s, const map<string, Function> &env,
                                 const string &prefix,
                                 const vector<PrefetchDirective> &prefetches) {
    Stmt stmt = InjectPlaceholderPrefetch(env, prefix, prefetches).mutate(s);
    return stmt;
}

Stmt inject_prefetch(const Stmt &s, const map<string, Function> &env) {
    CollectExternalBufferBounds finder;
    s.accept(&finder);
    return InjectPrefetch(env, finder.buffers).mutate(s);
}

Stmt reduce_prefetch_dimension(Stmt stmt, const Target &t) {
    size_t max_dim = 0;
    Expr max_byte_size;

    // Hexagon's prefetch takes in a range of address and can be maximum of
    // two dimension. Other architectures generate one prefetch per cache line.
    if (t.has_feature(Target::HVX)) {
        max_dim = 2;
    } else if (t.arch == Target::ARM) {
        // ARM's cache line size can be 32 or 64 bytes and it can switch the
        // size at runtime. To be safe, we just use 32 bytes.
        max_dim = 1;
        max_byte_size = 32;
    } else {
        max_dim = 1;
        max_byte_size = 64;
    }
    internal_assert(max_dim > 0);

    stmt = ReducePrefetchDimension(max_dim).mutate(stmt);
    if (max_byte_size.defined()) {
        // If the max byte size is specified, we may need to tile
        // the prefetch
        stmt = SplitPrefetch(max_byte_size).mutate(stmt);
    }
    return stmt;
}

Stmt hoist_prefetches(const Stmt &s) {
    return HoistPrefetches().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
