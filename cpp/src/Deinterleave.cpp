#include "Deinterleave.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRPrinter.h"
#include "Log.h"
#include "Scope.h"

using std::pair;
using std::make_pair;

namespace Halide {
namespace Internal {

class Deinterleaver : public IRMutator {
public:
    bool even_lanes;
    int new_width;
    bool failed;
private:
    Scope<int> internal;

    void visit(const Broadcast *op) {
        expr = new Broadcast(op->value, new_width);
    }

    void visit(const Load *op) {
        Type t = op->type;
        t.width = new_width;
        expr = new Load(t, op->name, mutate(op->index), op->image, op->param);
    }

    void visit(const Ramp *op) {
        if (even_lanes) {
            expr = new Ramp(op->base, op->stride * 2, new_width);
        } else {
            expr = new Ramp(op->base + op->stride, op->stride * 2, new_width);
        }
    }
    
    void visit(const Variable *op) {
        Type t = op->type;
        t.width = new_width;
        if (internal.contains(op->name)) {
            expr = new Variable(t, op->name, op->param, op->reduction_domain);
        } else {
            // Uh-oh, we don't know how to deinterleave this vector expression
            // Make llvm do it
            expr = new Call(t, even_lanes ? "extract even lanes" : "extract odd lanes", vec<Expr>(op));
        }
    }

    void visit(const Let *op) {
        Expr value = mutate(op->value);
        internal.push(op->name, 0);
        Expr body = mutate(op->body);
        internal.pop(op->name);
        expr = new Let(op->name, value, body);
    }
};

Expr extract_odd_lanes(Expr e) {
    Deinterleaver d;
    d.even_lanes = false;
    d.new_width = e.type().width/2;
    d.failed = false;
    e = d.mutate(e);
    if (d.failed) return Expr();
    else return simplify(e);
}

Expr extract_even_lanes(Expr e) {
    Deinterleaver d;
    d.even_lanes = true;
    d.new_width = (e.type().width+1)/2;
    d.failed = false;
    e = d.mutate(e);
    if (d.failed) return Expr();
    else return simplify(e);
}

namespace {
void check(Expr a, Expr even, Expr odd) {
    a = simplify(a);
    Expr correct_even = extract_even_lanes(a);
    Expr correct_odd = extract_odd_lanes(a);
    if (!equal(correct_even, even)) {
        assert(false);
    }
    if (!equal(correct_odd, odd)) {
        assert(false);
    }
}
}

void deinterleave_vector_test() {
    std::pair<Expr, Expr> result;
    Expr x = new Variable(Int(32), "x");
    Expr ramp = new Ramp(x + 4, 3, 7);
    Expr ramp_a = new Ramp(x + 4, 6, 4);
    Expr ramp_b = new Ramp(x + 7, 6, 3);
    Expr broadcast = new Broadcast(x + 4, 16);
    Expr broadcast_a = new Broadcast(x + 4, 8);
    Expr broadcast_b = broadcast_a;

    check(ramp, ramp_a, ramp_b);
    check(broadcast, broadcast_a, broadcast_b);
    
    check(new Load(ramp.type(), "buf", ramp, Buffer(), Parameter()), 
          new Load(ramp_a.type(), "buf", ramp_a, Buffer(), Parameter()), 
          new Load(ramp_b.type(), "buf", ramp_b, Buffer(), Parameter()));

    std::cout << "deinterleave_vector test passed" << std::endl;
}

}
}
