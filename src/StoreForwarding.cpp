#include "StoreForwarding.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Simplify.h"
#include "IREquality.h"
#include "ExprUsesVar.h"
#include "CSE.h"

#include <algorithm>

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::pair;
using std::set;
using std::map;

namespace {

// Group a collection of Exprs into a single Expr. Useful for running
// CSE jointly across a set of Exprs.
Expr pack_bundle(vector<Expr> e) {
    return Call::make(Int(32), unique_name('b'), e, Call::Intrinsic);
}

// Unpack a bundle made the by the function above.
vector<Expr> unpack_bundle(Expr e) {
    const Call *c = e.as<Call>();
    internal_assert(c) << "Not a bundle: " << e << "\n";
    return c->args;
}

// A template metafunction convert Stmt/Expr to LetStmt/Let respectively
template<typename T> struct LetForType;
template<> struct LetForType<Stmt> {typedef LetStmt type;};
template<> struct LetForType<Expr> {typedef Let type;};

// Peel containing lets off a Stmt or Expr into a vector.
template<typename StmtOrExpr, typename LetStmtOrLet = typename LetForType<StmtOrExpr>::type>
StmtOrExpr unwrap_lets(StmtOrExpr e, vector<pair<string, Expr>> &l) {
    l.clear();
    while (const LetStmtOrLet *let = e.template as<LetStmtOrLet>()) {
        l.push_back(make_pair(let->name, let->value));
        e = let->body;
    }
    return e;
}

// Do the opposite of the above - rewrap a Stmt or Expr with some lets.
template<typename StmtOrExpr, typename LetStmtOrLet = typename LetForType<StmtOrExpr>::type>
StmtOrExpr wrap_lets(StmtOrExpr e, const vector<pair<string, Expr>> &l,
                     bool only_if_used = false,
                     bool make_substitutions = false) {
    for (size_t i = l.size(); i > 0; i--) {
        pair<string, Expr> let = l[i-1];
        if (make_substitutions && (let.second.as<Variable>() || is_const(let.second))) {
            e = substitute(let.first, let.second, e);
        } else if (!only_if_used || stmt_or_expr_uses_var(e, let.first)) {
            e = LetStmtOrLet::make(let.first, let.second, e);
        }
    }
    return e;
}

// Wrappers of the above with more friendly names.
template<typename StmtOrExpr>
StmtOrExpr wrap_used_lets(StmtOrExpr e, const vector<pair<string, Expr>> &l) {
    return wrap_lets(e, l, true, false);
}

template<typename StmtOrExpr>
StmtOrExpr wrap_or_substitute_used_lets(StmtOrExpr e, const vector<pair<string, Expr>> &l) {
    return wrap_lets(e, l, true, true);
}

// Given a vector of peeled lets, return Variable nodes corresponding
// to each let.
vector<Expr> let_vars(const vector<pair<string, Expr>> &l) {
    vector<Expr> result;
    for (pair<string, Expr> p : l) {
        result.push_back(Variable::make(p.second.type(), p.first));
    }
    return result;
}

// Attempt to find the minimum or maximum lane in a vector. Useful in
// proving that two vectors don't overlap.
Expr extreme_lane(Expr e, bool max) {
    if (e.type().is_scalar()) {
        return e;
    }
    if (const Broadcast *b = e.as<Broadcast>()) {
        return b->value;
    }
    if (const Ramp *r = e.as<Ramp>()) {
        Expr last_lane = r->base + r->stride * (r->lanes - 1);
        Expr first_lane = r->base;
        if (is_positive_const(r->stride)) {
            if (max) {
                return last_lane;
            } else {
                return first_lane;
            }
        } else if (is_negative_const(r->stride) ^ max) {
            if (max) {
                return first_lane;
            } else {
                return last_lane;
            }
        }
    }
    internal_error << "max_lane/min_lane should only be called on constants\n";
    return Expr();
}

Expr max_lane(Expr e) {
    return extreme_lane(e, true);
}

Expr min_lane(Expr e) {
    return extreme_lane(e, false);
}

// Is it possible that one of the vector lanes of a equals one of the
// vector lanes of b.
bool might_overlap(Expr a, Expr b) {
    if (a.type().is_scalar() && b.type().is_scalar()) {
        return !is_one(simplify(a != b));
    } else if (is_const(a) && is_const(b)) {
        Expr no_overlap = (max_lane(a) < min_lane(b) ||
                           min_lane(a) > max_lane(b));
        return !is_one(simplify(no_overlap));
    } else {
        return true;
    }
}

// Make the index vector used when loading or storing a single vector
// to a single-vector allocation.
Expr scratch_index(Type t) {
    if (t.lanes() == 1) {
        return 0;
    } else {
        return Ramp::make(0, 1, t.lanes());
    }
}

}

