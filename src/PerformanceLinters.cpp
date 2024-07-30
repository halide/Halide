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

        CheckForStridedStores c;
        s.accept(&c);
        return s;
    };
};

class CheckForSaturatingCasts : public CustomPass {
    std::string name() override {
        return "check for strided stores";
    }

    Stmt run(const std::vector<Function> &outputs,
             const std::map<std::string, Function> &env,
             const Stmt &s,
             const Target &target) override {

        // Suggest cast(type, clamp(e, type.min(), type.max())) -> saturating_cast(type, e)

        class CheckForSaturatingCasts : public IRVisitor {
            using IRVisitor::visit;

            void visit(const Cast *op) override {
                Type cast_type = op->type;
                if (cast_type.is_int_or_uint()) {
                    // A call to clamp() is stored in IR as max(min(e, maxval), minval)
                    if (const Max *mx = op->value.as<Max>()) {
                        if (const Min *mn = mx->a.as<Min>()) {
                            Expr e = mn->a;
                            Expr minval = mx->b;
                            Expr maxval = mn->b;
                            if (cast_type.is_int()) {
                                const int64_t *i_min = as_const_int(minval);
                                const int64_t *i_max = as_const_int(maxval);
                                if (i_min && *i_min == *as_const_int(cast_type.min()) &&
                                    i_max && *i_max == *as_const_int(cast_type.max())) {
                                    user_warning
                                        << "Expressions of the form cast(type, clamp(e, min, max)) "
                                        << "should be replaced with a saturating_cast() call when min "
                                        << "and max are the natural bounds of the type.";

                                }
                            } else {
                                internal_assert(cast_type.is_uint());
                                const uint64_t *u_min = as_const_uint(minval);
                                const uint64_t *u_max = as_const_uint(maxval);
                                if (u_min && *u_min == *as_const_uint(cast_type.min()) &&
                                    u_max && *u_max == *as_const_uint(cast_type.max())) {
                                    user_warning
                                        << "Expressions of the form cast(type, clamp(e, min, max)) "
                                        << "should be replaced with a saturating_cast() call when min "
                                        << "and max are the natural bounds of the type.";

                                }
                            }
                        }
                    }
                }
                IRVisitor::visit(op);
            }
        };

        CheckForSaturatingCasts c;
        s.accept(&c);
        return s;
    };
};
// Add more linters here

std::vector<std::unique_ptr<CustomPass>> get_default_linters(const Target &t) {
    std::vector<std::unique_ptr<CustomPass>> passes;
    passes.emplace_back(new CheckForStridedStores{});
    passes.emplace_back(new CheckForSaturatingCasts{});
    return passes;
}

}  // namespace Internal
}  // namespace Halide
