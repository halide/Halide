#include "ImplementWithMatcher.h"

#include <map>
#include <string>
#include <vector>

#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "IRVisitor.h"
#include "Lower.h"
#include "Pipeline.h"
#include "RealizationOrder.h"
#include "SimplifySpecializations.h"
#include "StrictifyFloat.h"
#include "Target.h"
#include "TargetQueryOps.h"
#include "WrapCalls.h"

namespace Halide {
namespace Internal {

Stmt lower_spec_to_canonical_form(const Pipeline &spec, const Target &t) {
    // Extract the Internal::Function handles from the spec's outputs.
    std::vector<Function> output_funcs;
    for (const Func &f : spec.outputs()) {
        Function fn = f.function();
        internal_assert(fn.is_spec_pattern())
            << "lower_spec_to_canonical_form expects every output of the "
               "spec Pipeline to be a spec-pattern Func (one produced by "
               "Instruction::spec()); got output '"
            << fn.name() << "' which is not marked spec-pattern.";
        output_funcs.push_back(fn);
    }

    // Mirror lower_impl's pre-canonical-form setup. Two intentional
    // omissions compared to lower_impl:
    //   - apply_implement_with_directives: specs do not themselves carry
    //     implement_with directives, so there is nothing to apply.
    //   - any-strict-float flag plumbing into a Module: we never produce
    //     a Module here, so the bool return of strictify_float is dropped.
    auto [outputs, env] =
        deep_copy(output_funcs, build_environment(output_funcs));

    lower_target_query_ops(env, t);
    (void)strictify_float(env, t);

    for (auto &iter : env) {
        iter.second.lock_loop_levels();
    }

    env = wrap_func_calls(env);

    auto [order, fused_groups] = realization_order(outputs, env);

    simplify_specializations(env);

    return lower_to_canonical_form(outputs, env, order, fused_groups, t,
                                   /*requirements=*/{},
                                   /*pipeline_name=*/"implement_with_spec",
                                   /*trace_pipeline=*/false);
}

namespace {

// IRVisitor that records the first For node whose name matches the
// target. The canonical-form prefix runs uniquify_variable_names, so a
// given For name appears at most once in the Stmt.
class FindForByName : public IRVisitor {
public:
    Stmt found;
    const std::string &target_name;

    explicit FindForByName(const std::string &name) : target_name(name) {
    }

    using IRVisitor::visit;

    void visit(const For *op) override {
        if (op->name == target_name) {
            found = Stmt(op);
            return;
        }
        IRVisitor::visit(op);
    }
};

}  // namespace

Stmt find_implement_with_loop(const Stmt &s,
                              const std::string &user_func_name,
                              int stage_index,
                              const std::string &loop_var_name) {
    std::string target = user_func_name + ".s" +
                         std::to_string(stage_index) + "." +
                         loop_var_name;
    FindForByName v(target);
    s.accept(&v);
    return v.found;
}

}  // namespace Internal
}  // namespace Halide
