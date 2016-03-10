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

Expr pack_bundle(vector<Expr> e) {
    return Call::make(Int(32), unique_name('b'), e, Call::Intrinsic);
}

vector<Expr> unpack_bundle(Expr e) {
    const Call *c = e.as<Call>();
    internal_assert(c) << "Not a bundle: " << e << "\n";
    return c->args;
}

// Convert Stmt/Expr to LetStmt/Let respectively
template<typename T> struct LetForType;
template<> struct LetForType<Stmt> {typedef LetStmt type;};
template<> struct LetForType<Expr> {typedef Let type;};

template<typename StmtOrExpr, typename LetStmtOrLet = typename LetForType<StmtOrExpr>::type>
StmtOrExpr unwrap_lets(StmtOrExpr e, vector<pair<string, Expr>> &l) {
    l.clear();
    while (const LetStmtOrLet *let = e.template as<LetStmtOrLet>()) {
        l.push_back(make_pair(let->name, let->value));
        e = let->body;
    }
    return e;
}

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

template<typename StmtOrExpr>
StmtOrExpr wrap_used_lets(StmtOrExpr e, const vector<pair<string, Expr>> &l) {
    return wrap_lets(e, l, true, false);
}

template<typename StmtOrExpr>
StmtOrExpr wrap_or_substitute_used_lets(StmtOrExpr e, const vector<pair<string, Expr>> &l) {
    return wrap_lets(e, l, true, true);
}

vector<Expr> let_vars(const vector<pair<string, Expr>> &l) {
    vector<Expr> result;
    for (pair<string, Expr> p : l) {
        result.push_back(Variable::make(p.second.type(), p.first));
    }
    return result;
}


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

Expr scratch_index(Type t) {
    if (t.lanes() == 1) {
        return 0;
    } else {
        return Ramp::make(0, 1, t.lanes());
    }
}

}

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

class UseScratch : public IRMutator {
    using IRMutator::visit;

    const Load *load;
    const Store *store;
    const string &name;

    void visit(const Load *op) {
        if (equal(Expr(op), Expr(load))) {
            Expr idx;

            expr = Load::make(op->type, name, scratch_index(op->type),
                              Buffer(), Parameter());
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        if (op->name == store->name &&
            equal(op->index, store->index)) {
            Expr value = mutate(op->value);

            string var_name = unique_name('t');
            Expr var = Variable::make(value.type(), var_name);
            Stmt store_to_orig = Store::make(op->name, var, op->index);
            string next_buf_name = name + ".next";
            Stmt store_to_next = Store::make(next_buf_name, var, scratch_index(value.type()));
            stmt = LetStmt::make(var_name, value, Block::make(store_to_orig, store_to_next));

            // TODO: storing to next is only necessary if there are later loads from scratch.
            value_var = Load::make(value.type(), next_buf_name, scratch_index(value.type()),
                                   Buffer(), Parameter());

            // We know no other stores are aliased with this store,
            // but there may be later loads of the old value, so we'd
            // better defer the transition from next to scratch to the
            // end of the loop.
        } else {
            IRMutator::visit(op);
        }
    }

public:
    UseScratch(const Load *l, const Store *s, const string &n) : load(l), store(s), name(n) {}
    Expr value_var;
};

class StoreForwarding : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {
        if (op->for_type != ForType::Serial) {
            IRMutator::visit(op);
            return;
        }

        Stmt body = mutate(op->body);

        // Find all the unconditional loads and stores
        FindLoadsAndStores f;
        op->body.accept(&f);

        struct ScratchBuf {
            string name;
            Expr initial_value;
        };

        vector<ScratchBuf> scratch_buffers;

