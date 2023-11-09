#include "Substitute.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;

namespace {

class Substitute : public IRMutator {
    const map<string, Expr> &replace;
    Scope<> hidden;

    Expr find_replacement(const string &s) {
        map<string, Expr>::const_iterator iter = replace.find(s);
        if (iter != replace.end() && !hidden.contains(s)) {
            return iter->second;
        } else {
            return Expr();
        }
    }

public:
    Substitute(const map<string, Expr> &m)
        : replace(m) {
    }

    using IRMutator::visit;

    Expr visit(const Variable *v) override {
        Expr r = find_replacement(v->name);
        if (r.defined()) {
            return r;
        } else {
            return v;
        }
    }

    template<typename T>
    auto visit_let(const T *op) -> decltype(op->body) {
        decltype(op->body) orig = op;

        struct Frame {
            const T *op;
            Expr new_value;
            ScopedBinding<> bind;
        };
        std::vector<Frame> frames;
        decltype(op->body) body;
        bool values_unchanged = true;
        do {
            Expr new_value = mutate(op->value);
            values_unchanged &= new_value.same_as(op->value);
            frames.push_back(Frame{op, std::move(new_value), ScopedBinding<>(hidden, op->name)});
            body = op->body;
            op = body.template as<T>();
        } while (op);

        auto new_body = mutate(body);

        if (values_unchanged &&
            new_body.same_as(body)) {
            return orig;
        } else {
            for (auto it = frames.rbegin(); it != frames.rend(); it++) {
                new_body = T::make(it->op->name, it->new_value, new_body);
            }
            return new_body;
        }
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Stmt visit(const For *op) override {
        Expr new_min = mutate(op->min);
        Expr new_extent = mutate(op->extent);
        hidden.push(op->name);
        Stmt new_body = mutate(op->body);
        hidden.pop(op->name);

        if (new_min.same_as(op->min) &&
            new_extent.same_as(op->extent) &&
            new_body.same_as(op->body)) {
            return op;
        } else {
            return For::make(op->name, new_min, new_extent, op->for_type, op->partition_policy, op->device_api, new_body);
        }
    }
};

}  // namespace

Expr substitute(const string &name, const Expr &replacement, const Expr &expr) {
    map<string, Expr> m;
    m[name] = replacement;
    Substitute s(m);
    return s.mutate(expr);
}

Stmt substitute(const string &name, const Expr &replacement, const Stmt &stmt) {
    map<string, Expr> m;
    m[name] = replacement;
    Substitute s(m);
    return s.mutate(stmt);
}

Expr substitute(const map<string, Expr> &m, const Expr &expr) {
    Substitute s(m);
    return s.mutate(expr);
}

Stmt substitute(const map<string, Expr> &m, const Stmt &stmt) {
    Substitute s(m);
    return s.mutate(stmt);
}

namespace {

class SubstituteExpr : public IRMutator {
public:
    Expr find, replacement;

    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        if (equal(e, find)) {
            return replacement;
        } else {
            return IRMutator::mutate(e);
        }
    }
};

}  // namespace

Expr substitute(const Expr &find, const Expr &replacement, const Expr &expr) {
    SubstituteExpr s;
    s.find = find;
    s.replacement = replacement;
    return s.mutate(expr);
}

Stmt substitute(const Expr &find, const Expr &replacement, const Stmt &stmt) {
    SubstituteExpr s;
    s.find = find;
    s.replacement = replacement;
    return s.mutate(stmt);
}

namespace {

/** Substitute an expr for a var in a graph. */
class GraphSubstitute : public IRGraphMutator {
    string var;
    Expr value;

    using IRGraphMutator::visit;

    Expr visit(const Variable *op) override {
        if (op->name == var) {
            return value;
        } else {
            return op;
        }
    }

    Expr visit(const Let *op) override {
        Expr new_value = mutate(op->value);
        if (op->name == var) {
            return Let::make(op->name, new_value, op->body);
        } else {
            return Let::make(op->name, new_value, mutate(op->body));
        }
    }

public:
    GraphSubstitute(const string &var, const Expr &value)
        : var(var), value(value) {
    }
};

/** Substitute an Expr for another Expr in a graph. Unlike substitute,
 * this only checks for shallow equality. */
class GraphSubstituteExpr : public IRGraphMutator {
    Expr find, replace;

public:
    using IRGraphMutator::mutate;

    Expr mutate(const Expr &e) override {
        if (e.same_as(find)) {
            return replace;
        } else {
            return IRGraphMutator::mutate(e);
        }
    }

    GraphSubstituteExpr(const Expr &find, const Expr &replace)
        : find(find), replace(replace) {
    }
};

}  // namespace

Expr graph_substitute(const string &name, const Expr &replacement, const Expr &expr) {
    return GraphSubstitute(name, replacement).mutate(expr);
}

Stmt graph_substitute(const string &name, const Expr &replacement, const Stmt &stmt) {
    return GraphSubstitute(name, replacement).mutate(stmt);
}

Expr graph_substitute(const Expr &find, const Expr &replacement, const Expr &expr) {
    return GraphSubstituteExpr(find, replacement).mutate(expr);
}

Stmt graph_substitute(const Expr &find, const Expr &replacement, const Stmt &stmt) {
    return GraphSubstituteExpr(find, replacement).mutate(stmt);
}

namespace {

class SubstituteInAllLets : public IRGraphMutator {

    using IRGraphMutator::visit;

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        return graph_substitute(op->name, value, body);
    }
};

}  // namespace

Expr substitute_in_all_lets(const Expr &expr) {
    return SubstituteInAllLets().mutate(expr);
}

Stmt substitute_in_all_lets(const Stmt &stmt) {
    return SubstituteInAllLets().mutate(stmt);
}

}  // namespace Internal
}  // namespace Halide
