#include "RemoveExternLoops.h"
#include "Bounds.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::vector;

class RemoveExternLoops : public IRMutator2 {
public:
    Expr predicate;
private:
    using IRMutator2::visit;

    Stmt visit(const For *op) override {
        if (op->for_type != ForType::Extern) {
            return IRMutator2::visit(op);
        }
        Stmt body = mutate(op->body);
        Expr min = op->min;
        Expr extent = op->extent;
        body = LetStmt::make(op->name + ".min", min, body);
        body = LetStmt::make(op->name + ".max", (min + extent) - 1, body);
        return body;
    }
};

Stmt remove_extern_loops(Stmt s) {
    return RemoveExternLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
