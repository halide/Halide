#include "Deinterleave.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRPrinter.h"
#include "ModulusRemainder.h"
#include "Debug.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

using std::pair;
using std::make_pair;

class Deinterleaver : public IRMutator {
public:
    int starting_lane;
    int new_width;
    int lane_stride;
private:
    Scope<int> internal;

    using IRMutator::visit;

    void visit(const Broadcast *op) {
        if (new_width == 1) {
            expr = op->value;
        } else {
            expr = Broadcast::make(op->value, new_width);
        }
    }

    void visit(const Load *op) {
        Type t = op->type;
        t.width = new_width;
        expr = Load::make(t, op->name, mutate(op->index), op->image, op->param);
    }

    void visit(const Ramp *op) {
        expr = op->base + starting_lane * op->stride;
        if (new_width > 1) {
            expr = Ramp::make(expr, op->stride * lane_stride, new_width);
        }
    }

    void visit(const Variable *op) {
        Type t = op->type;
        t.width = new_width;
        if (internal.contains(op->name)) {
            expr = Variable::make(t, op->name, op->param, op->reduction_domain);
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

    void visit(const Cast *op) {
        Type t = op->type;
        t.width = new_width;
        expr = Cast::make(t, mutate(op->value));
    }

    void visit(const Call *op) {
        Type t = op->type;
        t.width = new_width;

        // Vector calls are always parallel across the lanes, so we
        // can just deinterleave the args.
        std::vector<Expr> args(op->args.size());
        for (size_t i = 0; i < args.size(); i++) {
            args[i] = mutate(op->args[i]);
        }

        expr = Call::make(t, op->name, args, op->call_type,
                          op->func, op->value_index, op->image, op->param);
    }

    void visit(const Let *op) {
        Expr value = mutate(op->value);
        internal.push(op->name, 0);
        Expr body = mutate(op->body);
        internal.pop(op->name);
        expr = Let::make(op->name, value, body);
    }
};

Expr extract_odd_lanes(Expr e) {
    Deinterleaver d;
    d.starting_lane = 1;
    d.lane_stride = 2;
    d.new_width = e.type().width/2;
    e = d.mutate(e);
    return simplify(e);
}

Expr extract_even_lanes(Expr e) {
    Deinterleaver d;
    d.starting_lane = 0;
    d.lane_stride = 2;
    d.new_width = (e.type().width+1)/2;
    e = d.mutate(e);
    return simplify(e);
}

Expr extract_lane(Expr e, int lane) {
    Deinterleaver d;
    d.starting_lane = lane;
    d.lane_stride = 0;
    d.new_width = 1;
    e = d.mutate(e);
    return simplify(e);
}

class Interleaver : public IRMutator {
    Scope<ModulusRemainder> alignment_info;

    using IRMutator::visit;

    void visit(const Let *op) {
        Expr value = mutate(op->value);
        if (value.type() == Int(32)) alignment_info.push(op->name, modulus_remainder(value));
        Expr body = mutate(op->body);
        if (value.type() == Int(32)) alignment_info.pop(op->name);
        if (value.same_as(op->value) && body.same_as(op->body)) {
            expr = op;
        } else {
            expr = Let::make(op->name, value, body);
        }
    }

    void visit(const LetStmt *op) {
        Expr value = mutate(op->value);
        if (value.type() == Int(32)) alignment_info.push(op->name,
                                                         modulus_remainder(value, alignment_info));
        Stmt body = mutate(op->body);
        if (value.type() == Int(32)) alignment_info.pop(op->name);
        if (value.same_as(op->value) && body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, value, body);
        }
    }

    void visit(const Select *op) {
        Expr condition = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        const EQ *eq = condition.as<EQ>();
        const Mod *mod = eq ? eq->a.as<Mod>() : NULL;
        const Ramp *ramp = mod ? mod->a.as<Ramp>() : NULL;
        if (ramp && ramp->width > 2 && is_one(ramp->stride) && is_const(eq->b) && is_two(mod->b)) {
            debug(3) << "Detected interleave vector pattern. Deinterleaving.\n";
            ModulusRemainder mod_rem = modulus_remainder(ramp->base, alignment_info);
            debug(3) << "Base is congruent to " << mod_rem.remainder << " modulo " << mod_rem.modulus << "\n";
            Expr a, b;
            bool base_is_even = ((mod_rem.modulus & 1) == 0) && ((mod_rem.remainder & 1) == 0);
            bool base_is_odd  = ((mod_rem.modulus & 1) == 0) && ((mod_rem.remainder & 1) == 1);
            if ((is_zero(eq->b) && base_is_even) ||
                (is_one(eq->b) && base_is_odd)) {
                a = extract_even_lanes(true_value);
                b = extract_odd_lanes(false_value);
            } else if ((is_one(eq->b) && base_is_even) ||
                       (is_zero(eq->b) && base_is_odd)) {
                a = extract_even_lanes(false_value);
                b = extract_odd_lanes(true_value);
            }

            if (a.defined() && b.defined()) {
                expr = Call::make(op->type, Call::interleave_vectors, vec(a, b), Call::Intrinsic);
                return;
            }
        }

        if (condition.same_as(op->condition) &&
            true_value.same_as(op->true_value) &&
            false_value.same_as(op->false_value)) {
            expr = op;
        } else {
            expr = Select::make(condition, true_value, false_value);
        }
    }
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
        std::cerr << correct_even << " != " << even << "\n";
        assert(false);
    }
    if (!equal(correct_odd, odd)) {
        std::cerr << correct_odd << " != " << odd << "\n";
        assert(false);
    }
}
}

void deinterleave_vector_test() {
    std::pair<Expr, Expr> result;
    Expr x = Variable::make(Int(32), "x");
    Expr ramp = Ramp::make(x + 4, 3, 7);
    Expr ramp_a = Ramp::make(x + 4, 6, 4);
    Expr ramp_b = Ramp::make(x + 7, 6, 3);
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
