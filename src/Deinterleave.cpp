#include "Deinterleave.h"
#include "BlockFlattening.h"
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
using std::make_pair;

class ContainsLoad : public IRVisitor {
public:
    const std::string load_name;
    bool has_load;

    ContainsLoad(const std::string& name) :
        load_name(name), has_load(false) {}

    operator bool() const {return has_load;}
private:

    using IRVisitor::visit;

    void visit(const Load *op) {
        if (op->name == load_name) {
            has_load = true;
            return;
        }
    }
};

class StoreCollector : public IRMutator {
public:
    const std::string store_name;
    const int store_stride;
    std::vector<LetStmt>& let_stmts;
    std::vector<Store>& stores;

    StoreCollector(const std::string& name, int stride, std::vector<LetStmt>& lets, std::vector<Store>& ss) :
        store_name(name), store_stride(stride), let_stmts(lets), stores(ss) {}
private:

    using IRMutator::visit;

    void visit(const For *op) {
        stmt = op;
    }

    void visit(const Store *op) {
        if (op->name == store_name) {
            const Ramp *r = op->index.as<Ramp>();

            if (r && is_const(r->stride, store_stride)) {
                stores.push_back(*op);
                stmt = Evaluate::make(0);
                return;
            }
        }

        stmt = op;
    }

    void visit(const LetStmt *op) {
        ContainsLoad let_has_load(store_name);
        op->value.accept(&let_has_load);
        if (let_has_load) {
            stmt = op;
            return;
        }

        let_stmts.push_back(*op);
        stmt = mutate(op->body);
        return;
    }

    void visit(const Block *op) {
        const LetStmt *let = op->first.as<LetStmt>();
        const Store   *store = op->first.as<Store>();

        if (let) {
            ContainsLoad let_has_load(store_name);
            let->value.accept(&let_has_load);
            if (let_has_load) {
                stmt = op;
                return;
            }

            let_stmts.push_back(*let);
            stmt = mutate(Block::make(let->body, op->rest));
            return;
        } else if (store) {
            ContainsLoad store_has_load(store_name);
            store->value.accept(&store_has_load);
            if (store_has_load) {
                stmt = op;
                return;
            } else if (store->name == store_name) {
                const Ramp *r = store->index.as<Ramp>();

                if (r && is_const(r->stride, store_stride)) {
                    stores.push_back(*store);
                    stmt = mutate(op->rest);
                    return;
                }
            }
        }

        stmt = Block::make(op->first, mutate(op->rest));
    }
};

Stmt collect_strided_stores(Stmt stmt, const std::string& name, int stride, std::vector<LetStmt> lets, std::vector<Store>& stores) {
    StoreCollector collect(name, stride, lets, stores);
    return collect.mutate(stmt);
}


class Deinterleaver : public IRMutator {
public:
    int starting_lane;
    int new_width;
    int lane_stride;

    // lets for which we have even and odd lane specializations
    const Scope<int> &external_lets;

    Deinterleaver(const Scope<int> &lets) : external_lets(lets) {}
private:
    Scope<Expr> internal;

    using IRMutator::visit;

    void visit(const Broadcast *op) {
        if (new_width == 1) {
            expr = op->value;
        } else {
            expr = Broadcast::make(op->value, new_width);
        }
    }

    void visit(const Load *op) {
        if (op->type.is_scalar()) {
            expr = op;
        } else {
            Type t = op->type;
            t.width = new_width;
            expr = Load::make(t, op->name, mutate(op->index), op->image, op->param);
        }
    }

    void visit(const Ramp *op) {
        expr = op->base + starting_lane * op->stride;
        if (new_width > 1) {
            expr = Ramp::make(expr, op->stride * lane_stride, new_width);
        }
    }

