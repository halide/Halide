#include "RemoveTrivialForLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "CodeGen_GPU_Dev.h"

namespace Halide {
namespace Internal {

class RemoveTrivialForLoops : public IRMutator {
    using IRMutator::visit;

    void visit(const For *for_loop) {
        Stmt body = mutate(for_loop->body);
        if (is_one(for_loop->extent) && !CodeGen_GPU_Dev::is_gpu_var(for_loop->name)) {
            if (for_loop->for_type == For::Parallel) {
                std::cerr << "Warning: Parallel for loop over "
                          << for_loop->name << " has extent one. "
                          << "Can't do one piece of work in parallel.\n";
            } else if (for_loop->for_type == For::Vectorized) {
                std::cerr << "Warning: Vectorized for loop over "
                          << for_loop->name << " has extent one. "
                          << "Not vectorizing.\n";
            }
            stmt = LetStmt::make(for_loop->name, for_loop->min, body);
        } else if (is_zero(for_loop->extent)) {
            stmt = Evaluate::make(0);
        } else if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = For::make(for_loop->name, for_loop->min, for_loop->extent, for_loop->for_type, for_loop->device_api, body);
        }
    }
};

// Turn for loops of size one into let statements
Stmt remove_trivial_for_loops(Stmt s) {
    return RemoveTrivialForLoops().mutate(s);
}


}
}
