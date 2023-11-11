#include "SplitTuples.h"

#include "Bounds.h"
#include "CSE.h"
#include "ExprUsesVar.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

// Collect all value indices of internal halide calls.
class FindCallValueIndices : public IRVisitor {
public:
    const string func;
    map<string, set<int>> func_value_indices;

    using IRVisitor::visit;

    void visit(const Call *call) override {
        IRVisitor::visit(call);
        if ((call->call_type == Call::Halide) && call->func.defined()) {
            func_value_indices[call->name].insert(call->value_index);
        }
    }
};

// Visitor and helper function to test if a piece of IR uses an extern image.
class UsesExternImage : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *c) override {
        if (c->call_type == Call::Image) {
            result = true;
        } else {
            IRVisitor::visit(c);
        }
    }

public:
    UsesExternImage() = default;
    bool result = false;
};

inline bool uses_extern_image(const Stmt &s) {
    UsesExternImage uses;
    s.accept(&uses);
    return uses.result;
}

class SplitTuples : public IRMutator {
    using IRMutator::visit;

    map<string, set<int>> func_value_indices;
    map<string, int> hoisted_tuple_count;

    Stmt visit(const HoistedStorage *op) override {
        hoisted_tuple_count[op->name] = 0;
        Stmt body = mutate(op->body);
        if (hoisted_tuple_count[op->name] > 1) {
            for (int ix = 0; ix < hoisted_tuple_count[op->name]; ix++) {
                body = HoistedStorage::make(op->name + "." + std::to_string(ix), body);
            }
            return body;
        } else {
            return HoistedStorage::make(op->name, body);
        }
    }