    void visit(const Variable *op) {
        if (op->type.is_scalar()) {
            expr = op;
        } else {

            Type t = op->type;
            t.width = new_width;
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
                std::vector<Expr> args;
                args.push_back(op);
                for (int i = 0; i < new_width; i++) {
                    args.push_back(starting_lane + lane_stride * i);
                }
                expr = Call::make(t, Call::shuffle_vector, args, Call::Intrinsic);
            }
        }
    }

    void visit(const Cast *op) {
        if (op->type.is_scalar()) {
            expr = op;
        } else {
            Type t = op->type;
            t.width = new_width;
            expr = Cast::make(t, mutate(op->value));
        }
    }

    void visit(const Call *op) {
        Type t = op->type;
        t.width = new_width;

        // Don't mutate scalars
        if (op->type.is_scalar()) {
            expr = op;
        } else if (op->name == Call::interleave_vectors &&
                   op->call_type == Call::Intrinsic) {
            internal_assert(starting_lane >= 0 && starting_lane < lane_stride);
            if ((int)op->args.size() == lane_stride) {
                expr = op->args[starting_lane];
            } else if ((int)op->args.size() % lane_stride == 0) {
                // Pick up every lane-stride arg.
                std::vector<Expr> new_args(op->args.size() / lane_stride);
                for (size_t i = 0; i < new_args.size(); i++) {
                    new_args[i] = op->args[i*lane_stride + starting_lane];
                }
                expr = Call::make(t, Call::interleave_vectors, new_args, Call::Intrinsic);
            } else {
                // Interleave some vectors then deinterleave by some other factor...
                // Brute force!
                std::vector<Expr> args;
                args.push_back(op);
                for (int i = 0; i < new_width; i++) {
                    args.push_back(i*lane_stride + starting_lane);
                }
                expr = Call::make(t, Call::shuffle_vector, args, Call::Intrinsic);
            }
        } else if (op->name == Call::shuffle_vector &&
                   op->call_type == Call::Intrinsic) {
            // Extract every nth numeric arg to the shuffle.
            std::vector<Expr> args;
            args.push_back(op->args[0]);
            for (int i = 0; i < new_width; i++) {
                int idx = i * lane_stride + starting_lane + 1;
                internal_assert(idx >= 0 && idx < int(op->args.size()));
                args.push_back(op->args[idx]);
            }
            expr = Call::make(t, Call::shuffle_vector, args, Call::Intrinsic);
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
};

Expr extract_odd_lanes(Expr e, const Scope<int> &lets) {
    internal_assert(e.type().width % 2 == 0);
    Deinterleaver d(lets);
    d.starting_lane = 1;
    d.lane_stride = 2;
    d.new_width = e.type().width/2;
    e = d.mutate(e);
    return simplify(e);
}

Expr extract_even_lanes(Expr e, const Scope<int> &lets) {
    internal_assert(e.type().width % 2 == 0);
    Deinterleaver d(lets);
    d.starting_lane = 0;
    d.lane_stride = 2;
    d.new_width = (e.type().width+1)/2;
    e = d.mutate(e);
    return simplify(e);
}

Expr extract_even_lanes(Expr e) {
    internal_assert(e.type().width % 2 == 0);
    Scope<int> lets;
    return extract_even_lanes(e, lets);
}

Expr extract_odd_lanes(Expr e) {
    internal_assert(e.type().width % 2 == 0);
    Scope<int> lets;
    return extract_odd_lanes(e, lets);
}

Expr extract_mod3_lanes(Expr e, int lane, const Scope<int> &lets) {
    internal_assert(e.type().width % 3 == 0);
    Deinterleaver d(lets);
    d.starting_lane = lane;
    d.lane_stride = 3;
    d.new_width = (e.type().width+2)/3;
    e = d.mutate(e);
    return simplify(e);
}

Expr extract_lane(Expr e, int lane) {
    Scope<int> lets;
    Deinterleaver d(lets);
    d.starting_lane = lane;
    d.lane_stride = 0;
    d.new_width = 1;
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
        if (e.type().width <= num_lanes) {
            // Just scalarize
            return e;
        } else if (num_lanes == 2) {
            Expr a = extract_even_lanes(e, vector_lets);
            Expr b = extract_odd_lanes(e, vector_lets);
            return Call::make(e.type(), Call::interleave_vectors,
                              vec(a, b), Call::Intrinsic);
        } else if (num_lanes == 3) {
            Expr a = extract_mod3_lanes(e, 0, vector_lets);
            Expr b = extract_mod3_lanes(e, 1, vector_lets);
            Expr c = extract_mod3_lanes(e, 2, vector_lets);
            return Call::make(e.type(), Call::interleave_vectors,
                              vec(a, b, c), Call::Intrinsic);
        } else if (num_lanes == 4) {
            Expr a = extract_even_lanes(e, vector_lets);
            Expr b = extract_odd_lanes(e, vector_lets);
            Expr aa = extract_even_lanes(a, vector_lets);
            Expr ab = extract_odd_lanes(a, vector_lets);
            Expr ba = extract_even_lanes(b, vector_lets);
            Expr bb = extract_odd_lanes(b, vector_lets);
            return Call::make(e.type(), Call::interleave_vectors,
                              vec(aa, ab, ba, bb), Call::Intrinsic);
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
            if (value.type().width % 2 == 0) {
                result = T::make(op->name + ".even_lanes", extract_even_lanes(value, vector_lets), result);
                result = T::make(op->name + ".odd_lanes", extract_odd_lanes(value, vector_lets), result);
            }
            if (value.type().width % 3 == 0) {
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
            if (r && is_const(op->b, i)) {
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
            if (r && is_const(op->b, i)) {
                should_deinterleave = true;
                num_lanes = i;
                break;
            }
        }
        IRMutator::visit(op);
    }

    void visit(const Load *op) {
        bool old_should_deinterleave = should_deinterleave;
        int old_num_lanes = num_lanes;

        should_deinterleave = false;
        Expr idx = mutate(op->index);
        expr = Load::make(op->type, op->name, idx, op->image, op->param);
        if (should_deinterleave) {
            expr = deinterleave_expr(expr);
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

        stmt = Store::make(op->name, value, idx);

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
            std::vector<LetStmt> let_stmts;
            while (let) {
                let_stmts.push_back(*let);
                store = let->body.as<Store>();
                let = let->body.as<LetStmt>();
            }

            // There was no inner store.
            if (!store) goto fail;

            const Ramp *r0 = store->index.as<Ramp>();

            // It's not a store of a ramp index.
            if (!r0) goto fail;

            const int *stride_ptr = as_const_int(r0->stride);

            // The stride isn't a constant or is <= 0
            if (!stride_ptr || *stride_ptr < 1) goto fail;

            const int stride = *stride_ptr;
            const int width = r0->width;

            // Collect the rest of the stores.
            std::vector<Store> stores;
            stores.push_back(*store);
            Stmt rest = collect_strided_stores(op->rest, store->name,
                                               stride, let_stmts, stores);

            if (stride > 1) {
                // Wrong number of stores collected.
                if (stores.size() != (size_t)stride) goto fail;

                // Figure out the offset of each store relative to the first one.
                int min_offset = 0;
                std::vector<int> offsets(stride);
                for (int i = 0; i < stride; ++i) {
                    const Ramp *ri = stores[i].index.as<Ramp>();
                    internal_assert(ri);

                    // Mismatched store vector widths.
                    if (ri->width != width) goto fail;

                    Expr diff = simplify(ri->base - r0->base);
                    const int *offs = as_const_int(diff);

                    // Difference between bases is not a constant.
                    if (!offs) goto fail;

                    offsets[i] = *offs;
                    if (*offs < min_offset) {
                        min_offset = *offs;
                    }
                }

                // Bucket the stores by offset.
                Expr base;
                std::vector<Expr> args(stride);
                for (int i = 0; i < stride; ++i) {
                    int j = offsets[i] - min_offset;
                    if (j == 0) {
                        base = stores[i].index.as<Ramp>()->base;
                    }

                    // The offset is not between zero and the stride.
                    if (j < 0 || j >= stride) goto fail;

                    // We already have a store for this offset.
                    if (args[j].defined()) goto fail;

                    args[j] = stores[i].value;
                }

                // One of the stores should have had the minimum offset.
                internal_assert(base.defined());

                // Generate a single interleaving store.
                Type t = store->value.type();
                t.width = width*stride;
                Expr index = Ramp::make(base, make_one(Int(32)), t.width);
                Expr value = Call::make(t, Call::interleave_vectors, args, Call::Intrinsic);
                Stmt new_store = Store::make(store->name, value, index);

                // Continue recursively into the stuff that
                // collect_strided_stores didn't collect.
                stmt = Block::make(new_store, mutate(rest));
            } else {
                // Wrong number of stores collected.
                if (stores.size() != (size_t)width) goto fail;

                // This case only triggers if we are doing a sequence of
                // unit stride vector stores of strided vector loads. In
                // this case we can easily transpose the operations and
                // store an interleaved vector of unit stride loads.
                int min_offset = 0;
                std::vector<int> offsets(width);
                std::string load_name;
                Buffer load_image;
                Parameter load_param;
                for (int i = 0; i < width; ++i) {
                    const Ramp *ri = stores[i].index.as<Ramp>();
                    internal_assert(ri);

                    // Mismatched store vector widths.
                    if (ri->width != width) goto fail;

                    Expr diff = simplify(ri->base - r0->base);
                    const int *offs = as_const_int(diff);

                    // Difference between bases is not constant.
                    if (!offs) goto fail;

                    // Difference between bases is not a multiple of the width.
                    if (*offs % width != 0) goto fail;

                    offsets[i] = *offs;
                    if (*offs < min_offset) {
                        min_offset = *offs;
                    }

                    const Load *load = stores[i].value.as<Load>();
                    if (!load) goto fail;

                    const Ramp *ramp = load->index.as<Ramp>();
                    if (!ramp) goto fail;

                    // Load stride or width is not eqaul to the store width.
                    if (!is_const(ramp->stride, width) || ramp->width != width) goto fail;

                    if (i == 0) {
                        load_name  = load->name;
                        load_image = load->image;
                        load_param = load->param;
                    } else {
                        if (load->name != load_name) goto fail;
                    }
                }

                // Gather the args for interleaving.
                Expr base;
                Type t = store->value.type();
                std::vector<Expr> args(width);
                for (int i = 0; i < width; ++i) {
                    int j = (offsets[i] - min_offset) / width;
                    if (j == 0) {
                        base = stores[i].index.as<Ramp>()->base;
                    }

                    // The offset is not between zero and the stride.
                    if (j < 0 || j >= width) goto fail;

                    // We already have a store for this offset.
                    if (args[j].defined()) goto fail;

                    args[j] = Load::make(t, load_name, stores[i].index, load_image, load_param);
                }

                // Generate a single interleaving store.
                t.width = width*width;
                Expr index = Ramp::make(base, make_one(Int(32)), t.width);
                Expr value = Call::make(t, Call::interleave_vectors, args, Call::Intrinsic);
                Stmt new_store = Store::make(store->name, value, index);

                // Continue recursively into the stuff that
                // collect_strided_stores didn't collect.
                stmt = Block::make(new_store, mutate(rest));
            }

            // Rewrap the let statements we pulled off.
            while (!let_stmts.empty()) {
                LetStmt let = let_stmts.back();
                stmt = LetStmt::make(let.name, let.value, stmt);
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
    s = flatten_blocks(s);
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

    check(Load::make(ramp.type(), "buf", ramp, Buffer(), Parameter()),
          Load::make(ramp_a.type(), "buf", ramp_a, Buffer(), Parameter()),
          Load::make(ramp_b.type(), "buf", ramp_b, Buffer(), Parameter()));

    std::cout << "deinterleave_vector test passed" << std::endl;
}

}
}
