#include "Deinterleave.h"
#include "Debug.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRPrinter.h"
#include "ModulusRemainder.h"
#include "Scope.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::pair;

class StoreCollector : public IRMutator {
public:
    const std::string store_name;
    const int store_stride, max_stores;
    std::vector<Stmt> &let_stmts;
    std::vector<Stmt> &stores;

    StoreCollector(const std::string &name, int stride, int ms,
                   std::vector<Stmt> &lets,
                   std::vector<Stmt> &ss) :
        store_name(name), store_stride(stride), max_stores(ms),
        let_stmts(lets), stores(ss), collecting(true) {}
private:

    using IRMutator::visit;

    // Don't enter any inner constructs for which it's not safe to pull out stores.
    void visit(const For *op) {collecting = false; stmt = op;}
    void visit(const IfThenElse *op) {collecting = false; stmt = op;}
    void visit(const ProducerConsumer *op) {collecting = false; stmt = op;}
    void visit(const Allocate *op) {collecting = false; stmt = op;}
    void visit(const Realize *op) {collecting = false; stmt = op;}

    bool collecting;
    // These are lets that we've encountered since the last collected
    // store. If we collect another store, these "potential" lets
    // become lets used by the collected stores.
    std::vector<Stmt> potential_lets;

    void visit(const Load *op) {
        if (!collecting) {
            expr = op;
            return;
        }

        // If we hit a load from the buffer we're trying to collect
        // stores for, stop collecting to avoid reordering loads and
        // stores from the same buffer.
        if (op->name == store_name) {
            collecting = false;
            expr = op;
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        if (!collecting) {
            stmt = op;
            return;
        }

        // By default, do nothing.
        stmt = op;

        if (stores.size() >= (size_t)max_stores) {
            // Already have enough stores.
            collecting = false;
            return;
        }

        // Make sure this Store doesn't do anything that causes us to
        // stop collecting.
        IRMutator::visit(op);
        if (!collecting) {
            return;
        }

        if (op->name != store_name) {
            // Not a store to the buffer we're looking for.
            return;
        }

        const Ramp *r = op->index.as<Ramp>();
        if (!r || !is_const(r->stride, store_stride)) {
            // Store doesn't store to the ramp we're looking
            // for. Can't interleave it. Since we don't want to
            // reorder stores, stop collecting.
            collecting = false;
            return;
        }

        // This store is good, collect it and replace with a no-op.
        stores.push_back(op);
        stmt = Evaluate::make(0);

        // Because we collected this store, we need to save the
        // potential lets since the last collected store.
        let_stmts.insert(let_stmts.end(), potential_lets.begin(), potential_lets.end());
        potential_lets.clear();
    }

    void visit(const Call *op) {
        if (!op->is_pure()) {
            // Avoid reordering calls to impure functions
            collecting = false;
            expr = op;
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (!collecting) {
            stmt = op;
            return;
        }
        IRMutator::visit(op);

        // If we're still collecting, we need to save the let as a potential let.
        if (collecting) {
            potential_lets.push_back(op);
        }
    }

    void visit(const Block *op) {
        if (!collecting) {
            stmt = op;
            return;
        }

        Stmt first = mutate(op->first);
        Stmt rest = op->rest;
        // We might have decided to stop collecting during mutation of first.
        if (collecting) {
            rest = mutate(rest);
        }
        stmt = Block::make(first, rest);
    }
};

Stmt collect_strided_stores(Stmt stmt, const std::string& name, int stride, int max_stores,
                            std::vector<Stmt> lets, std::vector<Stmt> &stores) {

    StoreCollector collect(name, stride, max_stores, lets, stores);
    return collect.mutate(stmt);
}


class Deinterleaver : public IRMutator {
public:
    int starting_lane;
    int new_lanes;
    int lane_stride;

    // lets for which we have even and odd lane specializations
    const Scope<int> &external_lets;

    Deinterleaver(const Scope<int> &lets) : external_lets(lets) {}

private:
    Scope<Expr> internal;

    using IRMutator::visit;

    void visit(const Broadcast *op) {
        if (new_lanes == 1) {
            expr = op->value;
        } else {
            expr = Broadcast::make(op->value, new_lanes);
        }
    }

    void visit(const Load *op) {
        if (op->type.is_scalar()) {
            expr = op;
        } else {
            Type t = op->type.with_lanes(new_lanes);
            expr = Load::make(t, op->name, mutate(op->index), op->image, op->param, mutate(op->predicate));
        }
    }

    void visit(const Ramp *op) {
        expr = op->base + starting_lane * op->stride;
        internal_assert(expr.type() == op->base.type());
        if (new_lanes > 1) {
            expr = Ramp::make(expr, op->stride * lane_stride, new_lanes);
        }
    }

    void visit(const Variable *op) {
        if (op->type.is_scalar()) {
            expr = op;
        } else {

            Type t = op->type.with_lanes(new_lanes);
            if (internal.contains(op->name)) {
                expr = internal.get(op->name);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 0 &&
                       lane_stride == 2) {
                expr = Variable::make(t, op->name + ".even_lanes", op->image, op->param, op->reduction_domain);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 1 &&
                       lane_stride == 2) {
                expr = Variable::make(t, op->name + ".odd_lanes", op->image, op->param, op->reduction_domain);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 0 &&
                       lane_stride == 3) {
                expr = Variable::make(t, op->name + ".lanes_0_of_3", op->image, op->param, op->reduction_domain);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 1 &&
                       lane_stride == 3) {
                expr = Variable::make(t, op->name + ".lanes_1_of_3", op->image, op->param, op->reduction_domain);
            } else if (external_lets.contains(op->name) &&
                       starting_lane == 2 &&
                       lane_stride == 3) {
                expr = Variable::make(t, op->name + ".lanes_2_of_3", op->image, op->param, op->reduction_domain);
            } else {
                // Uh-oh, we don't know how to deinterleave this vector expression
                // Make llvm do it
                std::vector<int> indices;
                for (int i = 0; i < new_lanes; i++) {
                    indices.push_back(starting_lane + lane_stride * i);
                }
                expr = Shuffle::make({op}, indices);
            }
        }
    }

    void visit(const Cast *op) {
        if (op->type.is_scalar()) {
            expr = op;
        } else {
            Type t = op->type.with_lanes(new_lanes);
            expr = Cast::make(t, mutate(op->value));
        }
    }

    void visit(const Call *op) {
        Type t = op->type.with_lanes(new_lanes);

        // Don't mutate scalars
        if (op->type.is_scalar()) {
            expr = op;
        } else if (op->is_intrinsic(Call::glsl_texture_load)) {
            // glsl_texture_load returns a <uint x 4> result. Deinterleave by
            // wrapping the call in a shuffle_vector
            std::vector<int> indices;
            for (int i = 0; i < new_lanes; i++) {
                indices.push_back(i*lane_stride + starting_lane);
            }
            expr = Shuffle::make({op}, indices);
        } else {

            // Vector calls are always parallel across the lanes, so we
            // can just deinterleave the args.

            // Beware of other intrinsics for which this is not true!
            // Currently there's only interleave_vectors and
            // shuffle_vector.

            std::vector<Expr> args(op->args.size());
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(op->args[i]);
            }

            expr = Call::make(t, op->name, args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        }
    }

    void visit(const Let *op) {
        if (op->type.is_vector()) {
            Expr new_value = mutate(op->value);
            std::string new_name = unique_name('t');
            Type new_type = new_value.type();
            Expr new_var = Variable::make(new_type, new_name);
            internal.push(op->name, new_var);
            Expr body = mutate(op->body);
            internal.pop(op->name);

            // Define the new name.
            expr = Let::make(new_name, new_value, body);

            // Someone might still use the old name.
            expr = Let::make(op->name, op->value, expr);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Shuffle *op) {
        if (op->is_interleave()) {
            internal_assert(starting_lane >= 0 && starting_lane < lane_stride);
            if ((int)op->vectors.size() == lane_stride) {
                expr = op->vectors[starting_lane];
            } else if ((int)op->vectors.size() % lane_stride == 0) {
                // Pick up every lane-stride vector.
                std::vector<Expr> new_vectors(op->vectors.size() / lane_stride);
                for (size_t i = 0; i < new_vectors.size(); i++) {
                    new_vectors[i] = op->vectors[i*lane_stride + starting_lane];
                }
                expr = Shuffle::make_interleave(new_vectors);
            } else {
                // Interleave some vectors then deinterleave by some other factor...
                // Brute force!
                std::vector<int> indices;
                for (int i = 0; i < new_lanes; i++) {
                    indices.push_back(i*lane_stride + starting_lane);
                }
                expr = Shuffle::make({op}, indices);
            }
        } else {
            // Extract every nth numeric arg to the shuffle.
            std::vector<int> indices;
            for (int i = 0; i < new_lanes; i++) {
                int idx = i * lane_stride + starting_lane;
                indices.push_back(op->indices[idx]);
            }
            expr = Shuffle::make({op}, indices);
        }
    }
};

Expr extract_odd_lanes(Expr e, const Scope<int> &lets) {
    internal_assert(e.type().lanes() % 2 == 0);
    Deinterleaver d(lets);
    d.starting_lane = 1;
    d.lane_stride = 2;
    d.new_lanes = e.type().lanes()/2;
    e = d.mutate(e);
    return simplify(e);
}

Expr extract_even_lanes(Expr e, const Scope<int> &lets) {
    internal_assert(e.type().lanes() % 2 == 0);
    Deinterleaver d(lets);
    d.starting_lane = 0;
    d.lane_stride = 2;
    d.new_lanes = (e.type().lanes()+1)/2;
    e = d.mutate(e);
    return simplify(e);
}

Expr extract_even_lanes(Expr e) {
    internal_assert(e.type().lanes() % 2 == 0);
    Scope<int> lets;
    return extract_even_lanes(e, lets);
}

Expr extract_odd_lanes(Expr e) {
    internal_assert(e.type().lanes() % 2 == 0);
    Scope<int> lets;
    return extract_odd_lanes(e, lets);
}

Expr extract_mod3_lanes(Expr e, int lane, const Scope<int> &lets) {
    internal_assert(e.type().lanes() % 3 == 0);
    Deinterleaver d(lets);
    d.starting_lane = lane;
    d.lane_stride = 3;
    d.new_lanes = (e.type().lanes()+2)/3;
    e = d.mutate(e);
    return simplify(e);
}

Expr extract_lane(Expr e, int lane) {
    Scope<int> lets;
    Deinterleaver d(lets);
    d.starting_lane = lane;
    d.lane_stride = e.type().lanes();
    d.new_lanes = 1;
    e = d.mutate(e);
    return simplify(e);
}

class Interleaver : public IRMutator {
    Scope<ModulusRemainder> alignment_info;

    Scope<int> vector_lets;

    using IRMutator::visit;

    bool should_deinterleave;
    int num_lanes;

    Expr deinterleave_expr(Expr e) {
        if (e.type().lanes() <= num_lanes) {
            // Just scalarize
            return e;
        } else if (num_lanes == 2) {
            Expr a = extract_even_lanes(e, vector_lets);
            Expr b = extract_odd_lanes(e, vector_lets);
            return Shuffle::make_interleave({a, b});
        } else if (num_lanes == 3) {
            Expr a = extract_mod3_lanes(e, 0, vector_lets);
            Expr b = extract_mod3_lanes(e, 1, vector_lets);
            Expr c = extract_mod3_lanes(e, 2, vector_lets);
            return Shuffle::make_interleave({a, b, c});
        } else if (num_lanes == 4) {
            Expr a = extract_even_lanes(e, vector_lets);
            Expr b = extract_odd_lanes(e, vector_lets);
            Expr aa = extract_even_lanes(a, vector_lets);
            Expr ab = extract_odd_lanes(a, vector_lets);
            Expr ba = extract_even_lanes(b, vector_lets);
            Expr bb = extract_odd_lanes(b, vector_lets);
            return Shuffle::make_interleave({aa, ba, ab, bb});
        } else {
            // Give up and don't do anything clever for >4
            return e;
        }
    }

    template<typename T, typename Body>
    Body visit_let(const T *op) {
        Expr value = mutate(op->value);
        if (value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(value, alignment_info));
        }

        if (value.type().is_vector()) {
            vector_lets.push(op->name, 0);
        }
        Body body = mutate(op->body);
        if (value.type().is_vector()) {
            vector_lets.pop(op->name);
        }
        if (value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }

        Body result;
        if (value.same_as(op->value) && body.same_as(op->body)) {
            result = op;
        } else {
            result = T::make(op->name, value, body);
        }

        // For vector lets, we may additionally need a let defining the even and odd lanes only
        if (value.type().is_vector()) {
            if (value.type().lanes() % 2 == 0) {
                result = T::make(op->name + ".even_lanes", extract_even_lanes(value, vector_lets), result);
                result = T::make(op->name + ".odd_lanes", extract_odd_lanes(value, vector_lets), result);
            }
            if (value.type().lanes() % 3 == 0) {
                result = T::make(op->name + ".lanes_0_of_3", extract_mod3_lanes(value, 0, vector_lets), result);
                result = T::make(op->name + ".lanes_1_of_3", extract_mod3_lanes(value, 1, vector_lets), result);
                result = T::make(op->name + ".lanes_2_of_3", extract_mod3_lanes(value, 2, vector_lets), result);
            }
        }

        return result;
    }

    void visit(const Let *op) {
        expr = visit_let<Let, Expr>(op);
    }

    void visit(const LetStmt *op) {
        stmt = visit_let<LetStmt, Stmt>(op);
    }

    void visit(const Mod *op) {
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
        IRMutator::visit(op);
    }

    void visit(const Div *op) {
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
        IRMutator::visit(op);
    }

    void visit(const Call *op) {
        if (!op->is_pure()) {
            // deinterleaving potentially changes the order of execution.
            should_deinterleave = false;
        }
        IRMutator::visit(op);
    }

    void visit(const Load *op) {
        bool old_should_deinterleave = should_deinterleave;
        int old_num_lanes = num_lanes;

        should_deinterleave = false;
        Expr idx = mutate(op->index);
        bool should_deinterleave_idx = should_deinterleave;

        should_deinterleave = false;
        Expr predicate = mutate(op->predicate);
        bool should_deinterleave_predicate = should_deinterleave;

        if (should_deinterleave_idx && (should_deinterleave_predicate || is_one(predicate))) {
            // If we want to deinterleave both the index and predicate
            // (or the predicate is one), then deinterleave the
            // resulting load.
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate);
            expr = deinterleave_expr(expr);
        } else if (should_deinterleave_idx) {
            // If we only want to deinterleave the index and not the
            // predicate, deinterleave the index prior to the load.
            idx = deinterleave_expr(idx);
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate);
        } else if (should_deinterleave_predicate) {
            // Similarly, deinterleave the predicate prior to the load
            // if we don't want to deinterleave the index.
            predicate = deinterleave_expr(predicate);
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate);
        } else if (!idx.same_as(op->index) || !predicate.same_as(op->index)) {
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate);
        } else {
            expr = op;
        }

        should_deinterleave = old_should_deinterleave;
        num_lanes = old_num_lanes;
    }

    void visit(const Store *op) {
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

        stmt = Store::make(op->name, value, idx, op->param, predicate);

        should_deinterleave = old_should_deinterleave;
        num_lanes = old_num_lanes;
    }

    void visit(const Block *op) {
        const LetStmt *let = op->first.as<LetStmt>();
        const Store *store = op->first.as<Store>();

        {
            // This isn't really a true block, so there can't be multiple
            // stores to collapse.
            if (!op->rest.defined()) goto fail;

            // Gather all the let stmts surrounding the first.
            std::vector<Stmt> let_stmts;
            while (let) {
                let_stmts.push_back(let);
                store = let->body.as<Store>();
                let = let->body.as<LetStmt>();
            }

            // There was no inner store.
            if (!store) goto fail;

            const Ramp *r0 = store->index.as<Ramp>();

            // It's not a store of a ramp index.
            if (!r0) goto fail;

            const int64_t *stride_ptr = as_const_int(r0->stride);

            // The stride isn't a constant or is <= 0
            if (!stride_ptr || *stride_ptr < 1) goto fail;

            const int64_t stride = *stride_ptr;
            const int lanes = r0->lanes;
            const int64_t expected_stores = stride == 1 ? lanes : stride;

            // Collect the rest of the stores.
            std::vector<Stmt> stores;
            stores.push_back(store);
            Stmt rest = collect_strided_stores(op->rest, store->name,
                                               stride, expected_stores,
                                               let_stmts, stores);

            // Check the store collector didn't collect too many
            // stores (that would be a bug).
            internal_assert(stores.size() <= (size_t)expected_stores);

            // Not enough stores collected.
            if (stores.size() != (size_t)expected_stores) goto fail;

            Type t = store->value.type();
            Expr base;
            std::vector<Expr> args(stores.size());
            std::vector<Expr> predicates(stores.size());

            int min_offset = 0;
            std::vector<int> offsets(stores.size());

            std::string load_name;
            Buffer<> load_image;
            Parameter load_param;
            for (size_t i = 0; i < stores.size(); ++i) {
                const Ramp *ri = stores[i].as<Store>()->index.as<Ramp>();
                internal_assert(ri);

                // Mismatched store vector laness.
                if (ri->lanes != lanes) goto fail;

                Expr diff = simplify(ri->base - r0->base);
                const int64_t *offs = as_const_int(diff);

                // Difference between bases is not constant.
                if (!offs) goto fail;

                offsets[i] = *offs;
                if (*offs < min_offset) {
                    min_offset = *offs;
                }

                if (stride == 1) {
                    // Difference between bases is not a multiple of the lanes.
                    if (*offs % lanes != 0) goto fail;

                    // This case only triggers if we have an immediate load of the correct stride on the RHS.
                    // TODO: Could we consider mutating the RHS so that we can handle more complex Expr's than just loads?
                    const Load *load = stores[i].as<Store>()->value.as<Load>();
                    if (!load) goto fail;
                    // TODO(psuriana): Predicated load is not currently handled.
                    if (!is_one(load->predicate)) goto fail;

                    const Ramp *ramp = load->index.as<Ramp>();
                    if (!ramp) goto fail;

                    // Load stride or lanes is not equal to the store lanes.
                    if (!is_const(ramp->stride, lanes) || ramp->lanes != lanes) goto fail;

                    if (i == 0) {
                        load_name  = load->name;
                        load_image = load->image;
                        load_param = load->param;
                    } else {
                        if (load->name != load_name) goto fail;
                    }
                }
            }

            // Gather the args for interleaving.
            for (size_t i = 0; i < stores.size(); ++i) {
                int j = offsets[i] - min_offset;
                if (stride == 1) {
                    j /= stores.size();
                }

                if (j == 0) {
                    base = stores[i].as<Store>()->index.as<Ramp>()->base;
                }

                // The offset is not between zero and the stride.
                if (j < 0 || (size_t)j >= stores.size()) goto fail;

                // We already have a store for this offset.
                if (args[j].defined()) goto fail;

                if (stride == 1) {
                    // Convert multiple dense vector stores of strided vector loads
                    // into one dense vector store of interleaving dense vector loads.
                    args[j] = Load::make(t, load_name, stores[i].as<Store>()->index,
                                         load_image, load_param, const_true(t.lanes()));
                } else {
                    args[j] = stores[i].as<Store>()->value;
                }
                predicates[j] = stores[i].as<Store>()->predicate;
            }

            // One of the stores should have had the minimum offset.
            internal_assert(base.defined());

            // Generate a single interleaving store.
            t = t.with_lanes(lanes*stores.size());
            Expr index = Ramp::make(base, make_one(base.type()), t.lanes());
            Expr value = Shuffle::make_interleave(args);
            Expr predicate = Shuffle::make_interleave(predicates);
            Stmt new_store = Store::make(store->name, value, index, store->param, predicate);

            // Continue recursively into the stuff that
            // collect_strided_stores didn't collect.
            stmt = Block::make(new_store, mutate(rest));

            // Rewrap the let statements we pulled off.
            while (!let_stmts.empty()) {
                const LetStmt *let = let_stmts.back().as<LetStmt>();
                stmt = LetStmt::make(let->name, let->value, stmt);
                let_stmts.pop_back();
            }

            // Success!
            return;
        }

      fail:
        // We didn't pass one of the tests. But maybe there are more
        // opportunities within. Continue recursively.
        stmt = Block::make(mutate(op->first), mutate(op->rest));
    }
  public:
    Interleaver() : should_deinterleave(false) {}
};