// Check if some IR uses any of the allocations in the provided scope.
class IRUsesAlloc : public IRVisitor {
    const Scope<int> &allocs;

    using IRVisitor::visit;

    void visit(const Load *op) {
        if (allocs.contains(op->name)) {
            result = true;
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        if (allocs.contains(op->name)) {
            result = true;
        }
    }

public:
    IRUsesAlloc(const Scope<int> &a) : allocs(a) {}
    bool result = false;
};

template<typename StmtOrExpr>
bool ir_uses_alloc(StmtOrExpr e, const Scope<int> &a) {
    IRUsesAlloc uses(a);
    e.accept(&uses);
    return uses.result;
}

// Find all loads and stores in some IR to external allocations. Only
// finds loads and stores that definitely occur.
class FindLoadsAndStores : public IRVisitor {
    using IRVisitor::visit;

    Scope<int> inner_allocs;
    Scope<int> inner_vars;

    void visit(const Load *op) {
        if (!ir_uses_alloc(Expr(op), inner_allocs) &&
            !expr_uses_vars(Expr(op), inner_vars)) {
            loads.push_back(op);
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        if (!ir_uses_alloc(Stmt(op), inner_allocs) &&
            !stmt_uses_vars(Stmt(op), inner_vars)) {
            stores.push_back(op);
        }
        IRVisitor::visit(op);
    }

    void visit(const IfThenElse *op) {
        // Conservatively don't enter a conditional
        op->condition.accept(this);
    }

    void visit(const For *op) {
        // We don't want to lift loads and stores that depend on inner
        // loop variables.
        inner_vars.push(op->name, 0);
        IRVisitor::visit(op);
        inner_vars.pop(op->name);
    }

    void visit(const Allocate *op) {
        inner_allocs.push(op->name, 0);
        IRVisitor::visit(op);
        inner_allocs.pop(op->name);
    }

    template<typename LetStmtOrLet, typename StmtOrExpr>
    void visit_let(const LetStmtOrLet *op) {
        op->value.accept(this);
        inner_vars.push(op->name, 0);
        op->body.accept(this);
        inner_vars.pop(op->name);
    }

    void visit(const LetStmt *op) {
        visit_let<LetStmt, Stmt>(op);
    }

    void visit(const Let *op) {
        visit_let<Let, Expr>(op);
    }


public:
    vector<const Load *> loads;
    vector<const Store *> stores;
};

// Check if a load or store node might alias with another distinct
// store node.
class MightAliasWithAStore : public IRVisitor {
    const Store *store = nullptr;
    const Load *load = nullptr;

    void visit(const Store *op) {
        if (store && op->name == store->name && op != store) {
            if (might_overlap(op->index, store->index)) {
                result = true;
            }
        } else if (load && op->name == load->name) {
            if (might_overlap(op->index, load->index)) {
                result = true;
            }
        }

        // No need to visit the index or value, because an Expr can't
        // contain a store node.
    }

    using IRVisitor::visit;
public:
    MightAliasWithAStore(const Store *s) : store(s) {}
    MightAliasWithAStore(const Load *l) : load(l) {}
    bool result = false;
};

// Promote values that are stored-then-loaded into let statements instead. For example:

// foo[bar] = expr
// ...
// ... foo[bar] ...

// becomes:

// let t0 = expr
// foo[bar] = t0
// ...
// ... t0 ...

// Requires that no potentially-aliasing stores to foo have taken
// place meanwhile.

class ForwardSingleStore : public IRMutator {

    using IRMutator::visit;

    // The store we're forwarding.
    const Store *store;

    // The traversal happens in the same order as execution. This flag
    // is set to true if we encounter another store that might alias
    // the one we're forwarding. If that happens we can make no
    // further changes.
    bool found_aliasing_store = false;

