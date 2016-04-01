#include "RemoveDeadAllocations.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

class RemoveDeadAllocations : public IRMutator {
    using IRMutator::visit;

    Scope<int> allocs;

    void visit(const Call *op) {
        if (op->call_type == Call::Extern ||
            op->call_type == Call::ExternCPlusPlus) {
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
            stmt = body;
            allocs.pop(op->name);
        } else if (body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = Allocate::make(op->name, op->type, op->extents, op->condition, body, op->new_expr, op->free_function);
        }
    }

    void visit(const Free *op) {
        if (allocs.contains(op->name)) {
            // We have reached a Free Stmt without ever using this buffer, do nothing.
            stmt = Evaluate::make(0);
        } else {
            stmt = op;
        }
    }
};

Stmt remove_dead_allocations(Stmt s) {
    return RemoveDeadAllocations().mutate(s);
}


}
}
