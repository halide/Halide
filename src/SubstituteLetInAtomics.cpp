#include "SubstituteLetInAtomics.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

class SubstituteLetInAtomics : public IRMutator {
    using IRMutator::visit;

    std::set<std::string> stores_in_atomic;
    Scope<Expr> let_scope;
    bool check_for_lets = false;

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        let_scope.push(op->name, op->value);
        Expr body = mutate(op->body);
        let_scope.pop(op->name);
        if (value.same_as(op->value) &&
            body.same_as(op->body)) {
            return op;
        }
        // Remove Let definition if the body does not use it
        if (expr_uses_var(body, op->name)) {
            return Let::make(op->name, std::move(value), std::move(body));
        } else {
            return body;
        }
    }

    Stmt visit(const LetStmt *op) override {
        Expr value = mutate(op->value);
        let_scope.push(op->name, op->value);
        Stmt body = mutate(op->body);
        let_scope.pop(op->name);
        if (value.same_as(op->value) &&
            body.same_as(op->body)) {
            return op;
        }
        // Remove Let definition if the body does not use it
        if (stmt_uses_var(body, op->name)) {
            return LetStmt::make(op->name, std::move(value), std::move(body));
        } else {
            return body;
        }
    }

    Stmt visit(const Atomic *op) override {
        if (op->mutex_name != "") {
            return IRMutator::visit(op);
        }

        // Find all stores inside the atomic node
        stores_in_atomic.clear();
        class CollectStores : public IRGraphVisitor {
        public:
            using IRGraphVisitor::visit;

            void visit(const Store *op) override {
                stores_in_atomic.insert(op->name);
            }

            CollectStores(std::set<std::string> &stores_in_atomic)
                : stores_in_atomic(stores_in_atomic) {
            }

            std::set<std::string> &stores_in_atomic;
        } collector(stores_in_atomic);
        op->body.accept(&collector);

        check_for_lets = true;
        Stmt body = mutate(op->body);
        check_for_lets = false;

        if (body.same_as(op->body)) {
            return op;
        } else {
            return Atomic::make(op->mutex_name, op->mutex_indices, std::move(body));
        }
    }

    Stmt visit(const Store *op) override {
        if (!check_for_lets) {
            return IRMutator::visit(op);
        }

        class CollectVariables : public IRGraphVisitor {
        public:
            using IRGraphVisitor::visit;

            void visit(const Variable *op) override {
                if (let_scope.contains(op->name)) {
                    variables.push_back({op->name, let_scope.get(op->name)});
                }
            }

            CollectVariables(const Scope<Expr> &let_scope)
                : let_scope(let_scope) {
            }

            vector<pair<string, Expr>> variables;
            const Scope<Expr> &let_scope;
        } collector(let_scope);

        op->value.accept(&collector);

        // If a let expression references any store buffer inside atomic,
        // we "should" substitute the let in. If it only references the current
        // store buffer at the same index, we "can" substitute the let in.
        // If we should but can't substitute, triggers an assertion.
        class CheckLoadExpr : public IRGraphVisitor {
        public:
            using IRGraphVisitor::visit;

            void visit(const Load *op) override {
                if (stores_in_atomic.find(op->name) != stores_in_atomic.end()) {
                    should_substitute_let = true;
                    if (op->name != current_buffer_name || !equal(op->index, store_index)) {
                        can_substitute_let = false;
                    }
                }
            }

            CheckLoadExpr(const set<string> &stores_in_atomic,
                          const std::string &current_buffer_name,
                          Expr store_index)
                : stores_in_atomic(stores_in_atomic),
                  current_buffer_name(current_buffer_name),
                  store_index(store_index) {
            }

            const set<string> &stores_in_atomic;
            const string &current_buffer_name;
            Expr store_index;
            bool should_substitute_let;
            bool can_substitute_let;
        } checker(stores_in_atomic, op->name, op->index);

        Expr predicate = mutate(op->predicate);
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);
        for (auto it : collector.variables) {
            const string &name = it.first;
            Expr e = it.second;
            checker.should_substitute_let = false;
            checker.can_substitute_let = true;
            e.accept(&checker);
            if (checker.should_substitute_let) {
                internal_assert(checker.can_substitute_let) <<
                    "Cannot ensure atomic operations in an atomic node. " <<
                    "Most likely some lowering passes lifted a variables " <<
                    "that we cannot substitute back in.\n";
                internal_assert(checker.can_substitute_let);
                value = substitute(name, e, value);
            }
        }

        if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
            return op;
        }
        return Store::make(op->name, std::move(value), std::move(index), op->param, std::move(predicate), op->alignment);
    }
};

}  // namespace

Stmt substitute_let_in_atomics(Stmt s) {
    s = SubstituteLetInAtomics().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