    void visit(const Load *op) {
        Expr index = mutate(op->index);
        if (op->name == store->name &&
            equal(index, store->index)) {
            // Forward the store! Instead of loading, reuse the value that was stored.
            expr = store->value;
        } else if (index.same_as(op->index)) {
            expr = op;
        } else {
            expr = Load::make(op->type, op->name, op->index, Buffer(), Parameter());
        }
    }

    void visit(const Store *op) {
        if (op->name == store->name && might_overlap(op->index, store->index)) {
            found_aliasing_store = true;
        }
        IRMutator::visit(op);
    }

    void visit(const For *op) {
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        Stmt body = mutate(op->body);
        if (found_aliasing_store) {
            // Back out any changes to the body that might have
            // occured before we hit the aliasing part.
            body = op->body;
        }
        if (min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, min, extent, op->for_type, op->device_api, body);
        }
    }

    void visit(const Call *op) {
        if (op->name == Call::address_of &&
            op->call_type == Call::Intrinsic) {
            // address_of expects a load. Don't replace it with a value.
            expr = op;
        } else {
            IRMutator::visit(op);
        }
    }

public:
    Expr mutate(Expr e) {
        if (found_aliasing_store) {
            return e;
        }
        return IRMutator::mutate(e);
    }

    Stmt mutate(Stmt s) {
        if (found_aliasing_store) {
            return s;
        }
        return IRMutator::mutate(s);
    }

    ForwardSingleStore(const Store *s) : store(s) {
    }
};

class StoreForwarding : public IRMutator {
    using IRMutator::visit;

    void visit(const Block *op) {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);

        // Pull lets outwards
        if (const LetStmt *let = first.as<LetStmt>()) {
            string new_name = unique_name('t');
            Stmt body = substitute(let->name, Variable::make(let->value.type(), new_name), let->body);
            stmt = LetStmt::make(new_name, let->value, mutate(Block::make(body, rest)));
            return;
        }

        if (const Store *store = first.as<Store>()) {
            if (store->value.as<Variable>()) {
                // If it's a var, we can just move it forwards to the
                // loads. It'll still be in scope in 'rest'.
                ForwardSingleStore forwarder(store);
                rest = forwarder.mutate(rest);
                stmt = Block::make(first, rest);
            } else {
                // Otherwise, make it a var. We wouldn't want it to
                // contain a load, or other piece of IR whose value
                // can change if we move it.
                string var_name = unique_name('t');
                Expr var = Variable::make(store->value.type(), var_name);
                Stmt new_store = Store::make(store->name, var, store->index);
                ForwardSingleStore forwarder(new_store.as<Store>());
                Stmt new_rest = forwarder.mutate(rest);
                if (new_rest.same_as(rest)) {
                    stmt = Block::make(first, rest);
                } else {
                    stmt = Block::make(new_store, new_rest);
                    stmt = LetStmt::make(var_name, store->value, stmt);
                }
            }
        } else {
            stmt = Block::make(first, rest);
        }
    }
};

// Check if some IR contains a load that might alias with a Store done
// in some other piece of IR.
class ContainsAliasedLoad : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Load *op) {
        if (body.defined()) {
            debug(3) << "Checking if " << Expr(op) << " aliases with a store in " << body << "\n";
            MightAliasWithAStore alias_tester(op);
            body.accept(&alias_tester);
            if (alias_tester.result) {
                result = true;
                debug(3) << " yes\n";
            } else {
                debug(3) << " no\n";
            }
        } else {
            result = true;
        }
        IRVisitor::visit(op);
    }

    Stmt body;
public:
    bool result = false;
    ContainsAliasedLoad(Stmt b) : body(b) {}
    ContainsAliasedLoad() {}
};

bool contains_aliased_load(Expr e, Stmt body) {
    debug(3) << "contains_aliased_load(" << e << ")\n";
    ContainsAliasedLoad c(body);
    e.accept(&c);
    return c.result;
}

// Check if an Expr contains a load of any kind
bool contains_load(Expr e) {
    ContainsAliasedLoad c;
    e.accept(&c);
    return c.result;
}

