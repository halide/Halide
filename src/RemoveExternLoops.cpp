#include "RemoveExternLoops.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

class RemoveExternLoops : public IRMutator {
private:
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (op->for_type != ForType::Extern) {
            return IRMutator::visit(op);
        }
        // Replace the for with its first iteration (implemented with a let).
        return LetStmt::make(op->name, op->min, mutate(op->body));
    }
};

Stmt remove_extern_loops(const Stmt &s) {
    return RemoveExternLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
