#include <map>

#include "CSE.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Scope.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

namespace {

// Some expressions are not worth lifting out into lets, even if they
// occur redundantly many times. They may also be illegal to lift out
// (e.g. calls with side-effects).
// This list should at least avoid lifting the same cases as that of the
// simplifier for lets, otherwise CSE and the simplifier will fight each
// other pointlessly.
bool should_extract(const Expr &e, bool lift_all) {
    if (is_const(e)) {
        return false;
    }

    if (e.as<Variable>()) {
        return false;
    }

    if (const Call *c = e.as<Call>()) {
        if (c->type == type_of<ApproximationPrecision *>()) {
            return false;
        }
    }

    if (lift_all) {
        return true;
    }

    if (const Broadcast *a = e.as<Broadcast>()) {
        return should_extract(a->value, false);
    }

    if (const Cast *a = e.as<Cast>()) {
        return should_extract(a->value, false);
    }

    if (const Add *a = e.as<Add>()) {
        return !(is_const(a->a) || is_const(a->b));
    }

    if (const Sub *a = e.as<Sub>()) {
        return !(is_const(a->a) || is_const(a->b));
    }

    if (const Mul *a = e.as<Mul>()) {
        return !(is_const(a->a) || is_const(a->b));
    }

    if (const Div *a = e.as<Div>()) {
        return !(is_const(a->a) || is_const(a->b));
    }

    if (const Ramp *a = e.as<Ramp>()) {
        return !is_const(a->stride);
    }

    return true;
}

// A global-value-numbering of expressions. Returns canonical form of
// the Expr and writes out a global value numbering as a side-effect.
class GVN : public IRMutator {
public:
    struct Entry {
        Expr expr;
        bool strict_float = false;
        int use_count = 0;
        // All consumer Exprs for which this is the last child Expr.
        map<Expr, int, IRGraphDeepCompare> uses;
        Entry(const Expr &e)
            : expr(e) {
        }
    };
    vector<std::unique_ptr<Entry>> entries;

    map<Expr, int, ExprCompare> shallow_numbering, output_numbering;
    map<Expr, int, IRGraphDeepCompare> leaves;

    int number = 0;

    Stmt mutate(const Stmt &s) override {
        internal_error << "Can't call GVN on a Stmt: " << s << "\n";
        return Stmt();
    }

    Expr mutate(const Expr &e) override {
        // Early out if we've already seen this exact Expr.
        {
            auto iter = shallow_numbering.find(e);
            if (iter != shallow_numbering.end()) {
                number = iter->second;
                return entries[number]->expr;
            }
        }

        // We haven't seen this exact Expr before. Rebuild it using
        // things already in the numbering.
        number = -1;
        Expr new_e = IRMutator::mutate(e);

        // 'number' is now set to the numbering for the last child of
        // this Expr (or -1 if there are no children). Next we see if
        // that child has an identical parent to this one.

        auto &use_map = number == -1 ? leaves : entries[number]->uses;
        auto p = use_map.emplace(new_e, (int)entries.size());
        auto iter = p.first;
        bool novel = p.second;
        if (novel) {
            // This is a never-before-seen Expr
            number = (int)entries.size();
            iter->second = number;
            entries.emplace_back(new Entry(new_e));
        } else {
            // This child already has a syntactically-equal parent
            number = iter->second;
            new_e = entries[number]->expr;
        }

        // Memorize this numbering for the old and new forms of this Expr
        shallow_numbering[e] = number;
        output_numbering[new_e] = number;
        return new_e;
    }
};

/** Fill in the use counts in a global value numbering. */
class ComputeUseCounts : public IRGraphVisitor {
    GVN &gvn;
    bool lift_all;
    bool in_strict_float{false};

public:
    ComputeUseCounts(GVN &g, bool l)
        : gvn(g), lift_all(l) {
    }