// Check if an Expr contains a call with a side-effect.
class ContainsSideEffectingCall : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Call *op) {
        IRVisitor::visit(op);
        for (Expr e : op->args) {
            if (e.type().is_handle()) {
                result = true;
            }
        }
        if (op->call_type == Call::Intrinsic) {
            if (op->name == Call::rewrite_buffer ||
                op->name == Call::set_host_dirty ||
                op->name == Call::set_dev_dirty ||
                op->name == Call::trace ||
                op->name == Call::trace_expr ||
                op->name == Call::glsl_texture_store ||
                op->name == Call::image_store ||
                op->name == Call::copy_memory ||
                op->name == Call::register_destructor) {
                result = true;
            }
        }
    }
public:
    bool result = false;
};

bool contains_side_effecting_call(Expr e) {
    ContainsSideEffectingCall c;
    e.accept(&c);
    return c.result;
}

class LiftFixedExpressionsSingleLoop : public IRMutator {
    Scope<int> inner_vars, inner_allocs;

    map<Expr, string, IRDeepCompare> lifted_expr_names;

    using IRMutator::visit;

    Stmt loop_body;

    // Are we currently scanning code that might never run.
    bool in_conditional = false;

    void visit(const For *op) {
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        inner_vars.push(op->name, 0);
        bool old_in_conditional = in_conditional;
        in_conditional = true;
        Stmt body = mutate(op->body);
        in_conditional = old_in_conditional;
        inner_vars.pop(op->name);
        stmt = For::make(op->name, min, extent, op->for_type, op->device_api, body);
    }

    template<typename LetStmtOrLet, typename StmtOrExpr>
    StmtOrExpr visit_let(const LetStmtOrLet *op) {
        Expr value;
        if (expr_can_be_lifted(op->value) &&
            op->value.template as<Variable>() == nullptr) {
            value = lift(op->value);
        } else {
            value = mutate(op->value);
        }
        debug(3) << "Visitinge let: " << op->name << ", " << op->value << ", " << value << "\n";
        const Variable *v = value.as<Variable>();
        if (v) {
            // The value was successfully lifted into a var
            return mutate(substitute(op->name, value, op->body));
        } else {
            // It was not lifted. We can't lift expressions outside
            // this var, so poison it.
            debug(3) << "Poisoning " << op->name << "\n";
            inner_vars.push(op->name, 0);
            StmtOrExpr body = mutate(op->body);
            inner_vars.pop(op->name);
            return LetStmtOrLet::make(op->name, value, body);
        }
    }

    void visit(const LetStmt *op) {
        stmt = visit_let<LetStmt, Stmt>(op);
    }

    void visit(const Let *op) {
        expr = visit_let<Let, Expr>(op);
    }

    void visit(const IfThenElse *op) {
        bool old_in_conditional = in_conditional;
        in_conditional = true;
        IRMutator::visit(op);
        in_conditional = old_in_conditional;
    }

    void visit(const Allocate *op) {
        inner_allocs.push(op->name, 0);
        IRMutator::visit(op);
        inner_allocs.pop(op->name);
    }

    bool expr_can_be_lifted(Expr e) {
        return !(
            (in_conditional && contains_load(e)) ||
            contains_aliased_load(e, loop_body) ||
            contains_side_effecting_call(e) ||
            expr_uses_vars(e, inner_vars) ||
            ir_uses_alloc(e, inner_allocs)
            );
    }

    bool expr_should_be_lifted(Expr e) {
        const Add *add = e.as<Add>();
        const Ramp *ramp = e.as<Ramp>();
        const Broadcast *broadcast = e.as<Broadcast>();
        const Call *call = e.as<Call>();
        return
            !is_const(e) &&
            e.as<Variable>() == nullptr &&
            !e.type().is_handle() &&
            !e.type().is_bool() &&
            !ramp &&
            !broadcast &&
            !(add && is_const(add->b)) &&
            !(call && call->name == Call::interleave_vectors) &&
            expr_can_be_lifted(e);
    }

    Expr lift(Expr e) {
        auto it = lifted_expr_names.find(e);
        if (it != lifted_expr_names.end()) {
            // We already lifted this expression and gave it a name
            return Variable::make(e.type(), it->second);
        } else {
            string name = unique_name('t');
            exprs.push_back(make_pair(name, e));
            lifted_expr_names[e] = name;
            return Variable::make(e.type(), name);
        }
    }

public:
    using IRMutator::mutate;