        // If any of the loads will reuse the value stored on the
        // previous iteration, then make a stack variable to hold the
        // value as well, and load from that instead. This only works
        // if no other store aliases with the store in question.
        for (const Store *store : f.stores) {
            MightAliasWithAStore alias_tester(store);
            op->body.accept(&alias_tester);
            if (alias_tester.result) continue;

            // If the store is to a constant address, it's probably
            // already into a small stack allocation.
            if (is_const(store->index)) continue;

            for (const Load *load : f.loads) {
                if (load->name != store->name) continue;
                if (load->index.type() != store->index.type()) continue;
                Expr next_loop_var = Variable::make(Int(32), op->name) + 1;
                Expr next_load_index = substitute(op->name, next_loop_var, load->index);
                if (is_zero(simplify(next_load_index - store->index))) {
                    debug(3) << "Found opportunity\n"
                             << " load: " << load->index << "\n"
                             << " store: " << store->index << "\n";

                    // Make a scratch buffer
                    Expr init = substitute(op->name, op->min, load);
                    ScratchBuf b = {unique_name("b"), init};
                    scratch_buffers.push_back(b);

                    // Change the loads to loads from the scratch
                    // buffer, and change the computation to go via a
                    // local variable.
                    UseScratch u(load, store, b.name);
                    body = u.mutate(body);

                    // At the end of the loop body, store the local variable to the scratch
                    internal_assert(u.value_var.defined());
                    Expr idx = scratch_index(u.value_var.type());
                    Stmt store_to_scratch = Store::make(b.name, u.value_var, idx);
                    body = Block::make(body, store_to_scratch);

                    // That should cover all loads that match this
                    // store. Move on to the next store.
                    break;
                } else {
                    /*
                    debug(3) << "Missed opportunity\n"
                             << " load: " << load->index << "\n"
                             << " store: " << store->index << "\n";
                    */
                }
            }
        }

        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);

        Stmt store_init;

        for (auto b : scratch_buffers) {
            // Make and initialize the scratch buffer
            Stmt store =
                Store::make(b.name, b.initial_value, scratch_index(b.initial_value.type()));
            if (store_init.defined()) {
                store_init = Block::make(store_init, store);
            } else {
                store_init = store;
            }
        }

        if (store_init.defined()) {
            if (!is_positive_const(op->extent)) {
                store_init = IfThenElse::make(op->extent > 0, store_init);
            }
            stmt = Block::make(store_init, stmt);
        }

        for (auto b : scratch_buffers) {
            stmt = Allocate::make(b.name, b.initial_value.type(), {}, const_true(), stmt);
            stmt = Allocate::make(b.name + ".next", b.initial_value.type(), {}, const_true(), stmt);
        }
    }
};

class LoopCarryOverLoop : public IRMutator {
    const string &loop_var;
    Stmt loop_body;
    Expr first_iteration;

    vector<pair<string, Type>> scratch_buffers;

    using IRMutator::visit;

