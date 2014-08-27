#include <map>

#include "CSE.h"
#include "IRMutator.h"
#include "IREquality.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::map;
using std::set;
using std::pair;
using std::make_pair;

struct RemoveLets : public IRMutator {
    set<Expr, ExprDeepCompare> canonical;
    vector<map<Expr, Expr, ExprCompare> > replacement;

    RemoveLets() {
        enter_scope();
    }

    Expr canonicalize(Expr e) {
        set<Expr, ExprDeepCompare>::iterator i = canonical.find(e);
        if (i != canonical.end()) {
            return *i;
        } else {
            canonical.insert(e);
            return e;
        }
    }

    using IRMutator::mutate;

    Expr mutate(Expr e) {
        e = canonicalize(e);

        Expr r = find_replacement(e);
        if (r.defined()) {
            return r;
        } else {
            Expr new_expr = canonicalize(IRMutator::mutate(e));
            add_replacement(e, new_expr);
            return new_expr;
        }


    }

    Expr find_replacement(Expr e) {
        for (size_t i = replacement.size(); i > 0; i--) {
            map<Expr, Expr, ExprCompare>::iterator iter = replacement[i-1].find(e);
            if (iter != replacement[i-1].end()) return iter->second;
        }
        return Expr();
    }

    void add_replacement(Expr key, Expr value) {
        replacement[replacement.size()-1][key] = value;
    }

    void enter_scope() {
        replacement.resize(replacement.size()+1);
    }

    void leave_scope() {
        replacement.pop_back();
    }

    using IRMutator::visit;

    void visit(const Let *let) {
        Expr var = canonicalize(Variable::make(let->value.type(), let->name));

        Expr new_value = mutate(let->value);
        enter_scope();
        add_replacement(var, new_value);
        expr = mutate(let->body);
        leave_scope();
    }

    void visit(const LetStmt *let) {
        Expr var = canonicalize(Variable::make(let->value.type(), let->name));
        Expr new_value = mutate(let->value);
        enter_scope();
        add_replacement(var, new_value);
        stmt = mutate(let->body);
        leave_scope();
    }
};

Expr remove_lets(Expr e) {
    return RemoveLets().mutate(e);
}

Stmt remove_lets(Stmt s) {
    return RemoveLets().mutate(s);
}

struct ReplaceExpr : public IRMutator {
    using IRMutator::mutate;

    map<Expr, Expr, ExprCompare> mutated;

    Expr mutate(Expr e) {
        map<Expr, Expr, ExprCompare>::iterator iter = mutated.find(e);
        if (iter != mutated.end()) {
            return iter->second;
        } else {
            Expr new_expr = IRMutator::mutate(e);
            mutated[e] = new_expr;
            return new_expr;
        }
    }
};

Expr replace_expr(Expr e, Expr old_expr, Expr replacement) {
    ReplaceExpr r;
    r.mutated[old_expr] = replacement;
    return r.mutate(e);
}

struct FindOneCommonSubexpression : public IRGraphVisitor {
    Expr result;

    using IRGraphVisitor::include;

    void include(const Expr &e) {
        if (result.defined()) return;

        set<const IRNode *>::iterator iter = visited.find(e.ptr);

        if (iter != visited.end()) {
            if (e.as<Variable>() ||
                is_const(e) ||
                e.as<StringImm>() ||
                (e.as<Broadcast>() && e.as<Broadcast>()->value.as<StringImm>())) {
                return;
            }

            result = e;
        } else {
            e.accept(this);
            visited.insert(e.ptr);
        }
    }
};

Expr common_subexpression_elimination(Expr e) {

    // debug(0) << "Input to letify " << e << "\n";

    e = remove_lets(e);

    // debug(0) << "Deletified letify " << e << "\n";

    vector<pair<string, Expr> > lets;

    while (1) {
        FindOneCommonSubexpression finder;
        finder.include(e);
        if (!finder.result.defined()) break;

        // debug(0) << "\nHoisting out " << finder.result << " from " << e << "\n";

        string name = unique_name('t');

        lets.push_back(make_pair(name, finder.result));

        e = replace_expr(e, finder.result, Variable::make(finder.result.type(), name));
        // debug(0) << "Result: " << e << "\n";
    }

    for (size_t i = lets.size(); i > 0; i--) {
        e = Let::make(lets[i-1].first, lets[i-1].second, e);
    }

    // debug(0) << "Output of letify " << e << "\n";

    return e;
}

class LetifyStmt : public IRMutator {
public:
    using IRMutator::mutate;

    Expr mutate(Expr e) {
        return common_subexpression_elimination(e);
    }
};

Stmt common_subexpression_elimination(Stmt s) {
    return LetifyStmt().mutate(s);
}

}
}
