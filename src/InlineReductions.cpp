#include "InlineReductions.h"

#include "CSE.h"
#include "Debug.h"
#include "Func.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include <utility>

namespace Halide {

using std::ostringstream;
using std::string;
using std::vector;

namespace Internal {
namespace {

class FindFreeVars : public IRMutator {
public:
    vector<Var> free_vars;
    vector<Expr> call_args;
    RDom rdom;

    FindFreeVars(const RDom &r, const string &n)
        : rdom(r), explicit_rdom(r.defined()), name(n) {
    }

private:
    bool explicit_rdom;
    const string &name;

    Scope<> internal;

    using IRMutator::visit;

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        internal.push(op->name);
        Expr body = mutate(op->body);
        internal.pop(op->name);
        if (value.same_as(op->value) &&
            body.same_as(op->body)) {
            return op;
        } else {
            return Let::make(op->name, value, body);
        }
    }

    Expr visit(const Variable *v) override {
        string var_name = v->name;
        Expr expr = v;

        if (internal.contains(var_name)) {
            // Don't capture internally defined vars
            return expr;
        }

        if (v->reduction_domain.defined()) {
            if (explicit_rdom) {
                if (v->reduction_domain.same_as(rdom.domain())) {
                    // This variable belongs to the explicit reduction domain, so
                    // skip it.
                    return expr;
                } else {
                    // This should be converted to a pure variable and
                    // added to the free vars list.
                    var_name = unique_name('v');
                    expr = Variable::make(v->type, var_name);
                }
            } else {
                if (!rdom.defined()) {
                    // We're looking for a reduction domain, and this variable
                    // has one. Capture it.
                    rdom = RDom(v->reduction_domain);
                    return expr;
                } else if (!rdom.domain().same_as(v->reduction_domain)) {
                    // We were looking for a reduction domain, and already
                    // found one. This one is different!
                    user_error << "Inline reduction \"" << name
                               << "\" refers to reduction variables from multiple reduction domains: "
                               << v->name << ", " << rdom.x.name() << "\n";
                } else {
                    // Recapturing an already-known reduction domain
                    return expr;
                }
            }
        }

        if (v->param.defined()) {
            // Skip parameters
            return expr;
        }

        for (auto &free_var : free_vars) {
            if (var_name == free_var.name()) {
                return expr;
            }
        }

        free_vars.emplace_back(var_name);
        call_args.emplace_back(v);
        return expr;
    }
};

}  // namespace
}  // namespace Internal

Expr sum(Expr e, const std::string &name) {
    return sum(RDom(), std::move(e), Func(name));
}

Expr sum(Expr e, const Func &f) {
    return sum(RDom(), std::move(e), f);
}

Expr sum(const RDom &r, Expr e, const std::string &name) {
    return sum(r, std::move(e), Func(name));
}

Expr sum(const RDom &r, Expr e, const Func &f) {
    user_assert(!f.defined())
        << "Func " << f.name()
        << " passed to sum already has a definition";

    Internal::FindFreeVars v(r, f.name());
    e = v.mutate(common_subexpression_elimination(e));

    user_assert(v.rdom.defined()) << "Expression passed to sum must reference a reduction domain";

    f(v.free_vars) += e;
    return f(v.call_args);
}

Expr saturating_sum(Expr e, const std::string &name) {
    return saturating_sum(RDom(), std::move(e), Func(name));
}

Expr saturating_sum(Expr e, const Func &f) {
    return saturating_sum(RDom(), std::move(e), f);
}

Expr saturating_sum(const RDom &r, Expr e, const std::string &name) {
    return saturating_sum(r, std::move(e), Func(name));
}

Expr saturating_sum(const RDom &r, Expr e, const Func &f) {
    user_assert(!f.defined())
        << "Func " << f.name()
        << " passed to saturating_sum already has a definition";

    Internal::FindFreeVars v(r, f.name());
    e = v.mutate(common_subexpression_elimination(e));

    user_assert(v.rdom.defined()) << "Expression passed to saturating_sum must reference a reduction domain";

    f(v.free_vars) = cast(e.type(), 0);
    f(v.free_vars) = saturating_add(f(v.free_vars), e);
    return f(v.call_args);
}

Expr product(Expr e, const std::string &name) {
    return product(RDom(), std::move(e), Func(name));
}

Expr product(Expr e, const Func &f) {
    return product(RDom(), std::move(e), f);
}

Expr product(const RDom &r, Expr e, const std::string &name) {
    return product(r, std::move(e), Func(name));
}

Expr product(const RDom &r, Expr e, const Func &f) {
    user_assert(!f.defined())
        << "Func " << f.name()
        << " passed to product already has a definition";

    Internal::FindFreeVars v(r, f.name());
    e = v.mutate(common_subexpression_elimination(e));

    user_assert(v.rdom.defined()) << "Expression passed to product must reference a reduction domain";

    f(v.free_vars) *= e;
    return f(v.call_args);
}