    Expr mutate(const Expr e) {
        if (!expr_should_be_lifted(e)) {
            return IRMutator::mutate(e);
        } else {
            return lift(e);
        }
    }

    vector<pair<string, Expr>> exprs;

    LiftFixedExpressionsSingleLoop(Stmt b) : loop_body(b) {}
};

namespace {
Stmt cse_initial_lets(Stmt s) {
    vector<pair<string, Expr>> lets;
    s = unwrap_lets(s, lets);
    // Make a bundle of the values and run CSE
    vector<Expr> vars = let_vars(lets);
    Expr bundle = pack_bundle(vars);
    bundle = wrap_lets(bundle, lets);
    bundle = common_subexpression_elimination(bundle, false);

    vector<pair<string, Expr>> new_lets;
    bundle = unwrap_lets(bundle, new_lets);

    set<string> let_names;
    for (pair<string, Expr> l : new_lets) {
        let_names.insert(l.first);
    }

    // Make sure the new lets define all the same variables
    vector<Expr> values = unpack_bundle(bundle);
    internal_assert(values.size() == lets.size());
    for (size_t i = 0; i < values.size(); i++) {
        pair<string, Expr> l = lets[i];
        new_lets.push_back(make_pair(l.first, values[i]));
    }

    return wrap_or_substitute_used_lets(s, new_lets);
}
}

// Pull expressions that don't change over the course of a single iteration to the top of the loop body.
class LiftFixedExpressions : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {
        // Expressions that might change in meaning are those which:
        // 1) Load from buffers with possibly-aliased stores inside the loop
        // 2) Load from buffers allocated inside this loop
        // 3) Depend on loop variables from inner loops
        // 4) Can't lift loads out of if statements or for loops -
        // they might not have run, and be in danger of segfaulting.

        Stmt body = op->body;
        LiftFixedExpressionsSingleLoop lifter(body);
        vector<pair<string, Expr>> lets;
        body = unwrap_lets(body, lets);
        body = lifter.mutate(body);
        body = mutate(body);
        body = wrap_lets(body, lifter.exprs);
        body = wrap_lets(body, lets);

        // The resulting lets probably have a bunch of common subexpressions.
        body = cse_initial_lets(body);

        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
    }
};

class RenameVars : public IRMutator {
    using IRMutator::visit;
    void visit(const Let *op) {
        string name = unique_name('t');
        Expr var = Variable::make(op->value.type(), name);
        Expr body = substitute(op->name, var, op->body);
        expr = Let::make(name, mutate(op->value), body);
    }

    void visit(const LetStmt *op) {
        string name = unique_name('t');
        Expr var = Variable::make(op->value.type(), name);
        Stmt body = substitute(op->name, var, op->body);
        stmt = LetStmt::make(name, mutate(op->value), body);
    }
};

class VarDependsOnLoad : public IRVisitor {
    using IRVisitor::visit;
    std::string var_name;

    Scope<int> scope;

    void visit(const Let *op) {
        bool pushed = false;
        op->value.accept(this);
        if (contains_load(op->value) ||
            expr_uses_vars(op->value, scope)) {
            scope.push(op->name, 0);
            pushed = true;
            if (op->name == var_name) {
                result = true;
            }
        }
        op->body.accept(this);
        if (pushed) {
            scope.pop(op->name);
        }
    }

public:
    bool result = false;

    VarDependsOnLoad(const std::string &s) : var_name(s) {}
};

bool var_depends_on_load(Expr e, const std::string &v) {
    VarDependsOnLoad tester(v);
    e.accept(&tester);
    return tester.result;
}

vector<Stmt> find_non_aliasing_stores(Stmt stmt) {
    FindLoadsAndStores finder;
    stmt.accept(&finder);

    vector<Stmt> result;

    for (Stmt st : finder.stores) {
        const Store *s = st.as<Store>();
        MightAliasWithAStore alias_tester(s);
        stmt.accept(&alias_tester);
        if (!alias_tester.result) {
            result.push_back(s);
        }
    }

    return result;
}

class LoopCarry : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {
        Stmt body = mutate(op->body);

        debug(3) << "\n** Considering loop over " << op->name << "\n";

        if (op->for_type != ForType::Serial) {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
            return;
        }
        Expr prev_var = Variable::make(Int(32), op->name) - 1;
        Expr next_var = Variable::make(Int(32), op->name) + 1;

