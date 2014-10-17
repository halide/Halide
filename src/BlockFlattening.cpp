#include "Deinterleave.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

class BlockFlattener : public IRMutator {
private:

    using IRMutator::visit;

    void visit(const Block *op) {
        const Block *first_block = op->first.as<Block>();

        if (first_block) {
            Stmt first = mutate(first_block->first);
            Stmt rest  = Block::make(mutate(first_block->rest), op->rest);
            stmt = Block::make(first, mutate(rest));
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt flatten_blocks(Stmt s) {
    BlockFlattener flatten;
    return flatten.mutate(s);
}

}
}