Expr maximum(Expr e, const std::string &name) {
    return maximum(RDom(), std::move(e), Func(name));
}

Expr maximum(Expr e, const Func &f) {
    return maximum(RDom(), std::move(e), f);
}

Expr maximum(const RDom &r, Expr e, const std::string &name) {
    return maximum(r, std::move(e), Func(name));
}

Expr maximum(const RDom &r, Expr e, const Func &f) {
    user_assert(!f.defined())
        << "Func " << f.name()
        << " passed to maximum already has a definition";

    Internal::FindFreeVars v(r, f.name());
    e = v.mutate(common_subexpression_elimination(e));

    user_assert(v.rdom.defined()) << "Expression passed to maximum must reference a reduction domain";

    f(v.free_vars) = e.type().min();
    f(v.free_vars) = max(f(v.free_vars), e);
    return f(v.call_args);
}

Expr minimum(Expr e, const std::string &name) {
    return minimum(RDom(), std::move(e), Func(name));
}

Expr minimum(Expr e, const Func &f) {
    return minimum(RDom(), std::move(e), f);
}

Expr minimum(const RDom &r, Expr e, const std::string &name) {
    return minimum(r, std::move(e), Func(name));
}

Expr minimum(const RDom &r, Expr e, const Func &f) {
    user_assert(!f.defined())
        << "Func " << f.name()
        << " passed to minimum already has a definition";

    Internal::FindFreeVars v(r, f.name());
    e = v.mutate(common_subexpression_elimination(e));

    user_assert(v.rdom.defined()) << "Expression passed to minimum must reference a reduction domain";

    f(v.free_vars) = e.type().max();
    f(v.free_vars) = min(f(v.free_vars), e);
    return f(v.call_args);
}

Tuple argmax(Expr e, const std::string &name) {
    return argmax(RDom(), std::move(e), Func(name));
}

Tuple argmax(Expr e, const Func &f) {
    return argmax(RDom(), std::move(e), f);
}

Tuple argmax(const RDom &r, Expr e, const std::string &name) {
    return argmax(r, std::move(e), Func(name));
}

Tuple argmax(const RDom &r, Expr e, const Func &f) {
    user_assert(!f.defined())
        << "Func " << f.name()
        << " passed to argmax already has a definition";

    Internal::FindFreeVars v(r, f.name());
    e = v.mutate(common_subexpression_elimination(e));

    user_assert(v.rdom.defined()) << "Expression passed to argmax must reference a reduction domain";

    Tuple initial_tup(vector<Expr>(v.rdom.dimensions() + 1));
    Tuple update_tup(vector<Expr>(v.rdom.dimensions() + 1));
    for (int i = 0; i < v.rdom.dimensions(); i++) {
        initial_tup[i] = 0;
        update_tup[i] = v.rdom[i];
    }
    int value_index = (int)initial_tup.size() - 1;
    initial_tup[value_index] = e.type().min();
    update_tup[value_index] = e;

    f(v.free_vars) = initial_tup;
    Expr better = e > f(v.free_vars)[value_index];
    Tuple update = tuple_select(better, update_tup, f(v.free_vars));
    f(v.free_vars) = update;
    return f(v.call_args);
}

Tuple argmin(Expr e, const std::string &name) {
    return argmin(RDom(), std::move(e), Func(name));
}

Tuple argmin(Expr e, const Func &f) {
    return argmin(RDom(), std::move(e), f);
}

Tuple argmin(const RDom &r, Expr e, const std::string &name) {
    return argmin(r, std::move(e), Func(name));
}

Tuple argmin(const RDom &r, Expr e, const Func &f) {
    user_assert(!f.defined())
        << "Func " << f.name()
        << " passed to argmin already has a definition";

    Internal::FindFreeVars v(r, f.name());
    e = v.mutate(common_subexpression_elimination(e));

    user_assert(v.rdom.defined()) << "Expression passed to argmin must reference a reduction domain";

    Tuple initial_tup(vector<Expr>(v.rdom.dimensions() + 1));
    Tuple update_tup(vector<Expr>(v.rdom.dimensions() + 1));
    for (int i = 0; i < v.rdom.dimensions(); i++) {
        initial_tup[i] = 0;
        update_tup[i] = v.rdom[i];
    }
    int value_index = (int)initial_tup.size() - 1;
    initial_tup[value_index] = e.type().max();
    update_tup[value_index] = e;

    f(v.free_vars) = initial_tup;
    Expr better = e < f(v.free_vars)[value_index];
    f(v.free_vars) = tuple_select(better, update_tup, f(v.free_vars));
    return f(v.call_args);
}

}  // namespace Halide
