#include <algorithm>
#include <map>
#include <string>

#include "Prefetch.h"
#include "Bounds.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Simplify.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {

const Definition &get_stage_definition(const Function &f, int stage_num) {
    if (stage_num == 0) {
        return f.definition();
    }
    return f.update(stage_num - 1);
}

// Collect the bounds of all the externally referenced buffers in a stmt.
class CollectExternalBufferBounds : public IRVisitor {
public:
    map<string, Box> buffers;

    using IRVisitor::visit;

    void add_buffer_bounds(const string &name, Buffer<> image, Parameter param, int dims) {
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

    void visit(const Call *op) {
        IRVisitor::visit(op);
        add_buffer_bounds(op->name, op->image, op->param, (int)op->args.size());
    }

    void visit(const Variable *op) {
        if (op->param.defined() && op->param.is_buffer()) {
            add_buffer_bounds(op->name, Buffer<>(), op->param, op->param.dimensions());
        }
    }
};

class InjectPrefetch : public IRMutator2 {
public:
    InjectPrefetch(const map<string, Function> &e, const map<string, Box> &buffers)
        : env(e), external_buffers(buffers), current_func(nullptr), stage(-1) { }

private:
    const map<string, Function> &env;
    const map<string, Box> &external_buffers;
    const Function *current_func;
    int stage;
    Scope<Box> buffer_bounds;

private:
    using IRMutator2::visit;

    const vector<PrefetchDirective> &get_prefetch_list(const string &loop_name) {
        if (!current_func || !starts_with(loop_name, current_func->name() + ".s" + std::to_string(stage))) {
            vector<string> v = split_string(loop_name, ".");
            internal_assert(v.size() > 2);
            const string &func_name = v[0];

            // Get the stage index
            stage = -1;
            internal_assert(v[1].substr(0, 1) == "s");
            {
                string str = v[1].substr(1, v[1].size() - 1);
                bool has_only_digits = (str.find_first_not_of("0123456789") == string::npos);
                internal_assert(has_only_digits);
                stage = atoi(str.c_str());
            }
            internal_assert(stage >= 0);

            const auto &it = env.find(func_name);
            internal_assert(it != env.end());

            current_func = &it->second;
            internal_assert(stage <= (int)current_func->updates().size());
        }

        const Definition &def = get_stage_definition(*current_func, stage);
        return def.schedule().prefetches();
    }

    Box get_buffer_bounds(string name, int dims) {
        if (buffer_bounds.contains(name)) {
            const Box &b = buffer_bounds.ref(name);
            internal_assert((int)b.size() == dims);
            return b;
        }

        // It is an external buffer.
        user_assert(env.find(name) == env.end())
            << "Prefetch to buffer \"" << name << "\" which has not been allocated\n" ;

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
        return IRMutator2::visit(op);
    }

    Stmt add_prefetch(string buf_name, const Parameter &param, const Box &box, Stmt body) {
        // Construct the region to be prefetched.
        Region bounds;
        for (size_t i = 0; i < box.size(); i++) {
            Expr extent = box[i].max - box[i].min + 1;
            bounds.push_back(Range(box[i].min, extent));
        }

        Stmt prefetch;
        if (param.defined()) {
            prefetch = Prefetch::make(buf_name, {param.type()}, bounds, param);
        } else {
            const auto &it = env.find(buf_name);
            internal_assert(it != env.end());
            prefetch = Prefetch::make(buf_name, it->second.output_types(), bounds);
        }

        if (box.maybe_unused()) {
            prefetch = IfThenElse::make(box.used, prefetch);
        }
        return Block::make({prefetch, body});
    }

    Stmt visit(const For *op) override {
        const Function *old_func = current_func;
        int old_stage = stage;

        const vector<PrefetchDirective> &prefetch_list = get_prefetch_list(op->name);

        Expr loop_var = Variable::make(Int(32), op->name);
        Stmt body = mutate(op->body);

        if (!prefetch_list.empty()) {
            // If there are multiple prefetches of the same Func or ImageParam,
            // use the most recent one
            set<string> seen;
            for (int i = prefetch_list.size() - 1; i >= 0; --i) {
                const PrefetchDirective &p = prefetch_list[i];
                if (!ends_with(op->name, "." + p.var) || (seen.find(p.name) != seen.end())) {
                    continue;
                }
                seen.insert(p.name);

                // Add loop variable + prefetch offset to interval scope for box computation
                Expr fetch_at = loop_var + p.offset;
                map<string, Box> boxes_rw = boxes_touched(LetStmt::make(op->name, fetch_at, body));

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
                    body = add_prefetch(b->first, p.param, prefetch_box, body);
                }
            }
        }

        Stmt stmt;
        if (!body.same_as(op->body)) {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
        } else {
            stmt = op;
        }

        current_func = old_func;
        stage = old_stage;
        return stmt;
    }
};