        // Thanks to the previous passes, at the top of every for loop
        // there's a block of let statements. We want to mine it for
        // expressions in common with the previous/next loop
        // iteration.

        vector<pair<string, Expr>> lets, orig_lets;
        body = unwrap_lets(body, lets);
        orig_lets = lets;

        // Also make loads equivalent to the stores this loop body
        // will do. It will follow along with our rewriting of lets.
        vector<Stmt> store_stmts = find_non_aliasing_stores(body);
        vector<Expr> values_stored, equivalent_loads;
        for (Stmt store_stmt : store_stmts) {
            const Store *s = store_stmt.as<Store>();
            Expr equivalent_load = Load::make(s->value.type(), s->name, s->index, Buffer(), Parameter());
            values_stored.push_back(s->value);
            equivalent_loads.push_back(equivalent_load);
        }

        // Make a single expr containing all the lets, and also the
        // values those will take on in the next iteration. Then run
        // CSE on these two things together to find useful
        // subexpressions in common between this loop iteration and
        // the next.
        vector<Expr> curr_bundle_values = let_vars(lets);
        Expr curr_bundle = wrap_lets(pack_bundle(curr_bundle_values), lets);
        Expr next_bundle = wrap_lets(pack_bundle(curr_bundle_values), lets);
        Expr stores_bundle = wrap_lets(pack_bundle(values_stored), lets);
        Expr loads_bundle = wrap_lets(pack_bundle(equivalent_loads), lets);

        next_bundle = simplify(substitute(op->name, next_var, next_bundle));
        next_bundle = RenameVars().mutate(next_bundle);

        debug(3) << "Current: " << curr_bundle << "\n";
        debug(3) << "Next   : " << next_bundle << "\n";

        Expr together = pack_bundle({curr_bundle, next_bundle, stores_bundle, loads_bundle});

        together = common_subexpression_elimination(together, false);
        debug(3) << "\n After CSE: \n" << together << "\n\n";

        // The Let nodes on 'together' now contains good candidates of
        // things to save for the next loop iteration. However, they
        // don't give us a natural way to get the previous loop
        // iteration's version of those values. Do something similar
        // to the above process again to learn a mapping between
        // variables in the previous loop iteration and this one.

        together = unwrap_lets(together, lets);
        vector<Expr> unpacked = unpack_bundle(together);
        vector<Expr> curr_bundle2_values = let_vars(lets);
        vector<Expr> prev_bundle2_values = curr_bundle2_values;
        curr_bundle = unpacked[0];
        values_stored = unpack_bundle(unpacked[2]);
        equivalent_loads = unpack_bundle(unpacked[3]);

        // The values stored in the previous iteration are equivalent
        // to loading from the same buffers at their old store indices
        // in the current iteration.
        curr_bundle2_values.insert(curr_bundle2_values.end(), values_stored.begin(), values_stored.end());
        prev_bundle2_values.insert(prev_bundle2_values.end(), equivalent_loads.begin(), equivalent_loads.end());

        Expr curr_bundle2 = wrap_lets(pack_bundle(curr_bundle2_values), lets);
        Expr prev_bundle2 = wrap_lets(pack_bundle(prev_bundle2_values), lets);

        // Look back one loop iteration.
        prev_bundle2 = simplify(substitute(op->name, prev_var, prev_bundle2));
        prev_bundle2 = RenameVars().mutate(prev_bundle2);

        // Pair the values we need for the original let statements
        // with the values we'll be storing in the non-aliasing stores
        // we found and their equivalent loads. These are the terms
        // we'll definitely compute each loop iteration.
        curr_bundle = pack_bundle({curr_bundle, pack_bundle(values_stored)});
        curr_bundle = wrap_lets(curr_bundle, lets);

        debug(3) << "Prev bundle2 : " << prev_bundle2 << "\n";
        debug(3) << "Curr bundle2 : " << curr_bundle2 << "\n";

        together = pack_bundle({curr_bundle2, prev_bundle2, curr_bundle});

        debug(3) << "Together: " << together << "\n";
        together = common_subexpression_elimination(simplify(together), false);
        debug(3) << "Together: " << together << "\n";

        // We now have an expression where the mapping between
        // curr_bundle2 and prev_bundle2 tells us the next iteration's
        // version of each var, and curr_bundle tells us the set of
        // lets we need to inject in terms of those vars.

