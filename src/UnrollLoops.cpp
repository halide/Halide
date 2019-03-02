#include "UnrollLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

using std::vector;

namespace Halide {
namespace Internal {

class UnrollLoops : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *for_loop) override {
        if (for_loop->for_type == ForType::Unrolled) {
            // Give it one last chance to simplify to an int
            Expr extent = simplify(for_loop->extent);
            const IntImm *e = extent.as<IntImm>();

            if (e == nullptr && permit_failed_unroll) {
                // Not constant. Just rewrite to serial.
                Stmt body = mutate(for_loop->body);
                return For::make(for_loop->name, for_loop->min, for_loop->extent,
                                 ForType::Serial, for_loop->device_api, std::move(body));
            }

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
            return IRMutator::visit(for_loop);
        }
    }
    bool permit_failed_unroll = false;
public:
    UnrollLoops() {
        // Experimental autoschedulers may want to unroll without
        // being totally confident the loop will indeed turn out
        // to be constant-sized. If this feature continues to be
        // important, we need to expose it in the scheduling
        // language somewhere, but how? For now we do something
        // ugly and expedient.

        // For the tracking issue to fix this, see
        // https://github.com/halide/Halide/issues/3479
        permit_failed_unroll = get_env_variable("HL_PERMIT_FAILED_UNROLL") == "1";
    }
};

Stmt unroll_loops(Stmt s) {
    return UnrollLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