    // Return a new version of the expr, a list of stores to do after
    // the expr, and a list let stmt that the whole thing needs to be
    // wrapped in.
    Expr consider_expr(Expr val, Expr equivalent_load, vector<Stmt> &result_stores, vector<pair<string, Expr> > &result_lets) {
        // TODO: worry about aliasing, inner loops

        // Our first task is to find candidate expressions that are
        // worth scavenging from a previous loop iteration. We could
        // consider all pairs of Exprs, but that would be
        // slow. Instead we construct the previous loop iteration's
        // version of the value, and run CSE on this version and the
        // previous version to find shared expressions between them.
        set<string> common_lets;

        {
            // The value of the RHS on the previous loop iteration
            Expr prev_val = simplify(substitute(loop_var, Variable::make(Int(32), loop_var) - 1, val));

            // Put them together and run CSE to look for shared subexpressions
            string dummy = unique_name('d');
            Expr together = Call::make(Int(32), dummy, {prev_val, val}, Call::Intrinsic);

            debug(3) << "Entering cse\n";
            together = common_subexpression_elimination(together, false);

            debug(3) << together << "\n";

            vector<pair<string, Expr>> lets;
            while (const Let *l = together.as<Let>()) {
                lets.push_back(make_pair(l->name, l->value));
                together = l->body;
            }

            // Some of these lets might just be common subexpressions that
            // only occur in val or prev_val. We want the ones that appear
            // in both.
            const Call *c = together.as<Call>();
            internal_assert(c && c->name == dummy && c->args.size() == 2);
            prev_val = c->args[0];
            val = c->args[1];

            while (!lets.empty()) {
                pair<string, Expr> l = lets.back();
                lets.pop_back();
                bool used_in_val = expr_uses_var(val, l.first);
                bool used_in_prev_val = expr_uses_var(prev_val, l.first);
                if (used_in_val) {
                    val = Let::make(l.first, l.second, val);
                }
                if (used_in_prev_val) {
                    prev_val = Let::make(l.first, l.second, val);
                }
                if (used_in_val && used_in_prev_val) {
                    common_lets.insert(l.first);
                }
            }
        }

        // We have now mutated val so that it has a bunch of let exprs
        // around it. The ones that appear in "common_lets" are the
        // ones worth scavenging from previous loop iterations.

        // Now peel off those lets and make the next loop iteration's version of each.


        // A mapping from vars to the next loop iteration's version of that var

        struct CarriedValue {
            string curr_name;    // The variable name in the current (later) iteration
            string prev_name;    // The variable name in the earlier iteration
            string scratch_name; // The name of the scratch buffer we'll use
        };

        // The variables we'll carry over to the next loop
        // iteration, indexed by their later names or earlier names
        map<string, CarriedValue> carried_values_by_curr;
        map<string, CarriedValue> carried_values_by_prev;
        {
            vector<pair<string, Expr>> lets;

            map<string, Expr> next_version;
            map<string, Expr> prev_version;
            next_version[loop_var] = Variable::make(Int(32), loop_var) + 1;
            prev_version[loop_var] = Variable::make(Int(32), loop_var) - 1;

            while (const Let *l = val.as<Let>()) {
                string name = l->name;
                Expr value = l->value;

                if (common_lets.count(name)) {
                    debug(3) << "\nFound loop carry sub-expression: " << l->name << " = " << l->value << "\n";
                    // 'value' is something we already computed last
                    // iteration. So this iteration, we need to compute
                    // next iteration's version of it.
                    Expr next_value = substitute(next_version, value);
                    Expr next_value_var = Variable::make(value.type(), name + ".next");
                    next_version[name] = next_value_var;

                    Expr prev_value = substitute(prev_version, value);
                    Expr prev_value_var = Variable::make(value.type(), name + ".prev");
                    prev_version[name] = prev_value_var;

                    lets.push_back(make_pair(name + ".next", next_value));
                    lets.push_back(make_pair(name + ".prev", prev_value));

                }

                lets.push_back(make_pair(name, value));

                val = l->body;
            }

            // We now want to do CSE again on the value and the previous
            // version of all the exprs so that we make the most
            // effective use of them.
            vector<Expr> next_values;
            for (string c : common_lets) {
                Expr e = next_version[c];
                next_values.push_back(Variable::make(e.type(), c));
                next_values.push_back(e);
            }

            // The next loop iteration's version of the value itself
            // can be gotten by reloading the thing we stored to last
            // iteration.
            if (equivalent_load.defined()) {
                next_values.push_back(substitute(prev_version, equivalent_load));
                next_values.push_back(val);
            }

            // Stick the value itself on the end to participate in the CSE
            next_values.push_back(val);

            string dummy = unique_name('d');
            Expr together = Call::make(Int(32), dummy, next_values, Call::Intrinsic);

            // Rewrap including all the new ones
            while (!lets.empty()) {
                pair<string, Expr> l = lets.back();
                lets.pop_back();
                together = Let::make(l.first, l.second, together);
            }

            debug(3) << " Rewrapped: " << together << "\n";

            together = common_subexpression_elimination(simplify(together), false);

            debug(3) << " Gathered and CSE'd: " << together << "\n";

            {
                val = together;
                while (const Let *l = val.as<Let>()) {
                    lets.push_back(make_pair(l->name, l->value));
                    val = l->body;
                }
                const Call *call = val.as<Call>();
                internal_assert(call);
                for (size_t i = 0; i < call->args.size() - 1; i+=2) {
                    const Variable *prev = call->args[i+1].as<Variable>();
                    const Variable *curr = call->args[i].as<Variable>();
                    if (!prev || !curr) continue;
                    if (prev->name == curr->name) {
                        // This is just a loop invariant
                        continue;
                    }
                    CarriedValue cv {curr->name, prev->name, unique_name('b')};
                    carried_values_by_curr[curr->name] = cv;
                    carried_values_by_prev[prev->name] = cv;
                    debug(3) << " Carried value: " << prev->name
                             << " in iteration i-1 becomes " << curr->name
                             << " in iteration i\n";
                }

                val = call->args.back();

                debug(3) << " With carried values identified: " << val << "\n";

                vector<pair<string, Expr>> tmp;

                while (!lets.empty()) {
                    pair<string, Expr> l = lets.back();
                    lets.pop_back();

                    Type t = l.second.type();

                    auto it = carried_values_by_curr.find(l.first);
                    if (it == carried_values_by_curr.end()) {
                        tmp.push_back(l);
                    } else {
                        // On all but the first loop iteration, we can
                        // reuse the previous iteration's version of
                        // this var
                        Expr idx = scratch_index(t);
                        Expr value = Load::make(t, it->second.scratch_name, idx, Buffer(), Parameter());
                        value = select(first_iteration, l.second, value);
                        tmp.push_back(make_pair(l.first, value));
                    }

                    it = carried_values_by_prev.find(l.first);
                    if (it != carried_values_by_prev.end()) {
                        // We need to store this for the next loop iteration to use.
                        Expr idx = scratch_index(t);
                        Expr value = Variable::make(t, it->second.prev_name);
                        result_stores.push_back(Store::make(it->second.scratch_name, value, idx));
                    }
                }

                while (!tmp.empty()) {
                    result_lets.push_back(tmp.back());
                    tmp.pop_back();
                }
            }
        }

        for (pair<string, Expr> l : result_lets) {
            debug(3) << l.first << " = " << l.second << "\n";
        }

        return val;
    }

