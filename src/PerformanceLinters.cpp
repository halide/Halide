#include "PerformanceLinters.h"
#include "IR.h"
#include "IRMutator.h"
#include "IRVisitor.h"
#include "Pipeline.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

class CheckForStridedStores : public CustomPass {
    std::string name() override {
        return "check for strided stores";
    }

    Stmt run(const std::vector<Function> &outputs,
             const std::map<std::string, Function> &env,
             const Stmt &s,
             const Target &target) override {

        // Check for strided stores to internal allocations. These can exist in
        // user code, but after lowering they should have been converted into
        // dense stores of shuffles.

        class CheckForStridedStores : public IRVisitor {
            using IRVisitor::visit;

            Scope<> internal_allocs;

            void visit(const Allocate *op) override {
                ScopedBinding<> bind(internal_allocs, op->name);
                IRVisitor::visit(op);
            }

            void visit(const Store *op) override {
                if (internal_allocs.contains(op->name)) {
                    if (const Ramp *r = op->index.as<Ramp>()) {
                        if (!is_const_one(r->stride)) {
                            user_warning
                                << "Vector store to Func " << op->name
                                << " has strided index with stride " << r->stride << ". "
                                << "Considering vectorizing across the innermost storage "
                                << "dimension instead, or if it is too small, unrolling "
                                << "the loop over the innermost storage dimension\n";
                        }
                    }
                }
            }
        };

        return s;
    };
};

// Add more linters here

std::vector<std::unique_ptr<CustomPass>> get_default_linters(const Target &t) {
    std::vector<std::unique_ptr<CustomPass>> passes;
    passes.emplace_back(new CheckForStridedStores{});
    return passes;
}

}  // namespace Internal
}  // namespace Halide
