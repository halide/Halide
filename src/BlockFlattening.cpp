#include "Deinterleave.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

class BlockFlattener : public IRMutator {
private:

    using IRMutator::visit;

    void visit(const Block *op) {
        Stmt first = op->first;
        Stmt rest  = op->rest;
        while(const Block *first_block = op->first.as<Block>()) {
            first = mutate(first_block->first);
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