    Stmt wrap_with_lets(Stmt s, vector<pair<string, Expr>> lets) {
        while (!lets.empty()) {
            pair<string, Expr> l = lets.back();
            lets.pop_back();
            s = LetStmt::make(l.first, l.second, s);
        }
        return s;
    }

    Stmt append_stores(Stmt s, const vector<Stmt> &stores) {
        for (Stmt store_stmt : stores) {
            const Store *store = store_stmt.as<Store>();
            internal_assert(store);
            scratch_buffers.push_back(make_pair(store->name, store->value.type()));
            s = Block::make(s, store_stmt);
        }
        return s;
    }

    void visit(const LetStmt *op) {
        vector<Stmt> stores;
        vector<pair<string, Expr>> lets;
        Expr new_value = consider_expr(op->value, Expr(), stores, lets);
        if (stores.empty()) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, new_value, op->body);
            stmt = append_stores(stmt, stores);
            stmt = wrap_with_lets(stmt, lets);
        }
    }

    void visit(const Store *op) {
        vector<Stmt> stores;
        vector<pair<string, Expr>> lets;


        Expr equivalent_load = Load::make(op->value.type(), op->name, op->index, Buffer(), Parameter());

        Expr new_value = consider_expr(op->value, equivalent_load, stores, lets);
        Expr new_index = consider_expr(op->index, Expr(), stores, lets);

        if (stores.empty()) {
            stmt = op;
        } else {
            stmt = Store::make(op->name, new_value, new_index);
            stmt = append_stores(stmt, stores);
            stmt = wrap_with_lets(stmt, lets);
        }
    }


public:

    Stmt wrap_with_scratch_allocations(Stmt stmt) {
        for (pair<string, Type> s : scratch_buffers) {
            stmt = Allocate::make(s.first, s.second, {}, const_true(), stmt);
        }
        return stmt;
    }

    LoopCarryOverLoop(const For *f, Expr fi) : loop_var(f->name),
                                               loop_body(f->body),
                                               first_iteration(fi) {
    }
};

// Are there subexpressions I can reuse from previous loop
// iterations. Even something as simple as a load is worth keeping in
// a register.
class LoopCarry : public IRMutator {

    using IRMutator::visit;

