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

public:
    ComputeUseCounts(GVN &g, bool l)
        : gvn(g), lift_all(l) {
    }

    using IRGraphVisitor::include;
    using IRGraphVisitor::visit;

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
            gvn.entries[iter->second]->use_count++;
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
protected:
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
protected:
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
        dummy = peel_lets(dummy, &lets);
        const Call *bundle = Call::as_intrinsic(dummy, {Call::bundle});
        internal_assert(bundle && bundle->args.size() == 2);

        Expr value = bundle->args[0], index = bundle->args[1];

        // Figure out which ones are actually needed by the index

        auto add_all_vars_to_set = [&](const Expr &e, std::set<std::string> &s) {
            visit_with(e, [&](auto *, const Variable *var) {
                s.insert(var->name);
            });
        };

        std::set<string> index_lets;
        add_all_vars_to_set(index, index_lets);
        for (const auto &[var, val] : reverse_view(lets)) {
            if (index_lets.count(var)) {
                add_all_vars_to_set(val, index_lets);
            }
        }

        vector<pair<string, Expr>> deferred;
        for (const auto &[var, val] : reverse_view(lets)) {
            if (index_lets.count(var)) {
                deferred.emplace_back(var, val);
            } else {
                value = Let::make(var, val, value);
            }
        }

        Stmt s = op->with(value, index, mutate(op->predicate), op->alignment);

        for (const auto &[var, val] : deferred) {
            s = LetStmt::make(var, val, s);
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

    e = RemoveLets()(e);

    debug(4) << "After removing lets: " << e << "\n";

    // CSE is run on unsanitized Exprs from the user, and may contain Vars with
    // the same name as the temporaries we intend to introduce. Find any such
    // Vars so that we know not to use those names.
    class UniqueNameProvider : public IRGraphVisitor {
    protected:
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
    };
    UniqueNameProvider namer;
    {
        e.accept(&namer);
    }

    GVN gvn;
    e = gvn(e);

    ComputeUseCounts count_uses(gvn, lift_all);
    count_uses(e);

    debug(4) << "Canonical form without lets " << e << "\n";

    // Figure out which ones we'll pull out as lets and variables.
    vector<pair<string, Expr>> lets;
    vector<Expr> new_version(gvn.entries.size());
    map<Expr, Expr, ExprCompare> replacements;
    for (size_t i = 0; i < gvn.entries.size(); i++) {
        const auto &e = gvn.entries[i];
        if (e->use_count > 1) {
            string name = namer.make_unique_name();
            lets.emplace_back(name, e->expr);
            // Point references to this expr to the variable instead.
            replacements[e->expr] = Variable::make(e->expr.type(), name);
        }
        debug(4) << i << ": " << e->expr << ", " << e->use_count << "\n";
    }

    // Rebuild the expr to include references to the variables:
    Replacer replacer(replacements);
    e = replacer(e);

    debug(4) << "With variables " << e << "\n";

    // Wrap the final expr in the lets.
    for (const auto &[var, value] : reverse_view(lets)) {
        // Drop this variable as an acceptable replacement for this expr.
        replacer.erase(value);
        // Use containing lets in the value.
        e = Let::make(var, replacer(value), e);
    }

    debug(4) << "With lets: " << e << "\n";

    return e;
}

Stmt common_subexpression_elimination(const Stmt &s, bool lift_all) {
    return CSEEveryExprInStmt(lift_all)(s);
}

}  // namespace Internal
}  // namespace Halide