    Stmt visit(const Realize *op) override {
        ScopedBinding<int> bind(realizations, op->name, 0);
        if (op->types.size() > 1) {
            // If there is a corresponding HoistedStorage node record the new number of
            // realizes.
            if (hoisted_tuple_count.count(op->name)) {
                hoisted_tuple_count[op->name] = op->types.size();
            }
            // Make a nested set of realize nodes for each tuple element
            Stmt body = mutate(op->body);
            Expr condition = mutate(op->condition);
            for (int i = (int)op->types.size() - 1; i >= 0; i--) {
                body = Realize::make(op->name + "." + std::to_string(i),
                                     {op->types[i]}, op->memory_type,
                                     op->bounds, condition, body);
            }
            return body;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const For *op) override {
        FindCallValueIndices find;
        op->body.accept(&find);

        ScopedValue<map<string, set<int>>> func_value_indices_v(func_value_indices, find.func_value_indices);
        return IRMutator::visit(op);
    }

    Stmt visit(const Prefetch *op) override {
        if (!op->prefetch.param.defined() && (op->types.size() > 1)) {
            Stmt body = mutate(op->body);
            // Split the prefetch from a multi-dimensional halide tuple to
            // prefetches of each tuple element. Keep only prefetches of
            // elements that are actually used in the loop body.
            const auto &indices = func_value_indices.find(op->name);
            internal_assert(indices != func_value_indices.end());

            for (const auto &idx : indices->second) {
                internal_assert(idx < (int)op->types.size());
                body = Prefetch::make(op->name + "." + std::to_string(idx), {op->types[(idx)]}, op->bounds, op->prefetch, op->condition, std::move(body));
            }
            return body;
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Call *op) override {
        if (op->call_type == Call::Halide) {
            auto it = env.find(op->name);
            internal_assert(it != env.end());
            Function f = it->second;
            string name = op->name;
            bool changed_name = false;
            if (f.outputs() > 1) {
                name += "." + std::to_string(op->value_index);
                changed_name = true;
            }
            auto [args, changed_args] = mutate_with_changes(op->args);
            // It's safe to hook up the pointer to the function
            // unconditionally. This expr never gets held by a
            // Function, so there can't be a cycle. We do this even
            // for scalar provides.
            if (changed_name || changed_args) {
                return Call::make(op->type, name, args, op->call_type, f.get_contents());
            } else {
                return op;
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit_provide(const Provide *op, const Atomic *atomic) {

        if (op->values.size() == 1) {
            if (atomic) {
                return IRMutator::visit(atomic);
            } else {
                return IRMutator::visit(op);
            }
        }

        // Mutate the args
        auto args = mutate(op->args);

        // Get the Function
        auto it = env.find(op->name);
        internal_assert(it != env.end());
        Function f = it->second;

        // For the new value of each tuple component, what existing
        // tuple components does it already depend on?
        vector<set<int>> dependencies(op->values.size());
        for (int i = 0; i < (int)op->values.size(); i++) {
            class Checker : public IRVisitor {
                using IRVisitor::visit;
                vector<pair<string, Expr>> lets;

                void visit(const Let *op) override {
                    op->value.accept(this);
                    lets.emplace_back(op->name, op->value);
                    op->body.accept(this);
                    lets.pop_back();
                }

                void visit(const Call *op) override {
                    if (op->call_type == Call::Halide &&
                        op->name == func_name &&
                        could_alias(op->args, store_args)) {
                        deps.insert(op->value_index);
                    }
                }

                bool could_alias(const vector<Expr> &a, const vector<Expr> &b) {
                    internal_assert(a.size() == b.size());
                    // Construct a boolean Expr that says the addresses are equal
                    Expr aliases = const_true();
                    for (size_t i = 0; i < a.size(); i++) {
                        aliases = aliases && (a[i] == b[i]);
                    }
                    // Might need some of the containing lets
                    for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                        if (expr_uses_var(aliases, it->first)) {
                            aliases = Let::make(it->first, it->second, aliases);
                        }
                    }
                    return !can_prove(!aliases);
                }

            public:
                set<int> &deps;
                const string &func_name;
                const vector<Expr> &store_args;
                Checker(set<int> &deps,
                        const string &func_name,
                        const vector<Expr> &store_args)
                    : deps(deps), func_name(func_name), store_args(store_args) {
                }
            } checker(dependencies[i], op->name, args);
            op->values[i].accept(&checker);
        }

        // Build clusters of tuple components where two components
        // belong to the same cluster if any of their loads or stores
        // may alias
        vector<vector<int>> clusters;
        // Reserve space so that we can use pointers to clusters.
        clusters.reserve(op->values.size());
        for (int i = 0; i < (int)op->values.size(); i++) {
            // What clusters does it already belong to?
            vector<int> *owning_cluster = nullptr;
            for (auto &c : clusters) {
                bool belongs_to_this_cluster = false;
                for (int j : c) {
                    if (dependencies[j].count(i) ||
                        dependencies[i].count(j)) {
                        belongs_to_this_cluster = true;
                        break;
                    }
                }
                if (belongs_to_this_cluster) {
                    if (owning_cluster) {
                        // It's already in a cluster! We need to merge the clusters.
                        owning_cluster->insert(owning_cluster->end(), c.begin(), c.end());
                        c.clear();
                    } else {
                        owning_cluster = &c;
                        c.push_back(i);
                    }
                }
            }
            if (!owning_cluster) {
                // Make a new cluster
                clusters.emplace_back();
                clusters.back().push_back(i);
            }
        }

        // If each cluster has only a single store in it, we can use
        // CAS loops or atomic adds and avoid ever needing to wrap
        // things in a mutex. We express this using separate atomic
        // nodes per store. If there's no mutex involved at all, then
        // there's no benefit in packing things together into a single
        // critical section.
        bool separate_atomic_nodes_per_store =
            ((atomic && atomic->mutex_name.empty()) ||
             (clusters.size() == op->values.size()));

        // For each cluster, build a list of scalar provide
        // statements, and a list of lets to wrap them.
        vector<Stmt> result;
        for (auto &c : clusters) {
            if (c.empty()) {
                continue;
            }
            std::sort(c.begin(), c.end());
            vector<Stmt> provides;
            vector<pair<string, Expr>> lets;

            Stmt s;

            if (c.size() == 1) {
                // Just make a provide node
                int i = *c.begin();
                string name = op->name + "." + std::to_string(i);
                s = Provide::make(name, {mutate(op->values[i])}, args, op->predicate);
            } else {
                // Make a list of let statements that compute the
                // values (doing any loads), and then a block of
                // provide statements that do the stores.
                for (auto i : c) {
                    string name = op->name + "." + std::to_string(i);
                    string var_name = name + ".value";
                    Expr val = mutate(op->values[i]);
                    if (!is_undef(val)) {
                        lets.emplace_back(var_name, val);
                        val = Variable::make(val.type(), var_name);
                    }
                    provides.push_back(Provide::make(name, {val}, args, op->predicate));
                }

                s = Block::make(provides);

                while (!lets.empty()) {
                    auto p = lets.back();
                    lets.pop_back();
                    s = LetStmt::make(p.first, p.second, s);
                }
            }

            if (atomic && separate_atomic_nodes_per_store) {
                s = Atomic::make(atomic->producer_name, atomic->mutex_name, s);
            }

            internal_assert(s.defined());
            result.push_back(s);
        }

        {
            Stmt s = Block::make(result);
            if (atomic && !separate_atomic_nodes_per_store) {
                s = Atomic::make(atomic->producer_name, atomic->mutex_name, s);
            }
            return s;
        }
    }

    Stmt visit(const Provide *op) override {
        return visit_provide(op, nullptr);
    }

    Stmt visit(const Atomic *op) override {
        // At this point in lowering, the only child of an atomic node
        // should be a single provide node. We haven't many any
        // statement mutations yet that would put things in between
        // the provide and the atomic.
        if (const Provide *p = op->body.as<Provide>()) {
            return visit_provide(p, op);
        } else {
            return IRMutator::visit(op);
        }
    }

    const map<string, Function> &env;
    Scope<int> realizations;

public:
    SplitTuples(const map<string, Function> &e)
        : env(e) {
    }
};

class SplitScatterGather : public IRMutator {
    using IRMutator::visit;

    // The enclosing producer node. Used for error messages.
    const ProducerConsumer *producer = nullptr;

    class GetScatterGatherSize : public IRVisitor {
        bool permitted = true;
        using IRVisitor::visit;
        void visit(const Call *op) override {
            if (op->is_intrinsic(Call::scatter_gather)) {
                user_assert(permitted)
                    << "Can't nest an expression tuple inside another in definition of "
                    << producer_name;
                if (result == 0) {
                    result = (int)op->args.size();
                } else {
                    user_assert((int)op->args.size() == result)
                        << "Expression tuples of mismatched sizes used in definition of "
                        << producer_name << ": " << result << " vs " << op->args.size();
                }
                // No nesting tuples
                permitted = false;
                IRVisitor::visit(op);
                permitted = true;
            } else {
                IRVisitor::visit(op);
            }
        }

    public:
        string producer_name;
        int result = 0;
    } get_scatter_gather_size_visitor;

    int get_scatter_gather_size(const IRNode *op) {
        if (producer) {
            get_scatter_gather_size_visitor.producer_name = producer->name;
        }
        get_scatter_gather_size_visitor.result = 0;
        op->accept(&get_scatter_gather_size_visitor);

        // Maybe the user did something like jam a gather op into a
        // constraint or tile size, so this is a user_assert.
        user_assert(producer || get_scatter_gather_size_visitor.result == 0)
            << "scatter/gather expression used outside of a Func definition";

        return get_scatter_gather_size_visitor.result;
    }

    class ExtractScatterGatherElement : public IRMutator {
        using IRMutator::visit;
        Expr visit(const Call *op) override {
            if (op->is_intrinsic(Call::scatter_gather)) {
                // No need to recursively mutate because we've
                // already asserted that these aren't nested.
                internal_assert(idx < (int)op->args.size());
                return op->args[idx];
            } else {
                return IRMutator::visit(op);
            }
        }

    public:
        int idx;
    } extractor;

    Stmt visit(const ProducerConsumer *op) override {
        ScopedValue<const ProducerConsumer *> old(producer, op->is_producer ? op : producer);
        return IRMutator::visit(op);
    }

    Stmt visit_gather_let_stmt(const LetStmt *op, int size) {
        // Split this variable into the gather components
        vector<pair<string, Expr>> lets;
        vector<Expr> vars;
        for (extractor.idx = 0; extractor.idx < size; extractor.idx++) {
            string name = unique_name(op->name + "." + std::to_string(extractor.idx));
            lets.emplace_back(name, extractor.mutate(op->value));
            vars.push_back(Variable::make(op->value.type(), name));
        }

        Stmt body = op->body;
        Expr gather_replacement = Call::make(op->value.type(),
                                             Call::scatter_gather,
                                             vars,
                                             Call::PureIntrinsic);
        body = substitute(op->name, gather_replacement, body);
        body = mutate(body);

        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            body = LetStmt::make(it->first, it->second, body);
        }

        return body;
    }

    Stmt visit(const LetStmt *op) override {
        vector<pair<string, Expr>> lets;
        int size = 0;
        Stmt body;
        do {
            body = op->body;
            size = get_scatter_gather_size(op->value.get());
            if (size != 0) {
                break;
            }
            lets.emplace_back(op->name, op->value);
            op = body.as<LetStmt>();
        } while (op);

        if (size) {
            internal_assert(op);
            body = visit_gather_let_stmt(op, size);
        } else {
            internal_assert(op == nullptr);
            body = mutate(body);
        }

        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            body = LetStmt::make(it->first, it->second, body);
        }

        return body;
    }

    Stmt visit(const Provide *op) override {
        int size = get_scatter_gather_size(op);

        if (size == 0) {
            return IRMutator::visit(op);
        }

        // The LHS should contain at least one scatter op, or our scatters
        // all go to the same place. Is it worth asserting this? It
        // could be a bug, or it could be some sort of degenerate base case.

        // Fork the args and the RHS into their various versions
        vector<Stmt> provides;
        vector<string> names;
        vector<Expr> exprs;
        for (extractor.idx = 0; extractor.idx < size; extractor.idx++) {
            vector<Expr> args = op->args;
            for (Expr &a : args) {
                string name = unique_name('t');
                exprs.push_back(extractor.mutate(a));
                names.push_back(name);
                a = Variable::make(a.type(), name);
            }
            vector<Expr> values = op->values;
            for (Expr &v : values) {
                v = extractor.mutate(v);
                string name = unique_name('t');
                exprs.push_back(extractor.mutate(v));
                names.push_back(name);
                v = Variable::make(v.type(), name);
            }
            provides.push_back(Provide::make(op->name, values, args, op->predicate));
        }

        Stmt s = Block::make(provides);
        // We just duplicated all the non-scatter/gather stuff too,
        // so do joint CSE on the exprs
        Expr bundle = Call::make(Int(32), Call::bundle, exprs, Call::PureIntrinsic);
        bundle = common_subexpression_elimination(bundle);

        vector<pair<string, Expr>> lets;
        while (const Let *let = bundle.as<Let>()) {
            lets.emplace_back(let->name, let->value);
            bundle = let->body;
        }
        const Call *c = bundle.as<Call>();
        internal_assert(c && c->is_intrinsic(Call::bundle));
        for (size_t i = 0; i < exprs.size(); i++) {
            if (is_pure(c->args[i])) {
                // names[i] is only used once, so if the value is pure
                // it should be substituted in
                s = substitute(names[i], c->args[i], s);
            } else {
                lets.emplace_back(names[i], c->args[i]);
            }
        }

        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            s = LetStmt::make(it->first, it->second, s);
        }

        return s;
    }
};

}  // namespace

Stmt split_tuples(const Stmt &stmt, const map<string, Function> &env) {
    Stmt s = SplitTuples(env).mutate(stmt);
    s = SplitScatterGather().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
