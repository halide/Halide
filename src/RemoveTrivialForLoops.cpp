#include "RemoveTrivialForLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "CodeGen_GPU_Dev.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

class RemoveTrivialForLoops : public IRMutator2 {
    using IRMutator2::visit;

    Stmt visit(const For *for_loop) override {

        Stmt body = mutate(for_loop->body);

        if (is_one(for_loop->extent) &&
            for_loop->device_api != DeviceAPI::None) {
            // Don't assume any device API loops are trivial, but we
            // can substitute in the min value if the loop is size 1.
            body = substitute(for_loop->name, for_loop->min, body);
            return For::make(for_loop->name, for_loop->min, for_loop->extent,
                             for_loop->for_type, for_loop->device_api, body);
        } else if (is_one(for_loop->extent)) {
            if (for_loop->for_type == ForType::Parallel) {
                std::cerr << "Warning: Parallel for loop over "
                          << for_loop->name << " has extent one. "
                          << "Can't do one piece of work in parallel.\n";
            } else if (for_loop->for_type == ForType::Vectorized) {
                std::cerr << "Warning: Vectorized for loop over "
                          << for_loop->name << " has extent one. "
                          << "Not vectorizing.\n";
            }
            return LetStmt::make(for_loop->name, for_loop->min, body);
        } else if (is_zero(for_loop->extent)) {
            return Evaluate::make(0);
        } else if (can_prove(for_loop->extent <= 1)) {
            // Loop has at most one iteration
            Stmt stmt = LetStmt::make(for_loop->name, for_loop->min, body);
            return IfThenElse::make(for_loop->extent > 0, stmt, Stmt());
        } else if (body.same_as(for_loop->body)) {
            return for_loop;
        } else {
            return For::make(for_loop->name, for_loop->min, for_loop->extent,
                             for_loop->for_type, for_loop->device_api, body);
        }
    }
};

// Turn for loops of size one into let statements
Stmt remove_trivial_for_loops(Stmt s) {
    return RemoveTrivialForLoops().mutate(s);
}

}
}
