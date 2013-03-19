#include "RemoveTrivialForLoops.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

class RemoveTrivialForLoops : public IRMutator {
    using IRMutator::visit;

    void visit(const For *for_loop) {
        Stmt body = mutate(for_loop->body);
        const IntImm *extent = for_loop->extent.as<IntImm>();
        if (extent && extent->value == 1) {
            stmt = new LetStmt(for_loop->name, for_loop->min, body);
        } else if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = new For(for_loop->name, for_loop->min, for_loop->extent, for_loop->for_type, body);
        }
    }
};

// Turn for loops of size one into let statements
Stmt remove_trivial_for_loops(Stmt s) {
    return RemoveTrivialForLoops().mutate(s);
}


}
}