        struct CarriedValue {
            // The variable name of the value we'll save for the next loop iteration.
            string save_for_next_var;
            // The variable name of the value we'll reuse from the previous loop iteration.
            string reuse_from_prev_var;
            // The name of the scratch buffer to use.
            string scratch_name;
        };

        // The variables we'll carry over to the next loop
        // iteration, indexed by their later names or earlier names
        map<string, CarriedValue> carried_values_by_save_name;
        map<string, CarriedValue> carried_values_by_reuse_name;

        together = unwrap_lets(together, lets);
        vector<Expr> exprs = unpack_bundle(together);
        internal_assert(exprs.size() == 3);
        curr_bundle2_values = unpack_bundle(exprs[0]);
        prev_bundle2_values = unpack_bundle(exprs[1]);
        curr_bundle_values = unpack_bundle(exprs[2]);
        curr_bundle = wrap_used_lets(exprs[2], lets);

        debug(3) << "Stuff we need to compute: " << curr_bundle << "\n";

        debug(3) << "Lets:\n";

        for (pair<string, Expr> l : lets) {
            debug(3) << " " << l.first << " = " << l.second << "\n";
        }

        debug(3) << "Loop carry mapping:\n";

        for (size_t i = 0; i < curr_bundle2_values.size(); i++) {
            Expr curr_expr = curr_bundle2_values[i];
            Expr prev_expr = prev_bundle2_values[i];
            const Variable *prev = prev_expr.as<Variable>();
            const Variable *curr = curr_expr.as<Variable>();

            debug(3) << " " << curr_expr << " --> " << prev_expr << "\n";

            // Force them to be variables
            /*
            if (!curr) {
                string name = unique_name('t');
                Expr var = Variable::make(curr_expr.type(), name);
                //lets.push_back(make_pair(name, curr_expr));
                //curr_bundle = substitute(curr_expr, var, curr_bundle);
                curr_expr = var;
                curr = var.as<Variable>();
            }

            if (!prev) {
                string name = unique_name('t');
                Expr var = Variable::make(prev_expr.type(), name);
                //lets.push_back(make_pair(name, prev_expr));
                //curr_bundle = substitute(prev_expr, var, curr_bundle);
                prev_expr = var;
                prev = var.as<Variable>();
            }
            */

            if (!curr || !prev) {
                continue;
            }

            // The current bundle has to actually need to compute the
            // previous value and also use the next one for it to make
            // sense for us to save it.
            if (!expr_uses_var(curr_bundle, prev->name) ||
                !expr_uses_var(curr_bundle, curr->name)) {
                continue;
            }

            // Make sure that the value we will ultimately store to
            // a scratch buffer and load from the scratch buffer is
            // a value that is loaded or stored in the first place. Not
            // doing this has the unfortunate effect of storing CSEd
            // indices of loads and stores to scratch buffers. This is
            // sub-optimal for one reason because we then lose the ability
            // to reason about the alignment of indices on loads and stores.
            // For instance if we don't take this precaution we risk converting
            //
            //   for(y, 0, extent) {
            //      t10 = input[ramp(y*32 + 100)]
            //   }
            //
            //          to
            //
            //   allocate b[1], int32
            //   b[0] = 0
            //   for (y, 0, extent) {
            //      t1 = b[0]
            //      t10 = input[ramp(b[0] + 100)]
            //      b[0] = y*32
            // }
            if (!var_depends_on_load(curr_bundle, prev->name) ||
                !var_depends_on_load(curr_bundle, curr->name)) {
                continue;
            }

            if (prev->name == curr->name) {
                // This is just a loop invariant
                continue;
            }

            debug(3) << "*** " << curr_expr << " --> " << prev_expr << "\n";

            CarriedValue cv {curr->name, prev->name, unique_name('b')};
            carried_values_by_reuse_name[prev->name] = cv;
            carried_values_by_save_name[curr->name] = cv;
            debug(3) << " Carried value: " << curr->name
                     << " in iteration i-1 becomes " << prev->name
                     << " in iteration i\n";
        }

        debug(3) << "Values we need in terms of lets:\n";

        //vector<pair<string, Expr>> junk_lets;
        //curr_bundle = unwrap_lets(curr_bundle, junk_lets);

