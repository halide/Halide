#include "UnrollLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

using std::vector;

namespace Halide {
namespace Internal {

class UnrollLoops : public IRMutator2 {
    using IRMutator2::visit;

    Stmt visit(const For *for_loop) override {
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

            vector<Stmt> iters;
            // Make n copies of the body, each wrapped in a let that defines the loop var for that body
            for (int i = 0; i < e->value; i++) {
                iters.push_back(substitute(for_loop->name, for_loop->min + i, body));
            }
            return Block::make(iters);

        } else {
            return IRMutator2::visit(for_loop);
        }
    }
};

Stmt unroll_loops(Stmt s) {
    return UnrollLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
