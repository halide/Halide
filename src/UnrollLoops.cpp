#include "UnrollLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"
#include "UniquifyVariableNames.h"

namespace Halide {
namespace Internal {

namespace {

class UnrollLoops : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *for_loop) override {
        if (for_loop->for_type == ForType::Unrolled) {
            Stmt body = for_loop->body;
            const IntImm *e = for_loop->extent.as<IntImm>();

            internal_assert(e)
                << "Loop over " << for_loop->name << " should have had a constant extent\n";
            body = mutate(body);

            if (e->value == 1) {
                user_warning << "Warning: Unrolling a for loop of extent 1: " << for_loop->name << "\n";
            }

            Stmt iters;
            for (int i = e->value - 1; i >= 0; i--) {
                Stmt iter = substitute(for_loop->name, for_loop->min + i, body);
                // It's necessary to eagerly simplify this iteration
                // here to resolve things like muxes down to a single
                // item before we go and make N copies of something of
                // size N.
                iter = simplify(iter);
                if (!iters.defined()) {
                    iters = iter;
                } else {
                    iters = Block::make(iter, iters);
                }
            }

            return iters;

        } else {
            return IRMutator::visit(for_loop);
        }
    }
};

}  // namespace

Stmt unroll_loops(const Stmt &s) {
    Stmt stmt = UnrollLoops().mutate(s);
    // Unrolling duplicates variable names. Other passes assume variable names are unique.
    return uniquify_variable_names(stmt);
}

}  // namespace Internal
}  // namespace Halide