        // The original lets are the values in the first half of curr_bundle.
        //curr_bundle_values = unpack_bundle(curr_bundle);
        curr_bundle_values = unpack_bundle(curr_bundle_values[0]);
        for (size_t i = 0; i < curr_bundle_values.size(); i++) {
            debug(3) << " " << curr_bundle_values[i] << "\n";
            // They're probably vars - don't introduce pointless let statements.
            if (curr_bundle_values[i].as<Variable>()) {
                body = substitute(orig_lets[i].first, curr_bundle_values[i], body);
            } else {
                body = LetStmt::make(orig_lets[i].first, curr_bundle_values[i], body);
            }
        }

        vector<pair<string, Type>> scratch_buffers;
        vector<Stmt> save_stores;
        vector<Stmt> initialize_scratch_stores;

        // Now work through the lets making the stores to scratch and the loads from scratch
        vector<pair<string, Expr>> lets_with_reuse;
        for (pair<string, Expr> l : lets) {
            Type t = l.second.type();
            Expr idx = scratch_index(t);

            auto it = carried_values_by_reuse_name.find(l.first);
            if (it == carried_values_by_reuse_name.end()) {
                lets_with_reuse.push_back(l);
            } else {
                // We can reuse this value from the previous iteration
                // by loading from the scratch buffer.
                Expr value = Load::make(t, it->second.scratch_name, idx, Buffer(), Parameter());
                lets_with_reuse.push_back(make_pair(l.first, value));

                // Outside the loop, put the first loop iteration's
                // version of this value in the scratch buffer.
                Stmt init = Store::make(it->second.scratch_name, l.second, idx);
                initialize_scratch_stores.push_back(init);
            }

            it = carried_values_by_save_name.find(l.first);
            if (it != carried_values_by_save_name.end()) {
                // We need to store this for the next loop iteration to use.
                Expr value = Variable::make(t, it->second.save_for_next_var);
                save_stores.push_back(Store::make(it->second.scratch_name, value, idx));
                scratch_buffers.push_back(make_pair(it->second.scratch_name, value.type()));
            }
        }

        // Stick the save-for-next-iteration stores at the end of the loop body
        Stmt save_store_block = fold_right(save_stores, Block::make);
        if (save_store_block.defined()) {
            body = Block::make(body, save_store_block);
        }

        // The block of stores outside the loop body.
        Stmt initialize_block = fold_right(initialize_scratch_stores, Block::make);

        // Wrap the body in the let statements (only the necessary ones)
        body = wrap_used_lets(body, lets_with_reuse);

        // Rebuild the loop
        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);

        // Prepend the initial stores.
        if (initialize_block.defined()) {
            initialize_block = wrap_used_lets(initialize_block, lets);

            // Probably not strictly necessary, but it's confusing when
            // the initialization block uses the same variable names as
            // the inner loop, because printing IR doesn't show where the
            // scope of a LetStmt ends.
            initialize_block = RenameVars().mutate(initialize_block);

            // For the initialize block we're talking about the first
            // iteration's values of things.
            initialize_block = substitute(op->name, op->min, initialize_block);

            // Wrap the initialize block in the let statements *without reuse loads*
            stmt = Block::make(initialize_block, stmt);
        }

        // Make the stack allocations for the scratch buffers
        for (pair<string, Type> p : scratch_buffers) {
            stmt = Allocate::make(p.first, p.second, {}, const_true(), stmt);
        }

        // The initialize block is not safe to run if the loop would never have run
        if (initialize_block.defined()) {
            stmt = IfThenElse::make(op->extent > 0, stmt);
        }
    }
};

Stmt store_forwarding(Stmt s) {
    debug(3) << "\n\n ************* BEFORE: " << s << "\n";
    s = LiftFixedExpressions().mutate(s);
    debug(3) << "\n\n ************* Lift fixed exprs: " << s << "\n";
    s = StoreForwarding().mutate(s);
    debug(3) << "\n\n ************* Store forwarding: " << s << "\n";
    s = LiftFixedExpressions().mutate(s);
    debug(3) << "\n\n ************* Lift fixed exprs: " << s << "\n";
    s = LoopCarry().mutate(s);
    debug(3) << "\n\n ************* Loop carry: " << s << "\n";
    return s;
}


}
}