Stmt rewrite_interleavings(Stmt s) {
    return Interleaver().mutate(s);
}

namespace {
void check(Expr a, Expr even, Expr odd) {
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
}

void deinterleave_vector_test() {
    std::pair<Expr, Expr> result;
    Expr x = Variable::make(Int(32), "x");
    Expr ramp = Ramp::make(x + 4, 3, 8);
    Expr ramp_a = Ramp::make(x + 4, 6, 4);
    Expr ramp_b = Ramp::make(x + 7, 6, 4);
    Expr broadcast = Broadcast::make(x + 4, 16);
    Expr broadcast_a = Broadcast::make(x + 4, 8);
    Expr broadcast_b = broadcast_a;

    check(ramp, ramp_a, ramp_b);
    check(broadcast, broadcast_a, broadcast_b);

    check(Load::make(ramp.type(), "buf", ramp, Buffer<>(), Parameter(), const_true(ramp.type().lanes())),
          Load::make(ramp_a.type(), "buf", ramp_a, Buffer<>(), Parameter(), const_true(ramp_a.type().lanes())),
          Load::make(ramp_b.type(), "buf", ramp_b, Buffer<>(), Parameter(), const_true(ramp_b.type().lanes())));

    std::cout << "deinterleave_vector test passed" << std::endl;
}

}
}
