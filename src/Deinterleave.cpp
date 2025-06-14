#include "Deinterleave.h"

#include "CSE.h"
#include "Debug.h"
#include "FlattenNestedRamps.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "ModulusRemainder.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::pair;

namespace {

class StoreCollector : public IRMutator {
public:
    const std::string store_name;
    const int store_stride, max_stores;
    std::vector<Stmt> &let_stmts;
    std::vector<Stmt> &stores;

    StoreCollector(const std::string &name, int stride, int ms,
                   std::vector<Stmt> &lets, std::vector<Stmt> &ss)
        : store_name(name), store_stride(stride), max_stores(ms),
          let_stmts(lets), stores(ss) {
    }

private:
    using IRMutator::visit;

    // Don't enter any inner constructs for which it's not safe to pull out stores.
    Stmt visit(const For *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const IfThenElse *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const ProducerConsumer *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const Allocate *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const Realize *op) override {
        collecting = false;
        return op;
    }

    bool collecting = true;
    // These are lets that we've encountered since the last collected
    // store. If we collect another store, these "potential" lets
    // become lets used by the collected stores.
    std::vector<Stmt> potential_lets;

    Expr visit(const Load *op) override {
        if (!collecting) {
            return op;
        }

        // If we hit a load from the buffer we're trying to collect
        // stores for, stop collecting to avoid reordering loads and
        // stores from the same buffer.
        if (op->name == store_name) {
            collecting = false;
            return op;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        if (!collecting) {
            return op;
        }

        // By default, do nothing.
        Stmt stmt = op;

        if (stores.size() >= (size_t)max_stores) {
            // Already have enough stores.
            collecting = false;
            return stmt;
        }

        // Make sure this Store doesn't do anything that causes us to
        // stop collecting.
        stmt = IRMutator::visit(op);
        if (!collecting) {
            return stmt;
        }

        if (op->name != store_name) {
            // Not a store to the buffer we're looking for.
            return stmt;
        }

        const Ramp *r = op->index.as<Ramp>();
        if (!r || !is_const(r->stride, store_stride)) {
            // Store doesn't store to the ramp we're looking
            // for. Can't interleave it. Since we don't want to
            // reorder stores, stop collecting.
            collecting = false;
            return stmt;
        }

        // This store is good, collect it and replace with a no-op.
        stores.emplace_back(op);
        stmt = Evaluate::make(0);

        // Because we collected this store, we need to save the
        // potential lets since the last collected store.
        let_stmts.insert(let_stmts.end(), potential_lets.begin(), potential_lets.end());
        potential_lets.clear();
        return stmt;
    }

    Expr visit(const Call *op) override {
        if (!op->is_pure()) {
            // Avoid reordering calls to impure functions
            collecting = false;
            return op;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        if (!collecting) {
            return op;
        }

        // Walk inside the let chain
        Stmt stmt = IRMutator::visit(op);

        // If we're still collecting, we need to save the entire let chain as potential lets.
        if (collecting) {
            Stmt body;
            do {
                potential_lets.emplace_back(op);
                body = op->body;
            } while ((op = body.as<LetStmt>()));
        }
        return stmt;
    }

    Stmt visit(const Block *op) override {
        if (!collecting) {
            return op;
        }

        Stmt first = mutate(op->first);
        Stmt rest = op->rest;
        // We might have decided to stop collecting during mutation of first.
        if (collecting) {
            rest = mutate(rest);
        }
        return Block::make(first, rest);
    }
};

Stmt collect_strided_stores(const Stmt &stmt, const std::string &name, int stride, int max_stores,
                            std::vector<Stmt> lets, std::vector<Stmt> &stores) {

    StoreCollector collect(name, stride, max_stores, lets, stores);
    return collect.mutate(stmt);
}

class Deinterleaver : public IRGraphMutator {
public:
    Deinterleaver(int starting_lane, int lane_stride, int new_lanes, const Scope<> &lets)
        : starting_lane(starting_lane),
          lane_stride(lane_stride),
          new_lanes(new_lanes),
          external_lets(lets) {
    }

private:
    int starting_lane;
    int lane_stride;
    int new_lanes;

    // lets for which we have even and odd lane specializations
    const Scope<> &external_lets;

    using IRMutator::visit;

    Expr visit(const VectorReduce *op) override {
        std::vector<int> input_lanes;
        int factor = op->value.type().lanes() / op->type.lanes();
        for (int i = starting_lane; i < op->type.lanes(); i += lane_stride) {
            for (int j = 0; j < factor; j++) {
                input_lanes.push_back(i * factor + j);
            }
        }
        Expr in = Shuffle::make({op->value}, input_lanes);
        return VectorReduce::make(op->op, in, new_lanes);
    }

    Expr visit(const Broadcast *op) override {
        if (new_lanes == 1) {
            if (op->value.type().lanes() == 1) {
                return op->value;
            } else {
                int old_starting_lane = starting_lane;
                int old_lane_stride = lane_stride;
                starting_lane = starting_lane % op->value.type().lanes();
                lane_stride = op->value.type().lanes();
                Expr e = mutate(op->value);
                starting_lane = old_starting_lane;
                lane_stride = old_lane_stride;
                return e;
            }
        }
        if (op->value.type().lanes() > 1) {
            // There is probably a more efficient way to do this.
            return mutate(flatten_nested_ramps(op));
        }

        return Broadcast::make(op->value, new_lanes);
    }

    Expr visit(const Load *op) override {
        if (op->type.is_scalar()) {
            return op;
        } else {
            Type t = op->type.with_lanes(new_lanes);
            ModulusRemainder align = op->alignment;
            // The alignment of a Load refers to the alignment of the first
            // lane, so we can preserve the existing alignment metadata if the
            // deinterleave is asking for any subset of lanes that includes the
            // first. Otherwise we just drop it. We could check if the index is
            // a ramp with constant stride or some other special case, but if
            // that's the case, the simplifier is very good at figuring out the
            // alignment, and it has access to context (e.g. the alignment of
            // enclosing lets) that we do not have here.
            if (starting_lane != 0) {
                align = ModulusRemainder();
            }
            return Load::make(t, op->name, mutate(op->index), op->image, op->param, mutate(op->predicate), align);
        }
    }

    Expr visit(const Ramp *op) override {
        int base_lanes = op->base.type().lanes();
        if (base_lanes > 1) {
            if (new_lanes == 1) {
                int index = starting_lane / base_lanes;
                Expr expr = op->base + cast(op->base.type(), index) * op->stride;
                ScopedValue<int> old_starting_lane(starting_lane, starting_lane % base_lanes);
                ScopedValue<int> old_lane_stride(lane_stride, base_lanes);
                expr = mutate(expr);
                return expr;
            } else if (base_lanes == lane_stride &&
                       starting_lane < base_lanes) {
                // Base class mutator actually works fine in this
                // case, but we only want one lane from the base and
                // one lane from the stride.
                ScopedValue<int> old_new_lanes(new_lanes, 1);
                return IRMutator::visit(op);
            } else {
                // There is probably a more efficient way to this by
                // generalizing the two cases above.
                return mutate(flatten_nested_ramps(op));
            }
        }
        Expr expr = op->base + cast(op->base.type(), starting_lane) * op->stride;
        internal_assert(expr.type() == op->base.type());
        if (new_lanes > 1) {
            expr = Ramp::make(expr, op->stride * lane_stride, new_lanes);
        }
        return expr;
    }

    Expr give_up_and_shuffle(const Expr &e) {
        // Uh-oh, we don't know how to deinterleave this vector expression
        // Make llvm do it
        std::vector<int> indices;
        for (int i = 0; i < new_lanes; i++) {
            indices.push_back(starting_lane + lane_stride * i);
        }
        return Shuffle::make({e}, indices);
    }

    Expr visit(const Variable *op) override {
        if (op->type.is_scalar()) {
            return op;
        } else {

            Type t = op->type.with_lanes(new_lanes);
            internal_assert((op->type.lanes() - starting_lane + lane_stride - 1) / lane_stride == new_lanes)
                << "Deinterleaving with lane stride " << lane_stride << " and staring lane " << starting_lane
                << " for var of Type " << op->type << " to " << t << " drops lanes unexpectedly."
                << " Deinterleaver probably recursed too deep into types of different lane count.";
            if (external_lets.contains(op->name) &&
                starting_lane == 0 &&
                lane_stride == 2) {
                return Variable::make(t, op->name + ".even_lanes", op->image, op->param, op->reduction_domain);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 1 &&
                       lane_stride == 2) {
                return Variable::make(t, op->name + ".odd_lanes", op->image, op->param, op->reduction_domain);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 0 &&
                       lane_stride == 3) {
                return Variable::make(t, op->name + ".lanes_0_of_3", op->image, op->param, op->reduction_domain);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 1 &&
                       lane_stride == 3) {
                return Variable::make(t, op->name + ".lanes_1_of_3", op->image, op->param, op->reduction_domain);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 2 &&
                       lane_stride == 3) {
                return Variable::make(t, op->name + ".lanes_2_of_3", op->image, op->param, op->reduction_domain);
            } else {
                return give_up_and_shuffle(op);
            }
        }
    }

    Expr visit(const Cast *op) override {
        if (op->type.is_scalar()) {
            return op;
        } else {
            Type t = op->type.with_lanes(new_lanes);
            return Cast::make(t, mutate(op->value));
        }
    }

    Expr visit(const Reinterpret *op) override {
        if (op->type.is_scalar()) {
            return op;
        } else if (op->type.bits() != op->value.type().bits()) {
            return give_up_and_shuffle(op);
        } else {
            Type t = op->type.with_lanes(new_lanes);
            return Reinterpret::make(t, mutate(op->value));
        }
    }

    Expr visit(const Call *op) override {
        Type t = op->type.with_lanes(new_lanes);

        // Don't mutate scalars
        if (op->type.is_scalar()) {
            return op;
        } else {

            // Vector calls are always parallel across the lanes, so we
            // can just deinterleave the args.

            // Beware of intrinsics for which this is not true!
            auto args = mutate(op->args);
            return Call::make(t, op->name, args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        }
    }

    Expr visit(const Shuffle *op) override {
        if (op->is_interleave()) {
            // Special case where we can discard some of the vector arguments entirely.
            internal_assert(starting_lane >= 0 && starting_lane < lane_stride);
            if ((int)op->vectors.size() == lane_stride) {
                return op->vectors[starting_lane];
            } else if ((int)op->vectors.size() % lane_stride == 0) {
                // Pick up every lane-stride vector.
                std::vector<Expr> new_vectors(op->vectors.size() / lane_stride);
                for (size_t i = 0; i < new_vectors.size(); i++) {
                    new_vectors[i] = op->vectors[i * lane_stride + starting_lane];
                }
                return Shuffle::make_interleave(new_vectors);
            }
        }

        // Keep the same set of vectors and extract every nth numeric
        // arg to the shuffle.
        std::vector<int> indices;
        for (int i = 0; i < new_lanes; i++) {
            int idx = i * lane_stride + starting_lane;
            indices.push_back(op->indices[idx]);
        }

        // If this is extracting a single lane, try to recursively deinterleave rather
        // than leaving behind a shuffle.
        if (indices.size() == 1) {
            int index = indices.front();
            for (const auto &i : op->vectors) {
                if (index < i.type().lanes()) {
                    if (i.type().lanes() == op->type.lanes()) {
                        ScopedValue<int> scoped_starting_lane(starting_lane, index);
                        return mutate(i);
                    } else {
                        return Shuffle::make(op->vectors, indices);
                    }
                }
                index -= i.type().lanes();
            }
            internal_error << "extract_lane index out of bounds: " << Expr(op) << " " << index << "\n";
        }

        return Shuffle::make(op->vectors, indices);
    }
};

Expr deinterleave(Expr e, int starting_lane, int lane_stride, int new_lanes, const Scope<> &lets) {
    debug(3) << "Deinterleave "
             << "(start:" << starting_lane << ", stide:" << lane_stride << ", new_lanes:" << new_lanes << "): "
             << e << " of Type: " << e.type() << "\n";
    Type original_type = e.type();
    e = substitute_in_all_lets(e);
    Deinterleaver d(starting_lane, lane_stride, new_lanes, lets);
    e = d.mutate(e);
    e = common_subexpression_elimination(e);
    Type final_type = e.type();
    int expected_lanes = (original_type.lanes() + lane_stride - starting_lane - 1) / lane_stride;
    internal_assert(original_type.code() == final_type.code()) << "Underlying types not identical after interleaving.";
    internal_assert(expected_lanes == final_type.lanes()) << "Number of lanes incorrect after interleaving: " << final_type.lanes() << "while expected was " << expected_lanes << ".";
    return simplify(e);
}

Expr extract_odd_lanes(const Expr &e, const Scope<> &lets) {
    internal_assert(e.type().lanes() % 2 == 0);
    return deinterleave(e, 1, 2, e.type().lanes() / 2, lets);
}

Expr extract_even_lanes(const Expr &e, const Scope<> &lets) {
    internal_assert(e.type().lanes() % 2 == 0);
    return deinterleave(e, 0, 2, e.type().lanes() / 2, lets);
}

Expr extract_mod3_lanes(const Expr &e, int lane, const Scope<> &lets) {
    internal_assert(e.type().lanes() % 3 == 0);
    return deinterleave(e, lane, 3, e.type().lanes() / 3, lets);
}

}  // namespace

Expr extract_even_lanes(const Expr &e) {
    internal_assert(e.type().lanes() % 2 == 0);
    Scope<> lets;
    return extract_even_lanes(e, lets);
}

Expr extract_odd_lanes(const Expr &e) {
    internal_assert(e.type().lanes() % 2 == 0);
    Scope<> lets;
    return extract_odd_lanes(e, lets);
}

Expr extract_lane(const Expr &e, int lane) {
    Scope<> lets;
    return deinterleave(e, lane, e.type().lanes(), 1, lets);
}

namespace {

class Interleaver : public IRMutator {
    Scope<> vector_lets;

    using IRMutator::visit;

    bool should_deinterleave = false;
    int num_lanes;

    Expr deinterleave_expr(const Expr &e) {
        std::vector<Expr> exprs;
        for (int i = 0; i < num_lanes; i++) {
            Scope<> lets;
            exprs.emplace_back(deinterleave(e, i, num_lanes, e.type().lanes() / num_lanes, lets));
        }
        return Shuffle::make_interleave(exprs);
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        // Visit an entire chain of lets in a single method to conserve stack space.
        struct Frame {
            const LetOrLetStmt *op;
            Expr new_value;
            ScopedBinding<> binding;
            Frame(const LetOrLetStmt *op, Expr v, Scope<void> &scope)
                : op(op),
                  new_value(std::move(v)),
                  binding(new_value.type().is_vector(), scope, op->name) {
            }
        };
        std::vector<Frame> frames;
        decltype(op->body) result;

        do {
            result = op->body;
            frames.emplace_back(op, mutate(op->value), vector_lets);
        } while ((op = result.template as<LetOrLetStmt>()));

        result = mutate(result);

        for (const auto &frame : reverse_view(frames)) {
            Expr value = std::move(frame.new_value);

            result = LetOrLetStmt::make(frame.op->name, value, result);

            // For vector lets, we may additionally need a let defining the even and odd lanes only
            if (value.type().is_vector()) {
                if (value.type().lanes() % 2 == 0) {
                    result = LetOrLetStmt::make(frame.op->name + ".even_lanes", extract_even_lanes(value, vector_lets), result);
                    result = LetOrLetStmt::make(frame.op->name + ".odd_lanes", extract_odd_lanes(value, vector_lets), result);
                }
                if (value.type().lanes() % 3 == 0) {
                    result = LetOrLetStmt::make(frame.op->name + ".lanes_0_of_3", extract_mod3_lanes(value, 0, vector_lets), result);
                    result = LetOrLetStmt::make(frame.op->name + ".lanes_1_of_3", extract_mod3_lanes(value, 1, vector_lets), result);
                    result = LetOrLetStmt::make(frame.op->name + ".lanes_2_of_3", extract_mod3_lanes(value, 2, vector_lets), result);
                }
            }
        }

        return result;
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Expr visit(const Ramp *op) override {
        if (op->stride.type().is_vector() &&
            is_const_one(op->stride) &&
            !op->base.as<Ramp>() &&
            !op->base.as<Broadcast>()) {
            // We have a ramp with a computed vector base.  If we
            // deinterleave we'll get ramps of stride 1 with a
            // computed scalar base.
            should_deinterleave = true;
            num_lanes = op->stride.type().lanes();
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Mod *op) override {
        const Ramp *r = op->a.as<Ramp>();
        for (int i = 2; i <= 4; ++i) {
            if (r &&
                is_const(op->b, i) &&
                (r->type.lanes() % i) == 0) {
                should_deinterleave = true;
                num_lanes = i;
                break;
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Div *op) override {
        const Ramp *r = op->a.as<Ramp>();
        for (int i = 2; i <= 4; ++i) {
            if (r &&
                is_const(op->b, i) &&
                (r->type.lanes() % i) == 0 &&
                r->type.lanes() > i) {
                should_deinterleave = true;
                num_lanes = i;
                break;
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        if (!op->is_pure() &&
            !op->is_intrinsic(Call::unsafe_promise_clamped) &&
            !op->is_intrinsic(Call::promise_clamped)) {
            // deinterleaving potentially changes the order of execution.
            should_deinterleave = false;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Load *op) override {
        bool old_should_deinterleave = should_deinterleave;
        int old_num_lanes = num_lanes;

        should_deinterleave = false;
        Expr idx = mutate(op->index);
        bool should_deinterleave_idx = should_deinterleave;

        should_deinterleave = false;
        Expr predicate = mutate(op->predicate);
        bool should_deinterleave_predicate = should_deinterleave;

        Expr expr;
        if (should_deinterleave_idx && (should_deinterleave_predicate || is_const_one(predicate))) {
            // If we want to deinterleave both the index and predicate
            // (or the predicate is one), then deinterleave the
            // resulting load.
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
            expr = deinterleave_expr(expr);
        } else if (should_deinterleave_idx) {
            // If we only want to deinterleave the index and not the
            // predicate, deinterleave the index prior to the load.
            idx = deinterleave_expr(idx);
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else if (should_deinterleave_predicate) {
            // Similarly, deinterleave the predicate prior to the load
            // if we don't want to deinterleave the index.
            predicate = deinterleave_expr(predicate);
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else if (!idx.same_as(op->index) || !predicate.same_as(op->index)) {
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else {
            expr = op;
        }

        should_deinterleave = old_should_deinterleave;
        num_lanes = old_num_lanes;
        return expr;
    }

    Stmt visit(const Store *op) override {
        bool old_should_deinterleave = should_deinterleave;
        int old_num_lanes = num_lanes;

        should_deinterleave = false;
        Expr idx = mutate(op->index);
        if (should_deinterleave) {
            idx = deinterleave_expr(idx);
        }

        should_deinterleave = false;
        Expr value = mutate(op->value);
        if (should_deinterleave) {
            value = deinterleave_expr(value);
        }

        should_deinterleave = false;
        Expr predicate = mutate(op->predicate);
        if (should_deinterleave) {
            predicate = deinterleave_expr(predicate);
        }

        Stmt stmt = Store::make(op->name, value, idx, op->param, predicate, op->alignment);

        should_deinterleave = old_should_deinterleave;
        num_lanes = old_num_lanes;

        return stmt;
    }

    HALIDE_NEVER_INLINE Stmt gather_stores(const Block *op) {
        const LetStmt *let = op->first.as<LetStmt>();
        const Store *store = op->first.as<Store>();

        // Gather all the let stmts surrounding the first.
        std::vector<Stmt> let_stmts;
        while (let) {
            let_stmts.emplace_back(let);
            store = let->body.as<Store>();
            let = let->body.as<LetStmt>();
        }

        // There was no inner store.
        if (!store) {
            return Stmt();
        }

        const Ramp *r0 = store->index.as<Ramp>();

        // It's not a store of a ramp index.
        if (!r0) {
            return Stmt();
        }

        auto optional_stride = as_const_int(r0->stride);

        // The stride isn't a constant or is <= 1
        if (!optional_stride || *optional_stride <= 1) {
            return Stmt();
        }

        const int64_t stride = *optional_stride;
        const int lanes = r0->lanes;
        const int64_t expected_stores = stride;

        // Collect the rest of the stores.
        std::vector<Stmt> stores;
        stores.emplace_back(store);
        Stmt rest = collect_strided_stores(op->rest, store->name,
                                           stride, expected_stores,
                                           let_stmts, stores);

        // Check the store collector didn't collect too many
        // stores (that would be a bug).
        internal_assert(stores.size() <= (size_t)expected_stores);

        // Not enough stores collected.
        if (stores.size() != (size_t)expected_stores) {
            return Stmt();
        }

        // Too many stores and lanes to represent in a single vector
        // type.
        int max_bits = sizeof(halide_type_t::lanes) * 8;
        // mul_would_overflow is for signed types, but vector lanes
        // are unsigned, so add a bit.
        max_bits++;
        if (mul_would_overflow(max_bits, stores.size(), lanes)) {
            return Stmt();
        }

        Type t = store->value.type();
        Expr base;
        std::vector<Expr> args(stores.size());
        std::vector<Expr> predicates(stores.size());

        int64_t min_offset = 0;
        std::vector<int64_t> offsets(stores.size());

        std::string load_name;
        Buffer<> load_image;
        Parameter load_param;
        for (size_t i = 0; i < stores.size(); ++i) {
            const Ramp *ri = stores[i].as<Store>()->index.as<Ramp>();
            internal_assert(ri);

            // Mismatched store vector laness.
            if (ri->lanes != lanes) {
                return Stmt();
            }

            auto offs = as_const_int(simplify(ri->base - r0->base));

            // Difference between bases is not constant.
            if (!offs) {
                return Stmt();
            }

            offsets[i] = *offs;
            min_offset = std::min(min_offset, *offs);
        }

        // Gather the args for interleaving.
        for (size_t i = 0; i < stores.size(); ++i) {
            int64_t j = offsets[i] - min_offset;
            if (j == 0) {
                base = stores[i].as<Store>()->index.as<Ramp>()->base;
            }

            // The offset is not between zero and the stride.
            if (j < 0 || (size_t)j >= stores.size()) {
                return Stmt();
            }

            // We already have a store for this offset.
            if (args[j].defined()) {
                return Stmt();
            }

            args[j] = stores[i].as<Store>()->value;
            predicates[j] = stores[i].as<Store>()->predicate;
        }

        // One of the stores should have had the minimum offset.
        internal_assert(base.defined());

        // Generate a single interleaving store.
        t = t.with_lanes(lanes * stores.size());
        Expr index = Ramp::make(base, make_one(base.type()), t.lanes());
        Expr value = Shuffle::make_interleave(args);
        Expr predicate = Shuffle::make_interleave(predicates);
        Stmt new_store = Store::make(store->name, value, index, store->param, predicate, ModulusRemainder());

        // Rewrap the let statements we pulled off.
        while (!let_stmts.empty()) {
            const LetStmt *let = let_stmts.back().as<LetStmt>();
            new_store = LetStmt::make(let->name, let->value, new_store);
            let_stmts.pop_back();
        }

        // Continue recursively into the stuff that
        // collect_strided_stores didn't collect.
        Stmt stmt = Block::make(new_store, mutate(rest));

        // Success!
        return stmt;
    }

    Stmt visit(const Block *op) override {
        Stmt s = gather_stores(op);
        if (s.defined()) {
            return s;
        } else {
            Stmt first = mutate(op->first);
            Stmt rest = mutate(op->rest);
            if (first.same_as(op->first) && rest.same_as(op->rest)) {
                return op;
            } else {
                return Block::make(first, rest);
            }
        }
    }

public:
    Interleaver() = default;
};

}  // namespace

Stmt rewrite_interleavings(const Stmt &s) {
    return Interleaver().mutate(s);
}

namespace {
void check(Expr a, const Expr &even, const Expr &odd) {
    a = simplify(a);
    Expr correct_even = extract_even_lanes(a);
    Expr correct_odd = extract_odd_lanes(a);
    if (!equal(correct_even, even)) {
        internal_error << correct_even << " != " << even << "\n";
    }
    if (!equal(correct_odd, odd)) {
        internal_error << correct_odd << " != " << odd << "\n";
    }
}
}  // namespace

void deinterleave_vector_test() {
    std::pair<Expr, Expr> result;
    Expr x = Variable::make(Int(32), "x");
    Expr ramp = Ramp::make(x + 4, 3, 8);
    Expr ramp_a = Ramp::make(x + 4, 6, 4);
    Expr ramp_b = Ramp::make(x + 7, 6, 4);
    Expr broadcast = Broadcast::make(x + 4, 16);
    Expr broadcast_a = Broadcast::make(x + 4, 8);
    const Expr &broadcast_b = broadcast_a;

    check(ramp, ramp_a, ramp_b);
    check(broadcast, broadcast_a, broadcast_b);

    check(Load::make(ramp.type(), "buf", ramp, Buffer<>(), Parameter(), const_true(ramp.type().lanes()), ModulusRemainder()),
          Load::make(ramp_a.type(), "buf", ramp_a, Buffer<>(), Parameter(), const_true(ramp_a.type().lanes()), ModulusRemainder()),
          Load::make(ramp_b.type(), "buf", ramp_b, Buffer<>(), Parameter(), const_true(ramp_b.type().lanes()), ModulusRemainder()));

    Expr vec_x = Variable::make(Int(32, 4), "vec_x");
    Expr vec_y = Variable::make(Int(32, 4), "vec_y");
    check(Shuffle::make({vec_x, vec_y}, {0, 4, 2, 6, 4, 2, 3, 7, 1, 2, 3, 4}),
          Shuffle::make({vec_x, vec_y}, {0, 2, 4, 3, 1, 3}),
          Shuffle::make({vec_x, vec_y}, {4, 6, 2, 7, 2, 4}));

    std::cout << "deinterleave_vector test passed\n";
}

}  // namespace Internal
}  // namespace Halide
