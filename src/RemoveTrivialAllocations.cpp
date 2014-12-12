#include "RemoveTrivialAllocations.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

class FindTrivialAllocations : public IRVisitor {
public:
    Scope<int> allocs;

private:
    using IRVisitor::visit;

    void visit(const Allocate *op) {
        allocs.push(op->name, 1);
        op->body.accept(this);
    }

};

class RemoveTrivialAllocations : public IRMutator {
    using IRMutator::visit;

    Scope<int> allocs;

    void visit(const Call *op) {
        if (op->call_type == Call::Extern) {
            for (size_t i = 0; i < op->args.size(); i++) {
                const Variable *var = op->args[i].as<Variable>();
                if (var && ends_with(var->name, ".buffer")) {
                    std::string func = var->name.substr(0, var->name.find_first_of('.'));
                    if (allocs.contains(func)) {
                        allocs.pop(func);
                    }
                }
            }
        }

        IRMutator::visit(op);
    }

    void visit(const Load *op) {
        if (allocs.contains(op->name)) {
            allocs.pop(op->name);
        }

        IRMutator::visit(op);
    }

    void visit(const Store *op) {
        if (allocs.contains(op->name)) {
            allocs.pop(op->name);
        }

        IRMutator::visit(op);
    }

    void visit(const Allocate *op) {
        allocs.push(op->name, 1);
        Stmt body = mutate(op->body);

        if (allocs.contains(op->name)) {
            stmt = op->body;
            allocs.pop(op->name);
        } else if (!body.same_as(op->body)) {
            stmt = Allocate::make(op->name, op->type, op->extents, op->condition, body);
        } else {
            stmt = op;
        }
    }
};

// Turn for loops of size one into let statements
Stmt remove_trivial_allocations(Stmt s) {
    return RemoveTrivialAllocations().mutate(s);
}


}
}
