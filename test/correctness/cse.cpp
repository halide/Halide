#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Normalize all names in an expr so that expr compares can be done
// without worrying about mere name differences.
class NormalizeVarNames : public IRMutator {
    int counter = 0;

    std::map<std::string, std::string> new_names;

    using IRMutator::visit;

    Expr visit(const Variable *var) override {
        std::map<std::string, std::string>::iterator iter = new_names.find(var->name);
        if (iter == new_names.end()) {
            return var;
        } else {
            return Variable::make(var->type, iter->second);
        }
    }

    Expr visit(const Let *let) override {
        std::string new_name = "t" + std::to_string(counter++);
        new_names[let->name] = new_name;
        Expr value = mutate(let->value);
        Expr body = mutate(let->body);
        return Let::make(new_name, value, body);
    }

public:
    NormalizeVarNames() = default;
};

void check(const Expr &in, const Expr &correct) {
    Expr result = common_subexpression_elimination(in);
    result = NormalizeVarNames()(result);
    internal_assert(equal(result, correct))
        << "Incorrect CSE:\n"
        << in
        << "\nbecame:\n"
        << result
        << "\ninstead of:\n"
        << correct << "\n";
}

// Construct a nested block of lets. Variables of the form "tn" refer
// to expr n in the vector.
Expr ssa_block(std::vector<Expr> exprs) {
    Expr e = exprs.back();
    for (size_t i = exprs.size() - 1; i > 0; i--) {
        std::string name = "t" + std::to_string(i - 1);
        e = Let::make(name, exprs[i - 1], e);
    }
    return e;
}

Expr masked(const Expr &c, const Expr &e) {
    return Call::make(e.type(), Call::if_then_else, {c, e}, Call::PureIntrinsic);
}

Expr load_buf(const Expr &index) {
    return Load::make(Int(32), "buf", index, Buffer<>(), Parameter(), const_true(), ModulusRemainder(), false);
}

}  // namespace

