#include "UnrollLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

class UnrollLoops : public IRMutator {
    using IRMutator::visit;

    void visit(const For *for_loop) {
        if (for_loop->for_type == ForType::Unrolled) {
            // Give it one last chance to simplify to an int
            Expr extent = simplify(for_loop->extent);
            const IntImm *e = extent.as<IntImm>();
            user_assert(e)
                << "Can only unroll for loops over a constant extent.\n"
                << "Loop over " << for_loop->name << " has extent " << extent << ".\n";
            Stmt body = mutate(for_loop->body);

            if (e->value == 1) {
                user_warning << "Warning: Unrolling a for loop of extent 1: " << for_loop->name << "\n";
            }

            Stmt block;
            // Make n copies of the body, each wrapped in a let that defines the loop var for that body
            for (int i = e->value-1; i >= 0; i--) {
                Stmt iter = substitute(for_loop->name, for_loop->min + i, body);
                block = Block::make(iter, block);
            }
            stmt = block;

        } else {
            IRMutator::visit(for_loop);
        }
    }
};

Stmt unroll_loops(Stmt s) {
    return UnrollLoops().mutate(s);
}

}
}