    using IRGraphVisitor::include;
    using IRGraphVisitor::visit;

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::strict_float)) {
            ScopedValue<bool> bind(in_strict_float, true);
            IRGraphVisitor::visit(op);
        } else {
            IRGraphVisitor::visit(op);
        }
    }

    void include(const Expr &e) override {
        // If it's not the sort of thing we want to extract as a let,
        // just use the generic visitor to increment use counts for
        // the children.
        debug(4) << "Include: " << e
                 << "; should extract: " << should_extract(e, lift_all) << "\n";
        if (!should_extract(e, lift_all)) {
            e.accept(this);
            return;
        }

        // Find this thing's number.
        auto iter = gvn.output_numbering.find(e);
        if (iter != gvn.output_numbering.end()) {
            auto &entry = gvn.entries[iter->second];
            entry->use_count++;
            entry->strict_float |= in_strict_float;
        } else {
            internal_error << "Expr not in shallow numbering: " << e << "\n";
        }

        // Visit the children if we haven't been here before.
        IRGraphVisitor::include(e);
    }
};

/** Rebuild an expression using a map of replacements. Works on graphs without exploding. */
class Replacer : public IRGraphMutator {
public:
    Replacer() = default;
    Replacer(const map<Expr, Expr, ExprCompare> &r)
        : IRGraphMutator() {
        expr_replacements = r;
    }

    void erase(const Expr &e) {
        expr_replacements.erase(e);
    }
};

class RemoveLets : public IRGraphMutator {
    using IRGraphMutator::visit;

    Scope<Expr> scope;

    Expr visit(const Variable *op) override {
        if (const Expr *e = scope.find(op->name)) {
            return *e;
        } else {
            return op;
        }
    }

    Expr visit(const Let *op) override {
        Expr new_value = mutate(op->value);
        // When we enter a let, we invalidate all cached mutations
        // with values that reference this var due to shadowing. When
        // we leave a let, we similarly invalidate any cached
        // mutations we learned on the inside that reference the var.

        // A blunt way to handle this is to temporarily invalidate
        // *all* mutations, so we never see the same Expr node
        // on the inside and outside of a Let.
        decltype(expr_replacements) tmp;
        tmp.swap(expr_replacements);
        ScopedBinding<Expr> bind(scope, op->name, new_value);
        auto result = mutate(op->body);
        tmp.swap(expr_replacements);
        return result;
    }
};

class CSEEveryExprInStmt : public IRMutator {
    bool lift_all;
    using IRMutator::visit;

    Stmt visit(const Store *op) override {
        // It's important to do CSE jointly on the index and value in
        // a store to stop:
        // f[x] = f[x] + y
        // from turning into
        // f[x] = f[z] + y
        // due to the two equal x's indices being CSE'd differently due to the presence of y.
        Expr dummy = Call::make(Int(32), Call::bundle, {op->value, op->index}, Call::PureIntrinsic);
        dummy = common_subexpression_elimination(dummy, lift_all);
        vector<pair<string, Expr>> lets;
        while (const Let *let = dummy.as<Let>()) {
            lets.emplace_back(let->name, let->value);
            dummy = let->body;
        }
        const Call *bundle = Call::as_intrinsic(dummy, {Call::bundle});
        internal_assert(bundle && bundle->args.size() == 2);
        Stmt s = Store::make(op->name, bundle->args[0], bundle->args[1],
                             op->param, mutate(op->predicate), op->alignment);
        for (const auto &[var, value] : reverse_view(lets)) {
            s = LetStmt::make(var, value, s);
        }
        return s;
    }

public:
    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        return common_subexpression_elimination(e, lift_all);
    }

    CSEEveryExprInStmt(bool l)
        : lift_all(l) {
    }
};

}  // namespace

