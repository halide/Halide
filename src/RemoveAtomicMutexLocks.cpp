#include "RemoveAtomicMutexLocks.h"
#include "IRVisitor.h"
#include "IRMutator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::set;

namespace {

class FindCall : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    void visit(const Call *op) override {
        if (call_names.find(op->name) != call_names.end()) {
            found = true;
        }
        for (size_t i = 0; i < op->args.size(); i++) {
            include(op->args[i]);
        }
    }

    set<string> call_names;
    bool found = false;

public:

    bool find_call(Expr e, const set<string> &call_names) {
        found = false;
        this->call_names = call_names;
        include(e);
        return found;
    }
};

class FindAtomicLetBindings : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Let *op) override {
        op->value.accept(this);
        let_bindings.push(op->name, op->value);
        op->body.accept(this);
        let_bindings.pop(op->name);
    }

    void visit(const LetStmt *op) override {
        op->value.accept(this);
        let_bindings.push(op->name, op->value);
        op->body.accept(this);
        let_bindings.pop(op->name);
    }

    void visit(const Variable *op) override {
        if (inside_provide) {
            if (let_bindings.contains(op->name)) {
                Expr e = let_bindings.get(op->name);
                FindCall finder;
                if (finder.find_call(e, provide_names)) {
                    found = true;
                }
            }
        }
    }

    void visit(const Provide *op) override {
        for (size_t i = 0; i < op->values.size(); i++) {
            inside_provide = true;
            op->values[i].accept(this);
            inside_provide = false;
        }
        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i].accept(this);
        }
    }

    bool inside_provide;
    set<string> provide_names;
    Scope<Expr> let_bindings;
public:
    FindAtomicLetBindings(const set<string> &provide_names)
        : provide_names(provide_names) {}

    bool found = false;
};

class CollectProvideNames : public IRGraphVisitor {
    using IRGraphVisitor::visit;
    
    void visit(const Provide *op) override {
        for (size_t i = 0; i < op->values.size(); i++) {
            include(op->values[i]);
        }
        for (size_t i = 0; i < op->args.size(); i++) {
            include(op->args[i]);
        }
        provide_names.insert(op->name);
    }
public:
    set<string> provide_names;
};

class RemoveAtomicMutexLocks : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Atomic *op) override {
        // Collect the names of all provide nodes inside.
        CollectProvideNames collector;
        op->body.accept(&collector);
        // Search for let bindings that access the providers.
        FindAtomicLetBindings finder(collector.provide_names);
        op->body.accept(&finder);
        if (finder.found) {
            // Can't remove mutex lock. Leave the Stmt as is.
            return IRMutator::visit(op);
        } else {
            remove_mutex_lock_names.insert(op->mutex_name);
            Stmt body = mutate(op->body);
            return Atomic::make("", {}, std::move(body));
        }
    }

public:
    std::set<string> remove_mutex_lock_names;
};

class RemoveAtomicMutexAllocation : public IRMutator {
public:
    using IRMutator::visit;

    RemoveAtomicMutexAllocation(const std::set<string> &remove_mutex_lock_names)
        : remove_mutex_lock_names(remove_mutex_lock_names) {}

    Stmt visit(const Allocate *op) override {
        if (remove_mutex_lock_names.find(op->name) != remove_mutex_lock_names.end()) {
            // The mutex allocation's body is always a Block.
            const Block *block = op->body.as<Block>();
            internal_assert(block != nullptr) <<
                "This is a mutex lock allocation, where the body is expected to be a Block.";
            // The body Block always start with an Evaluate.
            const Evaluate *eval = block->first.as<Evaluate>();
            internal_assert(eval != nullptr) <<
                "This is a mutex lock allocation, where the body Block's first statement is expected to be an Evaluate.";
            // The Evaluate node always contains a Call to memset for initializing the mutexes.
            const Call *call = eval->value.as<Call>();
            internal_assert(call->name == "memset") <<
                "This is a mutex lock allocation, where there should be a call to memset to initialize the locks.";
            return mutate(block->rest);
        } else {
            return IRMutator::visit(op);
        }
    }

    std::set<string> remove_mutex_lock_names;
};

}  // namespace

Stmt remove_atomic_mutex_locks(Stmt s) {
    RemoveAtomicMutexLocks mutator;
    s = mutator.mutate(s);
    if (mutator.remove_mutex_lock_names.size() > 0) {
        s = RemoveAtomicMutexAllocation(mutator.remove_mutex_lock_names).mutate(s);
    }
    return s;
}

}  // namespace Internal
}  // namespace Halide
