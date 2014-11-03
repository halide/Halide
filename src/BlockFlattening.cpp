#include "Deinterleave.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

class BlockFlattener : public IRMutator {
private:

    using IRMutator::visit;

    void visit(const Block *op) {
        /* First we dig into the block traversing down the 'first'
         * stmt until we find one that is not a block. We push all of
         * the rest stmt's into the 'rest' stmt of the top-level
         * block, and then fix up the 'rest' stmt recursively at the
         * end. The result of this mutation is an equivalent Block
         * node that does not contain any Block nodes in a 'first' stmt.
         */
        Stmt first = op->first;
        Stmt rest  = op->rest;
        while(const Block *first_block = first.as<Block>()) {
            first = first_block->first;
            if (first_block->rest.defined()) {
                rest = rest.defined()? Block::make(first_block->rest, rest): first_block->rest;
            }
        }

        if (first.same_as(op->first)) {
            rest = mutate(rest);
            stmt = rest.same_as(op->rest)? op: Block::make(first, rest);
        } else {
            stmt = Block::make(first, mutate(rest));
        }
    }
};

Stmt flatten_blocks(Stmt s) {
    BlockFlattener flatten;
    return flatten.mutate(s);
}

}
}