Expr common_subexpression_elimination(const Expr &e_in, bool lift_all) {
    Expr e = e_in;

    // Early-out for trivial cases.
    if (is_const(e) || e.as<Variable>()) {
        return e;
    }

    debug(4) << "\n\n\nInput to CSE " << e << "\n";

    e = RemoveLets().mutate(e);

    debug(4) << "After removing lets: " << e << "\n";

    // CSE is run on unsanitized Exprs from the user, and may contain Vars with
    // the same name as the temporaries we intend to introduce. Find any such
    // Vars so that we know not to use those names.
    class UniqueNameProvider : public IRGraphVisitor {
        using IRGraphVisitor::visit;

        const char prefix = 't';  // Annoyingly, this can't be static because this is a local class.

        void visit(const Variable *op) override {
            // It would be legal to just add all names found to the tracked set,
            // but because we know the form of the new names we're going to
            // introduce, we can save some time by only adding names that could
            // plausibly collide. In the vast majority of cases, this check will
            // result in the set being empty.
            if (op->name.size() > 1 &&
                op->name[0] == prefix &&
                isdigit(op->name[1])) {
                vars.insert(op->name);
            }
        }
        std::set<string> vars;

    public:
        string make_unique_name() {
            string name;
            do {
                name = unique_name(prefix);
            } while (vars.count(name));
            return name;
        }
    } namer;
    e.accept(&namer);

    GVN gvn;
    e = gvn.mutate(e);

    ComputeUseCounts count_uses(gvn, lift_all);
    count_uses.include(e);

    debug(4) << "Canonical form without lets " << e << "\n";

    // Figure out which ones we'll pull out as lets and variables.
    vector<std::tuple<string, Expr, bool>> lets;
    vector<Expr> new_version(gvn.entries.size());
    map<Expr, Expr, ExprCompare> replacements;
    for (size_t i = 0; i < gvn.entries.size(); i++) {
        const auto &e = gvn.entries[i];
        if (e->use_count > 1) {
            string name = namer.make_unique_name();
            lets.emplace_back(name, e->expr, e->strict_float);
            // Point references to this expr to the variable instead.
            replacements[e->expr] = Variable::make(e->expr.type(), name);
        }
        debug(4) << i << ": " << e->expr << ", " << e->use_count << "\n";
    }

    // Rebuild the expr to include references to the variables:
    Replacer replacer(replacements);
    e = replacer.mutate(e);

    debug(4) << "With variables " << e << "\n";

    // Wrap the final expr in the lets.
    for (const auto &[var, value, expr_strict_float] : reverse_view(lets)) {
        // Drop this variable as an acceptable replacement for this expr.
        replacer.erase(value);
        // Use containing lets in the value.
        if (expr_strict_float) {
            e = Let::make(var, strict_float(replacer.mutate(value)), e);
        } else {
            e = Let::make(var, replacer.mutate(value), e);
        }
    }

    debug(4) << "With lets: " << e << "\n";

    return e;
}

Stmt common_subexpression_elimination(const Stmt &s, bool lift_all) {
    return CSEEveryExprInStmt(lift_all).mutate(s);
}

// Testing code.

namespace {

// Normalize all names in an expr so that expr compares can be done
// without worrying about mere name differences.
class NormalizeVarNames : public IRMutator {
    int counter = 0;

    map<string, string> new_names;

    using IRMutator::visit;

    Expr visit(const Variable *var) override {
        map<string, string>::iterator iter = new_names.find(var->name);
        if (iter == new_names.end()) {
            return var;
        } else {
            return Variable::make(var->type, iter->second);
        }
    }

    Expr visit(const Let *let) override {
        string new_name = "t" + std::to_string(counter++);
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
    NormalizeVarNames n;
    result = n.mutate(result);
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
Expr ssa_block(vector<Expr> exprs) {
    Expr e = exprs.back();
    for (size_t i = exprs.size() - 1; i > 0; i--) {
        string name = "t" + std::to_string(i - 1);
        e = Let::make(name, exprs[i - 1], e);
    }
    return e;
}

}  // namespace

void cse_test() {
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
        Expr load = Load::make(Int(32), "buf", index, Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        Expr pred_load = Load::make(Int(32), "buf", index, Buffer<>(), Parameter(), pred, ModulusRemainder());
        e = select(x * y > 10, x * y + 2, x * y + 3 + load) + pred_load;

        Expr t2 = Variable::make(Bool(), "t2");
        Expr cse_load = Load::make(Int(32), "buf", t[3], Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        Expr cse_pred_load = Load::make(Int(32), "buf", t[3], Buffer<>(), Parameter(), t2, ModulusRemainder());
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
        Expr load = Load::make(Int(32), "buf", index, Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        Expr pred_load = Load::make(Int(32), "buf", index, Buffer<>(), Parameter(), pred, ModulusRemainder());
        e = select(x * y > 10, x * y + 2, x * y + 3 + pred_load) + pred_load;

        Expr t2 = Variable::make(Bool(), "t2");
        Expr cse_load = Load::make(Int(32), "buf", select(t2, t[1] + 2, t[1] + 10), Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        Expr cse_pred_load = Load::make(Int(32), "buf", select(t2, t[1] + 2, t[1] + 10), Buffer<>(), Parameter(), t2, ModulusRemainder());
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

    debug(0) << "common_subexpression_elimination test passed\n";
}

}  // namespace Internal
}  // namespace Halide