// Reduce the prefetch dimension if bigger than 'max_dim'. It keeps the 'max_dim'
// innermost dimensions and replaces the rests with for-loops.
class ReducePrefetchDimension : public IRMutator2 {
    using IRMutator2::visit;

    size_t max_dim;

    Stmt visit(const Evaluate *op) override {
        Stmt stmt = IRMutator2::visit(op);
        op = stmt.as<Evaluate>();
        internal_assert(op);
        const Call *call = op->value.as<Call>();

        // TODO(psuriana): Ideally, we want to keep the loop size minimal to
        // minimize the number of prefetch calls. We probably want to lift
        // the dimensions with larger strides and keep the smaller ones in
        // the prefetch call.

        size_t max_arg_size = 2 + 2 * max_dim; // Prefetch: {base, offset, extent0, stride0, extent1, stride1, ...}
        if (call && call->is_intrinsic(Call::prefetch) && (call->args.size() > max_arg_size)) {
            const Variable *base = call->args[0].as<Variable>();
            internal_assert(base && base->type.is_handle());

            vector<string> index_names;
            Expr new_offset = call->args[1];
            for (size_t i = max_arg_size; i < call->args.size(); i += 2) {
                Expr stride = call->args[i+1];
                string index_name = "prefetch_reduce_" + base->name + "." + std::to_string((i-1)/2);
                index_names.push_back(index_name);
                new_offset += Variable::make(Int(32), index_name) * stride;
            }

            vector<Expr> args = {base, new_offset};
            for (size_t i = 2; i < max_arg_size; ++i) {
                args.push_back(call->args[i]);
            }

            stmt = Evaluate::make(Call::make(call->type, Call::prefetch, args, Call::Intrinsic));
            for (size_t i = 0; i < index_names.size(); ++i) {
                stmt = For::make(index_names[i], 0, call->args[(i+max_dim)*2 + 2],
                                 ForType::Serial, DeviceAPI::None, stmt);
            }
            debug(5) << "\nReduce prefetch to " << max_dim << " dim:\n"
                     << "Before:\n" << Expr(call) << "\nAfter:\n" << stmt << "\n";
        }
        return stmt;
    }

public:
    ReducePrefetchDimension(size_t dim) : max_dim(dim) {}
};

// If the prefetched data is larger than 'max_byte_size', we need to tile the
// prefetch. This will split the prefetch call into multiple calls by adding
// an outer for-loop around the prefetch.
class SplitPrefetch : public IRMutator2 {
    using IRMutator2::visit;

    Expr max_byte_size;

    Stmt visit(const Evaluate *op) override {
        Stmt stmt = IRMutator2::visit(op);
        op = stmt.as<Evaluate>();
        internal_assert(op);
        const Call *call = op->value.as<Call>();

        if (call && call->is_intrinsic(Call::prefetch)) {
            const Variable *base = call->args[0].as<Variable>();
            internal_assert(base && base->type.is_handle());

            int elem_size = base->type.bytes();

            vector<string> index_names;
            vector<Expr> extents;
            Expr new_offset = call->args[1];
            for (size_t i = 2; i < call->args.size(); i += 2) {
                Expr extent = call->args[i];
                Expr stride = call->args[i+1];
                Expr stride_bytes = stride * elem_size;

                string index_name = "prefetch_split_" + base->name + "." + std::to_string((i-1)/2);
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
                    outer_extent = simplify((extent * abs_stride_bytes + max_byte_size - 1)/max_byte_size);
                    new_offset += outer_var * simplify(select(is_negative_stride, -max_byte_size, max_byte_size));
                }
                extents.push_back(outer_extent);
            }

            vector<Expr> args = {base, new_offset, Expr(1), simplify(max_byte_size / elem_size)};
            stmt = Evaluate::make(Call::make(call->type, Call::prefetch, args, Call::Intrinsic));
            for (size_t i = 0; i < index_names.size(); ++i) {
                stmt = For::make(index_names[i], 0, extents[i],
                                 ForType::Serial, DeviceAPI::None, stmt);
            }
            debug(5) << "\nSplit prefetch to max of " << max_byte_size << " bytes:\n"
                     << "Before:\n" << Expr(call) << "\nAfter:\n" << stmt << "\n";
        }
        return stmt;
    }

public:
    SplitPrefetch(Expr bytes) : max_byte_size(bytes) {}
};

} // anonymous namespace

Stmt inject_prefetch(Stmt s, const map<string, Function> &env) {
    CollectExternalBufferBounds finder;
    s.accept(&finder);
    return InjectPrefetch(env, finder.buffers).mutate(s);
}

Stmt reduce_prefetch_dimension(Stmt stmt, const Target &t) {
    size_t max_dim = 0;
    Expr max_byte_size;

    // Hexagon's prefetch takes in a range of address and can be maximum of
    // two dimension. Other architectures generate one prefetch per cache line.
    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
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

}
}
