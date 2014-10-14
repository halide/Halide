#include <map>

#include "CSE.h"
#include "IRMutator.h"
#include "IREquality.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::map;
using std::pair;

namespace {

// Some expressions are not worth lifting out into lets, even if they
// occur redundantly many times. This list should mirror the list in
// the simplifier for lets, otherwise they'll just fight with each
// other pointlessly.
bool should_extract(Expr e) {
    if (is_const(e)) {
        return false;
    }

    if (e.as<Variable>()) {
        return false;
    }

    if (const Broadcast *a = e.as<Broadcast>()) {
        return should_extract(a->value);
    }

    if (const Cast *a = e.as<Cast>()) {
        return should_extract(a->value);
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
        int use_count;
    };
    vector<Entry> entries;

    typedef map<ExprWithCompareCache, int> CacheType;
    CacheType numbering;

    map<Expr, int, ExprCompare> shallow_numbering;

    Scope<int> let_substitutions;
    int number;

    IRCompareCache cache;

    GVN() : number(0), cache(8) {}

    Stmt mutate(Stmt s) {
        internal_error << "Can't call GVN on a Stmt: " << s << "\n";
        return Stmt();
    }

    ExprWithCompareCache with_cache(Expr e) {
        return ExprWithCompareCache(e, &cache);
    }

    Expr mutate(Expr e) {
        // Early out if we've already seen this exact Expr.
        {
            map<Expr, int, ExprCompare>::iterator iter = shallow_numbering.find(e);
            if (iter != shallow_numbering.end()) {
                number = iter->second;
                return entries[number].expr;
            }
        }

        // If e is a var, check if it has been redirected to an existing numbering.
        if (const Variable *var = e.as<Variable>()) {
            if (let_substitutions.contains(var->name)) {
                number = let_substitutions.get(var->name);
                return entries[number].expr;
            }
        }

        // If e already has an entry, return that.
        CacheType::iterator iter = numbering.find(with_cache(e));
        if (iter != numbering.end()) {
            number = iter->second;
            shallow_numbering[e] = number;
            return entries[number].expr;
        }

        // Rebuild using things already in the numbering.
        Expr old_e = e;
        e = IRMutator::mutate(e);

        // See if it's there in another form after being rebuilt
        // (e.g. because it was a let variable).
        iter = numbering.find(with_cache(e));
        if (iter != numbering.end()) {
            number = iter->second;
            shallow_numbering[old_e] = number;
            return entries[number].expr;
        }

        // Add it to the numbering.
        Entry entry = {e, 0};
        number = (int)entries.size();
        numbering[with_cache(e)] = number;
        shallow_numbering[e] = number;
        entries.push_back(entry);
        return e;
    }


    using IRMutator::visit;

    void visit(const Let *let) {
        // Visit the value and add it to the numbering.
        Expr value = mutate(let->value);

        // Make references to the variable point to the value instead.
        let_substitutions.push(let->name, number);

        // Visit the body and add it to the numbering.
        Expr body = mutate(let->body);

        let_substitutions.pop(let->name);

        // Just return the body. We've removed the Let.
        expr = body;
    }

};

/** Fill in the use counts in a global value numbering. */
class ComputeUseCounts : public IRGraphVisitor {
    GVN &gvn;
public:
    ComputeUseCounts(GVN &g) : gvn(g) {}

    using IRGraphVisitor::include;

    void include(const Expr &e) {
        // If it's not the sort of thing we want to extract as a let,
        // just use the generic visitor to increment use counts for
        // the children.
        if (!should_extract(e)) {
            e.accept(this);
            return;
        }

        // Find this thing's number.
        map<Expr, int, ExprCompare>::iterator iter = gvn.shallow_numbering.find(e);
        if (iter != gvn.shallow_numbering.end()) {
            GVN::Entry &entry = gvn.entries[iter->second];
            entry.use_count++;
        }

        // Visit the children if we haven't been here before.
        if (!visited.count(e.ptr)) {
            visited.insert(e.ptr);
            e.accept(this);
        }
    }
};

/** Rebuild an expression using a map of replacements. Works on graphs without exploding. */
class Replacer : public IRMutator {
public:
    map<Expr, Expr, ExprCompare> replacements;
    Replacer(const map<Expr, Expr, ExprCompare> &r) : replacements(r) {}

    using IRMutator::mutate;

    Expr mutate(Expr e) {
        map<Expr, Expr, ExprCompare>::iterator iter = replacements.find(e);

        if (iter != replacements.end()) {
            return iter->second;
        }

        // Rebuild it, replacing children.
        Expr new_e = IRMutator::mutate(e);

        // In case we encounter this expr again.
        replacements[e] = new_e;

        return new_e;
    }
};

} // namespace

