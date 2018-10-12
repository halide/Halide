#include "RemoveExternLoops.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::vector;

class RemoveExternLoops : public IRMutator2 {
private:
    using IRMutator2::visit;

    Stmt visit(const For *op) override {
        if (op->for_type != ForType::Extern) {
            return IRMutator2::visit(op);
        }
        // Replace the for with its first iteration (implemented with a let).
        return LetStmt::make(op->name, op->min, mutate(op->body));
    }
};

Stmt remove_extern_loops(Stmt s) {
    return RemoveExternLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