    void visit(const For *op) {
        if (op->for_type != ForType::Serial) {
            IRMutator::visit(op);
            return;
        }

        Stmt body = mutate(op->body);

        string first_iteration = unique_name('f');
        Expr first_iteration_var = Variable::make(Bool(), first_iteration);
        LoopCarryOverLoop lc(op, first_iteration_var);
        Stmt new_body = lc.mutate(body);

        if (new_body.same_as(op->body)) {
            stmt = op;
        } else {
            Stmt first_iteration_body  = substitute(first_iteration, const_true(), new_body);
            first_iteration_body = substitute(op->name, op->min, first_iteration_body);
            Stmt other_iterations_body = substitute(first_iteration, const_false(), new_body);
            Stmt other_iterations_loop =
                For::make(op->name, op->min + 1, op->extent - 1, op->for_type, op->device_api, other_iterations_body);
            stmt = Block::make(first_iteration_body, other_iterations_loop);
            stmt = lc.wrap_with_scratch_allocations(stmt);
            stmt = IfThenElse::make(op->extent > 0, stmt, Stmt());
        }
    }
};

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

class StoreForwarding2 : public IRMutator {
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

bool contains_load(Expr e) {
    ContainsAliasedLoad c;
    e.accept(&c);
    return c.result;
}

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

namespace {
// A term in a summation
struct Term {
    Expr expr;
    bool positive;
};

// Lift an add or subtract node into a list of the terms and their signs
void gather_summation(Expr e, bool positive, vector<Term> &result) {
    if (const Add *add = e.as<Add>()) {
        gather_summation(add->a, positive, result);
        gather_summation(add->b, positive, result);
    } else if (const Sub *sub = e.as<Sub>()) {
        gather_summation(sub->a, positive, result);
        gather_summation(sub->b, !positive, result);
    } else {
        result.push_back({e, positive});
    }
}

// Recombine two terms in a summation
Term combine_terms(Term a, Term b) {
    if (!a.expr.defined()) {
        return b;
    } else if (!b.expr.defined()) {
        return a;
    } else if (a.positive == b.positive) {
        return {a.expr + b.expr, a.positive};
    } else if (a.positive) {
        return {a.expr - b.expr, true};
    } else {
        return {b.expr - a.expr, true};
    }
}
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

    /*
      // TODO: this code is apparently buggy
    void visit_add_or_sub(Expr e) {
        // We're in an add node that couldn't be naively lifted. It
        // must contain some liftable components and some not-liftable
        // components.
        vector<Term> terms;
        gather_summation(e, true, terms);

        // Try to lift each term
        Term lifted_term, not_lifted_term;
        for (Term t : terms) {
            t.expr = mutate(t.expr);
            if (t.expr.as<Variable>()) {
                lifted_term = combine_terms(lifted_term, t);
            } else {
                not_lifted_term = combine_terms(not_lifted_term, t);
            }
        }

        if (lifted_term.expr.defined() &&
            !lifted_term.expr.as<Variable>()) {
            // Lift the combination of the liftable terms
            lifted_term.expr = lift(lifted_term.expr);
        }

        Term both = combine_terms(lifted_term, not_lifted_term);

        // If either arg to combine_terms is positive, the result is
        // positive, so at this point we should really be positive
        // because we started with at least one positive term.
        internal_assert(both.positive) << "This shouldn't be possible\n";
        expr = both.expr;
    }

    void visit(const Add *op) {
        visit_add_or_sub(Expr(op));
    }

    void visit(const Sub *op) {
        visit_add_or_sub(Expr(op));
    }
    */

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
        if (false && it != lifted_expr_names.end()) {
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

class LoopCarry2 : public IRMutator {
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
        //here pdb
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

            // debug(3) << " " << curr_expr << " --> " << prev_expr << "\n";

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
    s = StoreForwarding2().mutate(s);
    debug(3) << "\n\n ************* Store forwarding: " << s << "\n";
    s = LiftFixedExpressions().mutate(s);
    debug(3) << "\n\n ************* Lift fixed exprs: " << s << "\n";
    s = LoopCarry2().mutate(s);
    debug(3) << "\n\n ************* Loop carry: " << s << "\n";
    return s;
}


}
}
