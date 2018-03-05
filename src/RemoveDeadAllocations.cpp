#include "RemoveDeadAllocations.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

class RemoveDeadAllocations : public IRMutator2 {
    using IRMutator2::visit;

    Scope<int> allocs;

    Expr visit(const Call *op) override {
        if (op->is_extern()) {
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

        return IRMutator2::visit(op);
    }

    Expr visit(const Load *op) override {
        if (allocs.contains(op->name)) {
            allocs.pop(op->name);
        }

        return IRMutator2::visit(op);
    }

    Stmt visit(const Store *op) override {
        if (allocs.contains(op->name)) {
            allocs.pop(op->name);
        }

        return IRMutator2::visit(op);
    }

    Expr visit(const Variable *op) override {
        if (allocs.contains(op->name)) {
            allocs.pop(op->name);
        }
        return op;
    }

    Stmt visit(const Allocate *op) override {
        allocs.push(op->name, 1);
        Stmt body = mutate(op->body);

        if (allocs.contains(op->name) && op->free_function.empty()) {
            allocs.pop(op->name);
            return body;
        } else if (body.same_as(op->body)) {
            return op;
        } else {
            return Allocate::make(op->name, op->type, op->memory_type, op->extents,
                                  op->condition, body, op->new_expr, op->free_function);
        }
    }

    Stmt visit(const Free *op) override {
        if (allocs.contains(op->name)) {
            // We have reached a Free Stmt without ever using this buffer, do nothing.
            return Evaluate::make(0);
        } else {
            return op;
        }
    }
};

Stmt remove_dead_allocations(Stmt s) {
    return RemoveDeadAllocations().mutate(s);
}


}
}
