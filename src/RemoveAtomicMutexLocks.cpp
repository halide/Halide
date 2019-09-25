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

    Stmt visit(const Allocate *op) override {
        if (remove_mutex_lock_name != "" &&
                op->name == remove_mutex_lock_name) {
            // The mutex allocation's body is always a Block
            const Block *block = op->body.as<Block>();
            internal_assert(block != nullptr) <<
                "This is a mutex lock allocation, where the body is expected to be a Block.";
            const Call *call = block->first.as<Call>();
            internal_assert(call != nullptr) <<
                "This is a mutex lock allocation, where the body Block's first statement is expected to be a Call to memset.";
            internal_assert(call->name == "memset");
            return mutate(block->rest);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Atomic *op) override {
        // Search for atomic tuple Provide nodes
        FindAtomicTupleProvide finder;
        op->body.accept(&finder);
        if (finder.found) {
            // Can't remove mutex lock. Leave the Stmt as is.
            return IRMutator::visit(op);
        } else {
            remove_mutex_lock_name = op->mutex_name;
            Stmt body = mutate(op->body);
            remove_mutex_lock_name = "";
            return Atomic::make("", {}, std::move(body));
        }
    }

    string remove_mutex_lock_name;
};

}  // namespace

Stmt remove_atomic_mutex_locks(Stmt s) {
    s = RemoveAtomicMutexLocks().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