int main(int argc, char **argv) {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    Expr t[32], tf[32];
    for (int i = 0; i < 32; i++) {
        t[i] = Variable::make(Int(32), "t" + std::to_string(i));
        tf[i] = Variable::make(Float(32), "t" + std::to_string(i));
    }
    Expr e, correct;

    // This is fine as-is.
    e = ssa_block({sin(x), tf[0] * tf[0]});
    check(e, e);

    // Test a simple case.
    e = ((x * x + x) * (x * x + x)) + x * x;
    e += e;
    correct = ssa_block({x * x,               // x*x
                         t[0] + x,            // x*x + x
                         t[1] * t[1] + t[0],  // (x*x + x)*(x*x + x) + x*x
                         t[2] + t[2]});
    check(e, correct);

    // Check for idempotence (also checks a case with lets)
    check(correct, correct);

    // Check a case with redundant lets
    e = ssa_block({x * x,
                   x * x,
                   t[0] / t[1],
                   t[1] / t[1],
                   t[2] % t[3],
                   (t[4] + x * x) + x * x});
    correct = ssa_block({x * x,
                         t[0] / t[0],
                         (t[1] % t[1] + t[0]) + t[0]});
    check(e, correct);

    // Check a case with nested lets with shared subexpressions
    // between the lets, and repeated names.
    Expr e1 = ssa_block({x * x,                 // a = x*x
                         t[0] + x,              // b = a + x
                         t[1] * t[1] * t[0]});  // c = b * b * a
    Expr e2 = ssa_block({x * x,                 // a again
                         t[0] - x,              // d = a - x
                         t[1] * t[1] * t[0]});  // e = d * d * a
    e = ssa_block({e1 + x * x,                  // f = c + a
                   e1 + e2,                     // g = c + e
                   t[0] + t[0] * t[1]});        // h = f + f * g

    correct = ssa_block({x * x,                                        // t0 = a = x*x
                         t[0] + x,                                     // t1 = b = a + x     = t0 + x
                         t[1] * t[1] * t[0],                           // t2 = c = b * b * a = t1 * t1 * t0
                         t[2] + t[0],                                  // t3 = f = c + a     = t2 + t0
                         t[0] - x,                                     // t4 = d = a - x     = t0 - x
                         t[3] + t[3] * (t[2] + t[4] * t[4] * t[0])});  // h (with g substituted in)
    check(e, correct);

    // Test it scales OK.
    e = x;
    for (int i = 0; i < 100; i++) {
        e = e * e + e + i;
        e = e * e - e * i;
    }
    Expr result = common_subexpression_elimination(e);

    {
        Expr pred = x * x + y * y > 0;
        Expr index = select(x * x + y * y > 0, x * x + y * y + 2, x * x + y * y + 10);
        Expr load = Load::make(Int(32), "buf", index);
        Expr pred_load = Load::make(Int(32), "buf", index, Buffer<>(), Parameter(), pred, ModulusRemainder(), false);
        e = select(x * y > 10, x * y + 2, x * y + 3 + load) + pred_load;

        Expr t2 = Variable::make(Bool(), "t2");
        Expr cse_load = Load::make(Int(32), "buf", t[3]);
        Expr cse_pred_load = Load::make(Int(32), "buf", t[3], Buffer<>(), Parameter(), t2, ModulusRemainder(), false);
        correct = ssa_block({x * y,
                             x * x + y * y,
                             t[1] > 0,
                             select(t2, t[1] + 2, t[1] + 10),
                             select(t[0] > 10, t[0] + 2, t[0] + 3 + cse_load) + cse_pred_load});

        check(e, correct);
    }

    {
        Expr pred = x * x + y * y > 0;
        Expr index = select(x * x + y * y > 0, x * x + y * y + 2, x * x + y * y + 10);
        Expr load = Load::make(Int(32), "buf", index);
        Expr pred_load = Load::make(Int(32), "buf", index, Buffer<>(), Parameter(), pred, ModulusRemainder(), false);
        e = select(x * y > 10, x * y + 2, x * y + 3 + pred_load) + pred_load;

        Expr t2 = Variable::make(Bool(), "t2");
        Expr cse_load = Load::make(Int(32), "buf", select(t2, t[1] + 2, t[1] + 10));
        Expr cse_pred_load = Load::make(Int(32), "buf", select(t2, t[1] + 2, t[1] + 10), Buffer<>(), Parameter(), t2, ModulusRemainder(), false);
        correct = ssa_block({x * y,
                             x * x + y * y,
                             t[1] > 0,
                             cse_pred_load,
                             select(t[0] > 10, t[0] + 2, t[0] + 3 + t[3]) + t[3]});

        check(e, correct);
    }

    {
        Expr halide_func = Call::make(Int(32), "dummy", {0}, Call::Halide);
        e = halide_func * halide_func;
        Expr t0 = Variable::make(halide_func.type(), "t0");
        // It's okay to CSE Halide call within an expr
        correct = Let::make("t0", halide_func, t0 * t0);
        check(e, correct);
    }

    {
        // An if_then_else only evaluates the branch it takes, so a load
        // that is only used under different conditions stays in the
        // branches...
        Expr load = load_buf(x * y);
        e = masked(x > 0, load) + masked(y > 0, load);
        check(e, e);

        // ...unless it's also used unconditionally.
        e = masked(x > 0, load) + load;
        correct = ssa_block({load, masked(x > 0, t[0]) + t[0]});
        check(e, correct);

        // Pure expressions may still be lifted out of branches.
        e = masked(x > 0, (x * y) * load) + masked(y > 0, (x * y) * 2);
        correct = ssa_block({x * y,
                             masked(x > 0, t[0] * load_buf(t[0])) +
                                 masked(y > 0, t[0] * 2)});
        check(e, correct);

        // An if_then_else containing a load isn't safe to speculate
        // either: its condition may only be part of what keeps the load in
        // bounds.
        Expr inner = masked(x > y, load);
        e = masked(x > 0, inner) + masked(y > 0, inner);
        check(e, e);

        // Repeated loads within a branch are CSE'd there.
        e = masked(x > 0, load * load + x) + masked(y > 0, load);
        correct = masked(x > 0, ssa_block({load, t[0] * t[0] + x})) + masked(y > 0, load);
        check(e, correct);
    }

    printf("Success!\n");
    return 0;
}
