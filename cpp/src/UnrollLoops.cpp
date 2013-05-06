#include "UnrollLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

class UnrollLoops : public IRMutator {
    using IRMutator::visit;

    void visit(const For *for_loop) {
        if (for_loop->for_type == For::Unrolled) {
            const IntImm *extent = for_loop->extent.as<IntImm>();
            assert(extent && "Can only unroll for loops over a constant extent");
            Stmt body = mutate(for_loop->body);
                
            Block *block = NULL;
            // Make n copies of the body, each wrapped in a let that defines the loop var for that body
            for (int i = extent->value-1; i >= 0; i--) {
                Stmt iter = substitute(for_loop->name, for_loop->min + i, body);
                block = new Block(iter, block);
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