Expr common_subexpression_elimination(Expr e) {

    // Early-out for trivial cases.
    if (is_const(e) || e.as<Variable>()) return e;

    debug(4) << "\n\n\nInput to letify " << e << "\n";

    GVN gvn;
    e = gvn.mutate(e);

    ComputeUseCounts count_uses(gvn);
    count_uses.include(e);

    debug(4) << "Canonical form without lets " << e << "\n";

    // Figure out which ones we'll pull out as lets and variables.
    vector<pair<string, Expr> > lets;
    vector<Expr> new_version(gvn.entries.size());
    map<Expr, Expr, ExprCompare> replacements;
    for (size_t i = 0; i < gvn.entries.size(); i++) {
        const GVN::Entry &e = gvn.entries[i];
        Expr old = e.expr;
        if (e.use_count > 1) {
            string name = unique_name('t');
            lets.push_back(make_pair(name, e.expr));
            // Point references to this expr to the variable instead.
            replacements[e.expr] = Variable::make(e.expr.type(), name);
        }
        debug(4) << i << ": " << e.expr << ", " << e.use_count << "\n";
    }

    // Rebuild the expr to include references to the variables:
    Replacer replacer(replacements);
    e = replacer.mutate(e);

    debug(4) << "With variables " << e << "\n";

    // Wrap the final expr in the lets.
    for (size_t i = lets.size(); i > 0; i--) {
        Expr value = lets[i-1].second;
        // Drop this variable as an acceptible replacement for this expr.
        replacer.replacements.erase(value);
        // Use containing lets in the value.
        value = replacer.mutate(lets[i-1].second);
        e = Let::make(lets[i-1].first, value, e);
    }

    debug(4) << "With lets: " << e << "\n";

    return e;
}

namespace {

class CSEEveryExprInStmt : public IRMutator {
public:
    using IRMutator::mutate;

    Expr mutate(Expr e) {
        return common_subexpression_elimination(e);
    }
};

} // namespace

Stmt common_subexpression_elimination(Stmt s) {
    return CSEEveryExprInStmt().mutate(s);
}


// Testing code.

namespace {

// Normalize all names in an expr so that expr compares can be done
// without worrying about mere name differences.
class NormalizeVarNames : public IRMutator {
    int counter;

    map<string, string> new_names;

    using IRMutator::visit;

    void visit(const Variable *var) {
        map<string, string>::iterator iter = new_names.find(var->name);
        if (iter == new_names.end()) {
            expr = var;
        } else {
            expr = Variable::make(var->type, iter->second);
        }
    }

    void visit(const Let *let) {
        string new_name = "t" + int_to_string(counter++);
        new_names[let->name] = new_name;
        Expr value = mutate(let->value);
        Expr body = mutate(let->body);
        expr = Let::make(new_name, value, body);
    }

public:
    NormalizeVarNames() : counter(0) {}
};

void check(Expr in, Expr correct) {
    Expr result = common_subexpression_elimination(in);
    NormalizeVarNames n;
    result = n.mutate(result);
    internal_assert(equal(result, correct))
        << "Incorrect CSE:\n" << in
        << "\nbecame:\n" << result
        << "\ninstead of:\n" << correct << "\n";
}

// Construct a nested block of lets. Variables of the form "tn" refer
// to expr n in the vector.
Expr ssa_block(vector<Expr> exprs) {
    Expr e = exprs.back();
    for (size_t i = exprs.size() - 1; i > 0; i--) {
        string name = "t" + int_to_string(i-1);
        e = Let::make(name, exprs[i-1], e);
    }
    return e;
}

} // namespace

void cse_test() {
    Expr x = Variable::make(Int(32), "x");
    Expr t[32], tf[32];
    for (int i = 0; i < 32; i++) {
        t[i] = Variable::make(Int(32), "t" + int_to_string(i));
        tf[i] = Variable::make(Float(32), "t" + int_to_string(i));
    }
    Expr e, correct;

    // This is fine as-is.
    e = ssa_block(vec(sin(x),
                      tf[0]*tf[0]));
    check(e, e);

    // Test a simple case.
    e = ((x*x + x)*(x*x + x)) + x*x;
    e += e;
    correct = ssa_block(vec(x*x,                  // x*x
                            t[0] + x,             // x*x + x
                            t[1] * t[1] + t[0],   // (x*x + x)*(x*x + x) + x*x
                            t[2] + t[2]));
    check(e, correct);

    // Check for idempotence (also checks a case with lets)
    check(correct, correct);

    // Check a case with redundant lets
    e = ssa_block(vec(x*x,
                      x*x,
                      t[0] / t[1],
                      t[1] / t[1],
                      t[2] % t[3],
                      (t[4] + x*x) + x*x));
    correct = ssa_block(vec(x*x,
                            t[0] / t[0],
                            (t[1] % t[1] + t[0]) + t[0]));
    check(e, correct);

    // Check a case with nested lets with shared subexpressions
    // between the lets, and repeated names.
    Expr e1 = ssa_block(vec(x*x,                  // a = x*x
                            t[0] + x,             // b = a + x
                            t[1] * t[1] * t[0])); // c = b * b * a
    Expr e2 = ssa_block(vec(x*x,                  // a again
                            t[0] - x,             // d = a - x
                            t[1] * t[1] * t[0])); // e = d * d * a
    e = ssa_block(vec(e1 + x*x,                   // f = c + a
                      e1 + e2,                    // g = c + e
                      t[0] + t[0] * t[1]));       // h = f + f * g

    correct = ssa_block(vec(x*x,                // t0 = a = x*x
                            t[0] + x,           // t1 = b = a + x     = t0 + x
                            t[1] * t[1] * t[0], // t2 = c = b * b * a = t1 * t1 * t0
                            t[2] + t[0],        // t3 = f = c + a     = t2 + t0
                            t[0] - x,           // t4 = d = a - x     = t0 - x
                            t[3] + t[3] * (t[2] + t[4] * t[4] * t[0]))); // h (with g substituted in)
    check(e, correct);

    // Test it scales OK.
    e = x;
    for (int i = 0; i < 100; i++) {
        e = e*e + e + i;
        e = e*e - e * i;
    }
    Expr result = common_subexpression_elimination(e);

    debug(0) << "common_subexpression_elimination test passed\n";
}

}
}
