#include "RemoveAtomicMutexLocks.h"
#include "IRVisitor.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

class FindAtomicTupleProvide : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (current_provide_name != "" &&
                op->name == current_provide_name + ".value") {
            found = true;
        }
    }

    void visit(const Provide *op) override {
        for (size_t i = 0; i < op->values.size(); i++) {
            current_provide_name = op->name;
            op->values[i].accept(this);
            current_provide_name = "";
        }
        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i].accept(this);
        }
    }

    string current_provide_name;
public:
    bool found = false;
};

class RemoveAtomicMutexLocks : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Atomic *op) override {
        // Search for atomic tuple Provide nodes
        FindAtomicTupleProvide finder;
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
            // The mutex allocation's body is always a Block
            const Block *block = op->body.as<Block>();
            internal_assert(block != nullptr) <<
                "This is a mutex lock allocation, where the body is expected to be a Block.";
            const Evaluate *eval = block->first.as<Evaluate>();
            internal_assert(eval != nullptr) <<
                "This is a mutex lock allocation, where the body Block's first statement is expected to be an Evaluate.";
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
