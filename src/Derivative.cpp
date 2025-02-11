#include <cmath>
#include <iostream>
#include <set>

#include "Associativity.h"
#include "BoundaryConditions.h"
#include "CSE.h"
#include "Debug.h"
#include "Derivative.h"
#include "DerivativeUtils.h"
#include "Error.h"
#include "ExprUsesVar.h"
#include "FindCalls.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "RealizationOrder.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"

namespace Halide {

using std::map;
using std::set;
using std::string;
using std::vector;
using FuncKey = Derivative::FuncKey;

namespace Internal {
namespace {

bool is_float_extern(const string &op_name,
                     const string &func_name) {
    return op_name == (func_name + "_f16") ||
           op_name == (func_name + "_f32") ||
           op_name == (func_name + "_f64");
}

bool is_math_func(const Call *call,
                  const string &func_name,
                  Call::IntrinsicOp intrinsic_op = Call::IntrinsicOp::IntrinsicOpCount) {
    if (call->is_extern()) {
        const string &op_name = call->name;
        return op_name == (func_name + "_f16") ||
               op_name == (func_name + "_f32") ||
               op_name == (func_name + "_f64");
    } else if (call->is_intrinsic() && intrinsic_op != Call::IntrinsicOpCount) {
        return call->is_intrinsic(intrinsic_op);
    } else {
        return false;
    }
}

/** Compute derivatives through reverse accumulation
 */
class ReverseAccumulationVisitor : public IRVisitor {
public:
    void propagate_adjoints(const Func &output,
                            const Func &adjoint,
                            const Region &output_bounds);

    map<FuncKey, Func> get_adjoint_funcs() const {
        return adjoint_funcs;
    }

protected:
    void visit(const IntImm *) override;
    void visit(const UIntImm *) override;
    void visit(const FloatImm *) override;
    void visit(const StringImm *) override;
    void visit(const Cast *op) override;
    void visit(const Reinterpret *op) override;
    void visit(const Variable *op) override;
    void visit(const Add *op) override;
    void visit(const Sub *op) override;
    void visit(const Mul *op) override;
    void visit(const Div *op) override;
    void visit(const Mod *op) override;
    void visit(const Min *op) override;
    void visit(const Max *op) override;
    void visit(const EQ *op) override;
    void visit(const NE *op) override;
    void visit(const LT *op) override;
    void visit(const LE *op) override;
    void visit(const GT *op) override;
    void visit(const GE *op) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Select *op) override;
    void visit(const Let *op) override;
    void visit(const Call *op) override;
    void visit(const Load *op) override {
        internal_error << "Encounter unexpected expression \"Load\" when differentiating.";
    }
    void visit(const Ramp *op) override {
        internal_error << "Encounter unexpected expression \"Ramp\" when differentiating.";
    }
    void visit(const Broadcast *op) override {
        internal_error << "Encounter unexpected expression \"Broadcast\" when differentiating.";
    }
    void visit(const Shuffle *op) override {
        internal_error << "Encounter unexpected expression \"Shuffle\" when differentiating.";
    }
    void visit(const VectorReduce *op) override {
        internal_error << "Encounter unexpected expression \"VectorReduce\" when differentiating.";
    }
    void visit(const LetStmt *op) override {
        internal_error << "Encounter unexpected statement \"LetStmt\" when differentiating.";
    }
    void visit(const AssertStmt *op) override {
        internal_error << "Encounter unexpected statement \"AssertStmt\" when differentiating.";
    }
    void visit(const ProducerConsumer *op) override {
        internal_error << "Encounter unexpected statement \"ProducerConsumer\" when differentiating.";
    }
    void visit(const For *op) override {
        internal_error << "Encounter unexpected statement \"For\" when differentiating.";
    }
    void visit(const Store *op) override {
        internal_error << "Encounter unexpected statement \"Store\" when differentiating.";
    }
    void visit(const Provide *op) override {
        internal_error << "Encounter unexpected statement \"Provide\" when differentiating.";
    }
    void visit(const Allocate *op) override {
        internal_error << "Encounter unexpected statement \"Allocate\" when differentiating.";
    }
    void visit(const Free *op) override {
        internal_error << "Encounter unexpected statement \"Free\" when differentiating.";
    }
    void visit(const Realize *op) override {
        internal_error << "Encounter unexpected statement \"Realize\" when differentiating.";
    }
    void visit(const Block *op) override {
        internal_error << "Encounter unexpected statement \"Block\" when differentiating.";
    }
    void visit(const IfThenElse *op) override {
        internal_error << "Encounter unexpected statement \"IfThenElse\" when differentiating.";
    }
    void visit(const Evaluate *op) override {
        internal_error << "Encounter unexpected statement \"Evaluate\" when differentiating.";
    }
    void visit(const Prefetch *op) override {
        internal_error << "Encounter unexpected statement \"Prefetch\" when differentiating.";
    }
    void visit(const Fork *op) override {
        internal_error << "Encounter unexpected statement \"Fork\" when differentiating.";
    }
    void visit(const Acquire *op) override {
        internal_error << "Encounter unexpected statement \"Acquire\" when differentiating.";
    }
    void visit(const Atomic *op) override {
        internal_error << "Encounter unexpected statement \"Atomic\" when differentiating.";
    }
    void visit(const HoistedStorage *op) override {
        internal_error << "Encounter unexpected statement \"HoistedStorage\" when differentiating.";
    }

private:
    void accumulate(const Expr &stub, Expr adjoint);

    void propagate_halide_function_call(
        Expr adjoint,
        const std::string &name,             // called function name
        const FunctionPtr &func_ptr,         // pointer to halide function, is null if this is a call to buffer or param
        const std::vector<Expr> &call_args,  // call arguments
        int value_index,                     // which element in the tuple
        const Type &type                     // return type of the called function
    );

    // For each expression, we store the accumulated adjoints expression
    map<const BaseExprNode *, Expr> expr_adjoints;
    // For each function and each update, we store the accumulated adjoints func
    map<FuncKey, Func> adjoint_funcs;
    // Let variables and their mapping
    map<string, Expr> let_var_mapping;
    vector<string> let_variables;
    // Bounds of functions
    map<string, Box> func_bounds;
    // Current function that scatters its adjoints to its dependencies
    Func current_func;
    // Current update of the function
    int current_update_id;
    // We compute the derivatives in several passes.
    // Sometimes we don't want to propagate through Halide function calls
    bool is_forward_overwrite_detection_phase;
    bool is_self_referencing_phase;
    // Is the current function update a non overwriting scan?
    bool is_current_non_overwriting_scan;
    // A temporary flag for checking the derivatives
    // to self reference of a Halide function is 1 or not
    // Used in forward overwrite detection phase
    Tuple self_reference_adjoint = Tuple(Expr());
    vector<vector<Expr>> self_reference_args;
};

void ReverseAccumulationVisitor::propagate_adjoints(
    const Func &output,
    const Func &adjoint,
    const Region &output_bounds) {
    // Topologically sort the functions
    map<string, Function> env = find_transitive_calls(output.function());
    vector<string> order =
        realization_order({output.function()}, env).first;
    vector<Func> funcs;
    funcs.reserve(order.size());
    // Internal::debug(0) << "Sorted Func list:\n";
    // for (const auto &func_name : order) {
    //     Internal::debug(0) << "  . " << func_name << "\n";
    // }
    for (const auto &func_name : order) {
        funcs.emplace_back(env[func_name]);
    }
    internal_assert(!funcs.empty());

    // If the derivatives depend on an in-place overwrite,
    // and the self reference adjoint is not 0 or 1,
    // throws an error to the users.
    // For example:
    //
    // 1.
    // f(x) = g(x)
    // f(x) = f(x) * f(x)
    // f'(x) depends on first f(x)
    //
    // 2.
    // f(x) = 0
    // f(x) = 2 * f(x) + g(r.x)
    // g'(r.x) depends on intermediate f'(x)
    //
    // The following is fine because the self reference adjoint is 1:
    // f(x) = f(x) + g(r.x)
    // (when it's 1 all instances of f(x) have the same adjoint)
    //
    // The issue is that the self reference to f makes propagation to g
    // using the wrong adjoints.
    //
    // The user should rewrite the above updates to the following:
    //
    // 1.
    // f_(x, 0) = g(x)
    // f_(x, 1) = f_(x, 0) * f_(x, 0)
    // f(x) = f_(x, 1)
    //
    // 2.
    // f_(x, 0) = 0
    // f_(x, r.x + 1) = 2 * f_(x, r.x) + g(r.x)
    // f(x) = f_(x, r.x.max() + 1)
    //
    // We can do the rewrite for the users automatically, but it requires
    // generating the indirect reference f_, making scheduling these
    // functions extremely difficult.
    is_forward_overwrite_detection_phase = true;
    set<FuncKey> non_overwriting_scans;
    for (auto &func : funcs) {
        current_func = func;
        // Precompute the left hand side intervals for each update
        // We use this to determine if there's overlaps between the updates
        vector<Box> boxes;
        boxes.reserve(func.num_update_definitions());
        for (int update_id = 0;
             update_id < func.num_update_definitions(); update_id++) {
            const vector<Expr> &args = func.update_args(update_id);
            vector<Interval> intervals;
            intervals.reserve(args.size());
            for (const auto &arg : args) {
                Scope<Interval> scope;
                ReductionDomain rdom = extract_rdom(arg);
                if (rdom.defined()) {
                    const vector<ReductionVariable> &rvars = rdom.domain();
                    for (const auto &r : rvars) {
                        Expr r_max = simplify(r.min + r.extent + 1);
                        scope.push(r.var, Interval(r.min, r_max));
                    }
                }
                Interval interval = bounds_of_expr_in_scope(arg, scope);
                intervals.push_back(interval);
            }
            boxes.emplace_back(intervals);
        }
        for (int update_id = 0;
             update_id < func.num_update_definitions(); update_id++) {
            // We check for two criteria:
            // 1. We check if the derivatives
            //    depend on previous update, and if that particular
            //    value has been overwritten.
            // 2. For updates of f with reduction variables,
            //    unless the derivatives to self reference is 1 or 0,
            //    we make sure overwritten f' is not used by others.
            //    We conservatively detect this by distinguish two cases:
            //    a. If f' is always never being overwritten for all instances of
            //       the reduction variables
            //    b. Or if f' is never used by others except itself.
            //
            // A few examples:
            //
            // f(x) = f(x) + g(r.x) // good, the self update derivative is 1
            //
            // f(x) = 2 * f(x) // good, although the self update derivative is 2,
            //                    there's no reduction variables
            //
            // f(x) = 2 * f(x) + g(r.x) // bad, f'(x) will be used for updating
            //                             g(r.x) but will be overwritten
            //
            // f(x) = f(x) * f(x) // bad, derivative of f(x) depends on previous value
            //                       which has been overwritten
            //
            // f(x, 0) = ...
            // f(x, 1) = f(x, 0) * f(x, 0) // good, although the derivative depends on
            //                          // previous value, the updates do not overlap
            //
            // f(x, r.x + 1) = 2 * f(x, r.x) + g(r.x) // good,
            //                                      // f' is never overwritten
            //
            // f(x, y) = g(x)
            // f(x, r.x + 1) = f(x, r.x) * f(x, r.x); // bad, the derivatives
            //                                           depend on previous updates
            //
            // f(x, y, 0) = g(x)
            // f(x, r.x + 1, 1) = f(x, r.x, 0) * f(x, r.x, 0); // good
            //
            // f(x, r.x + 1, r.y + 1) = 2 * f(x, r.x, r.y) + g(r.x) // good
            //
            // f(x, r.x + 1, r.x + r.y + 1) = 2 * f(x, r.x, r.y) + g(r.x) // bad

            vector<Expr> zeros;
            Tuple rhs_tuple = func.values();
            zeros.reserve(rhs_tuple.size());
            for (int i = 0; i < (int)rhs_tuple.size(); i++) {
                zeros.push_back(make_zero(rhs_tuple[i].type()));
            }
            self_reference_adjoint = Tuple(zeros);
            self_reference_args.clear();
            // Checking 1. here:
            // Take the derivative at expression level, the results are
            // stored in expr_adjoints
            vector<Expr> expr_list;
            Tuple update_tuple = func.update_values(update_id);
            vector<const BaseExprNode *> output_exprs;
            const vector<Expr> &update_tuple_vector = update_tuple.as_vector();
            for (const auto &expr : update_tuple_vector) {
                vector<Expr> value_expr_list = sort_expressions(expr);
                expr_list.insert(expr_list.end(),
                                 value_expr_list.begin(), value_expr_list.end());
                output_exprs.push_back((const BaseExprNode *)expr_list.back().get());
            }

            // TODO: replace let_var_mapping with Scope
            // Gather let variables
            let_var_mapping.clear();
            let_variables.clear();
            for (const auto &expr : expr_list) {
                if (const Let *op = expr.as<Let>()) {
                    // Assume Let variables are unique
                    internal_assert(let_var_mapping.find(op->name) == let_var_mapping.end());
                    let_var_mapping[op->name] = op->value;
                    let_variables.push_back(op->name);
                }
            }

            // Set the output adjoint to 1
            // We're not really propagating adjoints, just checking if there's
            // self references
            for (auto &output_expr : output_exprs) {
                expr_adjoints[output_expr] = 1.f;
            }

            for (Expr &e : reverse_view(expr_list)) {
                if (e.type().is_handle()) {
                    // Ignore pointer types
                    continue;
                }
                e.accept(this);
            }

            auto error = [&]() {
                user_error << "Can't take the gradients of " << func.name() << ", which depend on intermediate values. "
                           << "Use a scan (which saves intermediate results) instead.";
            };

            // For each adjoint expression depositing to a function or image,
            // check if it references to the function
            bool adjoints_used_by_others = false;
            for (const auto &it : expr_adjoints) {
                Expr target_expr(it.first);
                bool is_target_func_or_buffer = false;
                const Call *call_op = target_expr.as<Call>();
                if (call_op != nullptr) {
                    is_target_func_or_buffer =
                        call_op->call_type == Call::Image ||
                        call_op->call_type == Call::Halide;
                }
                Expr expr = it.second;
                if (is_target_func_or_buffer &&
                    is_calling_function(func.name(), expr, let_var_mapping)) {
                    // Self reference might not be bad.
                    // If we carefully avoid overwriting intermediate values,
                    // we can still backprop.
                    // First we check for the pure definition.
                    // If the pure definition depends on any functions or buffers,
                    // there is no hope since we will overwrite something
                    Tuple rhs_tuple = func.values();
                    for (int tuple_id = 0; tuple_id < (int)rhs_tuple.size();
                         tuple_id++) {
                        if (is_calling_function(rhs_tuple[tuple_id], let_var_mapping)) {
                            error();
                        }
                    }
                    // Now we check all previous updates, see if the left hand
                    // side arguments overlap.
                    const Box &current_box = boxes[update_id];
                    for (int prev_update_id = 0; prev_update_id < update_id;
                         prev_update_id++) {
                        // Gather two boxes from current update and previous update
                        const Box &prev_box = boxes[prev_update_id];
                        internal_assert(current_box.size() == prev_box.size());
                        // If any of the boxes overlap, we need to throw an error
                        if (boxes_overlap(current_box, prev_box)) {
                            error();
                        }
                    }
                }

                if (is_target_func_or_buffer && call_op->name != func.name()) {
                    adjoints_used_by_others = true;
                }
            }
            expr_adjoints.clear();

            // Checking 2. here:
            bool all_zero_or_one_self_adjoint = true;
            for (int i = 0; i < (int)self_reference_adjoint.size(); i++) {
                if (!is_const(self_reference_adjoint[i], 0) &&
                    !is_const(self_reference_adjoint[i], 1)) {
                    all_zero_or_one_self_adjoint = false;
                    break;
                }
            }
            bool has_reduction_var = !func.rvars(update_id).empty();
            if (!all_zero_or_one_self_adjoint && has_reduction_var) {
                // a. is there any instance of reduction variable such that
                // the self reference update overwrites itself?
                // Or, equivalently, for all possible values of the reduction
                // variables, does the self reference update always
                // reads from/writes to different locations?
                // First we determine the ranges of RDoms for
                // and_condition_over_domain
                Scope<Interval> varying;
                // Loop over lhs & rhs to grab a reduction domain
                ReductionDomain r;
                const vector<Expr> &update_args = func.update_args(update_id);
                for (const Expr &expr : update_args) {
                    r = extract_rdom(expr);
                    if (r.defined()) {
                        break;
                    }
                }
                if (!r.defined()) {
                    for (int tuple_id = 0; tuple_id < (int)update_tuple.size();
                         tuple_id++) {
                        r = extract_rdom(update_tuple[tuple_id]);
                        if (r.defined()) {
                            break;
                        }
                    }
                }
                internal_assert(r.defined());
                // Go over all self reference call arguments
                bool is_not_overwriting = true;
                for (const vector<Expr> &self_ref_args : self_reference_args) {
                    internal_assert(self_ref_args.size() == update_args.size());
                    Expr not_overwriting_cond = const_false();
                    for (int arg_id = 0; arg_id < (int)self_ref_args.size(); arg_id++) {
                        // Are the read from/write to arguments always different?
                        not_overwriting_cond = simplify(not_overwriting_cond ||
                                                        (self_ref_args[arg_id] != update_args[arg_id]));
                    }
                    not_overwriting_cond = and_condition_over_domain(
                        not_overwriting_cond, varying);
                    // Needs to be true for all self reference
                    is_not_overwriting = is_not_overwriting &&
                                         can_prove(not_overwriting_cond);
                }

                // b. Even if the derivative is overwritten, as long as
                // we don't use it in this update we are good.
                // Otherwise we throw an error
                if (!is_not_overwriting && adjoints_used_by_others) {
                    error();
                }

                if (is_not_overwriting) {
                    // This is a non overwriting scan, let's remember it
                    non_overwriting_scans.insert(FuncKey{func.name(), update_id});
                }
            }
        }
    }
    is_forward_overwrite_detection_phase = false;

    // Bounds inference
    Box output_box;
    for (const auto &p : output_bounds) {
        // Convert from min,extent to min,max
        output_box.push_back(Interval(p.min, p.min + p.extent));
    }
    func_bounds = inference_bounds(output, output_box);
    for (const auto &it : func_bounds) {
        const Box &bounds = it.second;
        for (int d = 0; d < (int)bounds.size(); d++) {
            user_assert(bounds[d].is_bounded()) << "Access to function or buffer " << it.first << " at dimension " << d << " is not bounded. "
                                                << "We can only differentiate bounded accesses.\n";
        }
    }

    // Create a stub for each function and each update to accumulate adjoints.
    for (int func_id = 0; func_id < (int)funcs.size(); func_id++) {
        const Func &func = funcs[func_id];
        for (int update_id = -1; update_id < func.num_update_definitions(); update_id++) {
            Func adjoint_func(func.name() + "_" + std::to_string(update_id + 1) + "_d_def__");
            bool is_final_output = func_id == (int)funcs.size() - 1 &&
                                   update_id == func.num_update_definitions() - 1;
            vector<Var> args = func.args();
            for (auto &arg : args) {
                if (arg.is_implicit()) {
                    // Replace implicit variables with non implicit ones
                    arg = Var();
                }
            }
            if (is_final_output) {
                adjoint_func(args) = adjoint(args);
            } else {
                // Initialize to 0
                if (func.values().size() == 1) {
                    adjoint_func(args) = make_zero(func.values()[0].type());
                } else {
                    vector<Expr> init(func.values().size());
                    for (int i = 0; i < (int)init.size(); i++) {
                        init[i] = make_zero(func.values()[i].type());
                    }
                    adjoint_func(args) = Tuple(init);
                }
            }
            FuncKey func_key{func.name(), update_id};
            internal_assert(adjoint_funcs.find(func_key) == adjoint_funcs.end());
            adjoint_funcs[func_key] = adjoint_func;
        }
    }
    // Also create stubs for buffers referenced by the functions
    map<string, BufferInfo> called_buffers_or_param;
    for (auto &func : funcs) {
        map<string, BufferInfo> buffers = find_buffer_param_calls(func);
        called_buffers_or_param.insert(buffers.begin(), buffers.end());
    }
    for (const auto &it : called_buffers_or_param) {
        // Replace all the dots in the function names to make it legal.
        Func adjoint_func(replace_all(it.first, ".", "_") + "_d__");
        vector<Var> args(it.second.dimension);
        adjoint_func(args) = make_zero(it.second.type);
        FuncKey func_key{it.first, -1};
        if (adjoint_funcs.find(func_key) != adjoint_funcs.end()) {
            user_error << "Naming conflict between buffer/parameters and function:" << it.first << "\n";
        }
        adjoint_funcs[func_key] = adjoint_func;
    }

    // Traverse functions from producers to consumers for reverse accumulation
    for (const auto &func : reverse_view(funcs)) {
        current_func = func;

        FuncKey func_key{func.name(), func.num_update_definitions() - 1};
        // Traverse from the last update to first
        for (int update_id = func.num_update_definitions() - 1;
             update_id >= -1; update_id--) {
            current_update_id = update_id;
            FuncKey func_key{func.name(), update_id};
            Func adjoint_func = adjoint_funcs[func_key];
            internal_assert(func_bounds.find(func.name()) != func_bounds.end());
            // The propagation of adjoints to self reference goes to
            // current update instead of previous if it's a non overwriting scan
            is_current_non_overwriting_scan = false;
            if (update_id >= 0) {
                auto it = non_overwriting_scans.find(func_key);
                if (it != non_overwriting_scans.end()) {
                    is_current_non_overwriting_scan = true;
                }
            }

            // Initialize the next adjoint function by
            // propagating the adjoints to next update
            // Example:
            // f(x) = ...
            // f(1) = ... <- we're here
            // We have an adjoint for f(1) defined over the whole support of f
            // Now we want to initialize for the f(x) update
            // Need to propagate back to all x while masking 1
            // x -> next_args
            // 1 -> update_args
            auto mask_previous_update = [&]() {
                FuncKey prev_func_key{func.name(), update_id - 1};
                Func &prev_adjoint_func = adjoint_funcs[prev_func_key];
                vector<Var> prev_args = prev_adjoint_func.args();
                vector<Expr> update_args = func.update_args(update_id);
                // Replace implicit variables
                for (auto &arg : update_args) {
                    set<string> implicit_variables =
                        find_implicit_variables(arg);
                    for (const auto &var : implicit_variables) {
                        arg = substitute(var, prev_args[Var::implicit_index(var)], arg);
                    }
                }
                // Check if prev_args are the same as update_args
                // If they are the same simply set everything to zero
                bool is_noop = true;
                for (int i = 0; i < (int)prev_args.size(); i++) {
                    const Variable *update_var = update_args[i].as<Variable>();
                    if (update_var == nullptr || prev_args[i].name() != update_var->name) {
                        is_noop = false;
                    }
                }
                prev_adjoint_func = Func(prev_adjoint_func.name());
                if (!is_noop) {
                    // f'(x) = adjoint
                    prev_adjoint_func(prev_args) =
                        adjoint_funcs[func_key](prev_args);
                    if (func.values().size() == 1) {
                        Type type = func.values()[0].type();
                        prev_adjoint_func(update_args) = make_zero(type);
                    } else {
                        vector<Expr> init(func.values().size());
                        for (int i = 0; i < (int)init.size(); i++) {
                            init[i] = make_zero(func.values()[i].type());
                        }
                        prev_adjoint_func(update_args) = Tuple(init);
                    }
                } else {
                    if (func.values().size() == 1) {
                        Type type = func.values()[0].type();
                        prev_adjoint_func(prev_args) = make_zero(type);
                    } else {
                        vector<Expr> init(func.values().size());
                        for (int i = 0; i < (int)init.size(); i++) {
                            init[i] = make_zero(func.values()[i].type());
                        }
                        prev_adjoint_func(prev_args) = Tuple(init);
                    }
                }
            };
            if (update_id >= 0 && !is_current_non_overwriting_scan) {
                // Delay the masking if we're keeping track of intermediate values.
                // Since in this case we are propagating to current update,
                // instead of previous update.
                mask_previous_update();
            }

            // Now we want to propagate the derivatives at expression level.
            // We topologically sort the expressions for each value in the tuple.
            vector<Expr> expr_list;
            Tuple rhs_tuple =
                update_id < 0 ? func.values() : func.update_values(update_id);
            vector<const BaseExprNode *> output_exprs;
            const vector<Expr> &rhs_tuple_vector = rhs_tuple.as_vector();
            for (const auto &expr : rhs_tuple_vector) {
                vector<Expr> value_expr_list = sort_expressions(expr);
                expr_list.insert(
                    expr_list.end(), value_expr_list.begin(), value_expr_list.end());
                output_exprs.push_back((const BaseExprNode *)expr_list.back().get());
            }

            // TODO: replace let_var_mapping with Scope
            // Gather let variables
            let_var_mapping.clear();
            let_variables.clear();
            for (const auto &expr : expr_list) {
                if (const Let *op = expr.as<Let>()) {
                    // Assume Let variables are unique
                    internal_assert(let_var_mapping.find(op->name) == let_var_mapping.end());
                    let_var_mapping[op->name] = op->value;
                    let_variables.push_back(op->name);
                }
            }

            // Retrieve previously propagated adjoint for the Func,
            // apply it to expression adjoints.
            // f(x) = g(x)
            // d_g(x) = d_f(x) * df/dg
            vector<Expr> update_args;
            if (update_id >= 0) {
                update_args = func.update_args(update_id);
            } else {
                update_args.reserve(func.args().size());
                Func adjoint_func = adjoint_funcs[func_key];
                for (const auto &var : adjoint_func.args()) {
                    update_args.push_back(var);
                }
            }

            // We propagate in two phases, the first phase only propagates
            // to self references, the second phase propagates to the rest.
            {  // First phase
                is_self_referencing_phase = true;
                expr_adjoints.clear();
                if (output_exprs.size() == 1) {
                    expr_adjoints[output_exprs[0]] =
                        (adjoint_funcs[func_key])(update_args);
                } else {
                    for (int i = 0; i < (int)output_exprs.size(); i++) {
                        expr_adjoints[output_exprs[i]] =
                            (adjoint_funcs[func_key])(update_args)[i];
                    }
                }

                for (Expr &e : reverse_view(expr_list)) {
                    if (e.type().is_handle()) {
                        // Ignore pointer types
                        continue;
                    }
                    // Propagate adjoints
                    e.accept(this);
                }
            }
            if (is_current_non_overwriting_scan) {
                // Now, if we detect a non-overwriting scan operation,
                // the update of adjoints goes to the current function.
                // We let the previous adjoint the same as the current one

                FuncKey prev_func_key{func_key.first, func_key.second - 1};
                // Recreate a new adjoint for previous update
                Func prev_adjoint;
                vector<Expr> args;
                args.reserve(adjoint_func.args().size());
                for (const auto &arg : adjoint_func.args()) {
                    args.push_back(arg);
                }
                vector<Expr> calls;
                calls.reserve(rhs_tuple.size());
                for (int i = 0; i < (int)rhs_tuple.size(); i++) {
                    calls.push_back(Call::make(
                        adjoint_funcs[func_key].function(), args, i));
                }
                prev_adjoint(args) = Tuple(calls);
                adjoint_funcs[prev_func_key] = prev_adjoint;
                mask_previous_update();
            }
            {  // Second phase
                is_self_referencing_phase = false;
                expr_adjoints.clear();
                for (int i = 0; i < (int)output_exprs.size(); i++) {
                    expr_adjoints[output_exprs[i]] =
                        Call::make(adjoint_funcs[func_key].function(),
                                   update_args, i);
                }

                for (Expr &e : reverse_view(expr_list)) {
                    if (e.type().is_handle()) {
                        // Ignore pointer types
                        continue;
                    }
                    // Propagate adjoints
                    e.accept(this);
                }
            }
        }
    }
}

void ReverseAccumulationVisitor::accumulate(const Expr &stub, Expr adjoint) {
    const BaseExprNode *stub_ptr = (const BaseExprNode *)stub.get();

    // Trick to avoid NaN in select() clauses:
    // select(c, x, 0) * y -> select(c, x * y, 0)
    // x * select(c, y, 0) -> select(c, x * y, 0)
    // select(c, x, 0) / y -> select(c, x / y, 0)
    if (adjoint.as<Mul>() != nullptr) {
        const Mul *mul_op = adjoint.as<Mul>();
        auto mul_select_with_zero = [&](const Expr &sel, const Expr &other) {
            const Select *sel_op = sel.as<Select>();
            if (is_const_zero(sel_op->true_value)) {
                return select(sel_op->condition,
                              sel_op->true_value, sel_op->false_value * other);
            }
            if (is_const_zero(sel_op->false_value)) {
                return select(sel_op->condition,
                              sel_op->true_value * other, sel_op->false_value);
            }
            return sel * other;
        };
        if (mul_op->a.as<Select>() != nullptr) {
            adjoint = mul_select_with_zero(mul_op->a, mul_op->b);
        } else if (mul_op->b.as<Select>() != nullptr) {
            adjoint = mul_select_with_zero(mul_op->b, mul_op->a);
        }
    }
    if (adjoint.as<Div>() != nullptr) {
        const Div *div_op = adjoint.as<Div>();
        auto div_select_with_zero = [&](const Expr &sel, const Expr &other) {
            const Select *sel_op = sel.as<Select>();
            if (is_const_zero(sel_op->true_value)) {
                return select(sel_op->condition,
                              sel_op->true_value, sel_op->false_value / other);
            }
            if (is_const_zero(sel_op->false_value)) {
                return select(sel_op->condition,
                              sel_op->true_value / other, sel_op->false_value);
            }
            return sel * other;
        };
        if (div_op->a.as<Select>() != nullptr) {
            adjoint = div_select_with_zero(div_op->a, div_op->b);
        }
    }

    if (expr_adjoints.find(stub_ptr) == expr_adjoints.end()) {
        expr_adjoints[stub_ptr] = adjoint;
    } else {
        expr_adjoints[stub_ptr] = expr_adjoints[stub_ptr] + adjoint;
    }
}

void ReverseAccumulationVisitor::visit(const IntImm *op) {
    // Nothing to propagate to
}

void ReverseAccumulationVisitor::visit(const UIntImm *op) {
    // Nothing to propagate to
}

void ReverseAccumulationVisitor::visit(const FloatImm *op) {
    // Nothing to propagate to
}

void ReverseAccumulationVisitor::visit(const StringImm *op) {
    // Nothing to propagate to
}

void ReverseAccumulationVisitor::visit(const Cast *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // d/dx cast(x) = 1.f if op->type is float otherwise 0
    if (op->type.is_float()) {
        accumulate(op->value, cast(op->value.type(), adjoint));
    } else {
        accumulate(op->value, make_zero(op->value.type()));
    }
}

void ReverseAccumulationVisitor::visit(const Reinterpret *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // bit manipulation -- has zero derivative.
    accumulate(op->value, make_zero(op->type));
}

void ReverseAccumulationVisitor::visit(const Variable *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    if (op->param.defined()) {
        // This is a reference to a Parameter, propagate to the corresponding buffer
        propagate_halide_function_call(adjoint, op->param.name(), FunctionPtr(), {}, 0, op->type);
        return;
    }

    // If the variable is a let variable, accumulates adjoints into the content
    auto it = let_var_mapping.find(op->name);
    if (it != let_var_mapping.end()) {
        accumulate(it->second, Let::make(op->name, it->second, adjoint));
    }
}

void ReverseAccumulationVisitor::visit(const Add *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // d/da a + b = 1
    accumulate(op->a, adjoint);
    // d/db a + b = 1
    accumulate(op->b, adjoint);
}

void ReverseAccumulationVisitor::visit(const Sub *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // d/da a - b = 1
    accumulate(op->a, adjoint);
    // d/db a - b = -1
    accumulate(op->b, -adjoint);
}

void ReverseAccumulationVisitor::visit(const Mul *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // d/da a * b = b
    accumulate(op->a, adjoint * op->b);
    // d/db a * b = a
    accumulate(op->b, adjoint * op->a);
}

void ReverseAccumulationVisitor::visit(const Div *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // Trick to avoid NaN in select() clauses: if adjoint is a select with an 0,
    // multiply into it
    if (adjoint.as<Select>() != nullptr) {
        const Select *sel_op = adjoint.as<Select>();
        if (is_const_zero(sel_op->true_value)) {
            // d/da a / b = 1 / b
            accumulate(op->a, select(sel_op->condition,
                                     sel_op->true_value, sel_op->false_value / op->b));
            // d/db a * b = - a / b^2
            accumulate(op->b, select(sel_op->condition,
                                     sel_op->true_value, -sel_op->false_value * op->a / (op->b * op->b)));
            return;
        }
        if (is_const_zero(sel_op->false_value)) {
            // d/da a / b = 1 / b
            accumulate(op->a, select(sel_op->condition,
                                     sel_op->true_value / op->b, sel_op->false_value));
            // d/db a * b = - a / b^2
            accumulate(op->b, select(sel_op->condition,
                                     -sel_op->true_value * op->a / (op->b * op->b), sel_op->false_value));
            return;
        }
    }

    // d/da a / b = 1 / b
    accumulate(op->a, adjoint / op->b);
    // d/db a / b = - a / b^2
    accumulate(op->b, -adjoint * op->a / (op->b * op->b));
}

void ReverseAccumulationVisitor::visit(const Mod *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // a % b = a - trunc(a/b) * b
    // d/da = 1
    accumulate(op->a, adjoint);
    // d/db = -trunc(a/b)
    accumulate(op->b, -adjoint * trunc(op->a / op->b));
}

void ReverseAccumulationVisitor::visit(const Min *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // d/da min(a, b) = a <= b ? 1 : 0
    accumulate(op->a,
               select(op->a <= op->b, adjoint, make_zero(adjoint.type())));
    // d/db min(a, b) = b <= a ? 1 : 0
    accumulate(op->b,
               select(op->b <= op->a, adjoint, make_zero(adjoint.type())));
}

void ReverseAccumulationVisitor::visit(const Max *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // d/da max(a, b) = a >= b ? 1 : 0
    accumulate(op->a,
               select(op->a >= op->b, adjoint, make_zero(adjoint.type())));
    // d/db max(a, b) = b >= a ? 1 : 0
    accumulate(op->b,
               select(op->b >= op->a, adjoint, make_zero(adjoint.type())));
}

void ReverseAccumulationVisitor::visit(const EQ *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the arguments
    accumulate(op->a, make_zero(op->a.type()));
    accumulate(op->b, make_zero(op->b.type()));
}

void ReverseAccumulationVisitor::visit(const NE *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the arguments
    accumulate(op->a, make_zero(op->a.type()));
    accumulate(op->b, make_zero(op->b.type()));
}

void ReverseAccumulationVisitor::visit(const LT *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the arguments
    accumulate(op->a, make_zero(op->a.type()));
    accumulate(op->b, make_zero(op->b.type()));
}

void ReverseAccumulationVisitor::visit(const LE *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the arguments
    accumulate(op->a, make_zero(op->a.type()));
    accumulate(op->b, make_zero(op->b.type()));
}

void ReverseAccumulationVisitor::visit(const GT *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the arguments
    accumulate(op->a, make_zero(op->a.type()));
    accumulate(op->b, make_zero(op->b.type()));
}

void ReverseAccumulationVisitor::visit(const GE *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the arguments
    accumulate(op->a, make_zero(op->a.type()));
    accumulate(op->b, make_zero(op->b.type()));
}

void ReverseAccumulationVisitor::visit(const And *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the arguments
    accumulate(op->a, make_zero(op->a.type()));
    accumulate(op->b, make_zero(op->b.type()));
}

void ReverseAccumulationVisitor::visit(const Or *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the arguments
    accumulate(op->a, make_zero(op->a.type()));
    accumulate(op->b, make_zero(op->b.type()));
}

void ReverseAccumulationVisitor::visit(const Not *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    // Expr adjoint = expr_adjoints[op];

    // output is a boolean, so we should propagate zero to the argument
    accumulate(op->a, make_zero(op->a.type()));
}

void ReverseAccumulationVisitor::visit(const Let *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    accumulate(op->body, adjoint);
}

void ReverseAccumulationVisitor::visit(const Select *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];

    // d/db select(a, b, c) = select(a, 1, 0)
    accumulate(op->true_value,
               select(op->condition, adjoint, make_zero(adjoint.type())));
    // d/dc select(a, b, c) = select(a, 0, 1)
    accumulate(op->false_value,
               select(op->condition, make_zero(adjoint.type()), adjoint));
}

void ReverseAccumulationVisitor::visit(const Call *op) {
    internal_assert(expr_adjoints.find(op) != expr_adjoints.end());
    Expr adjoint = expr_adjoints[op];
    // Math functions (Can be both intrinsic and extern).
    if (is_math_func(op, "exp", Call::fast_exp)) {
        // d/dx exp(x) = exp(x)
        accumulate(op->args[0], adjoint * exp(op->args[0]));
    } else if (is_math_func(op, "log", Call::fast_log)) {
        // d/dx log(x) = 1 / x
        accumulate(op->args[0], adjoint / op->args[0]);
    } else if (is_math_func(op, "sin", Call::fast_sin)) {
        // d/dx sin(x) = cos(x)
        accumulate(op->args[0], adjoint * cos(op->args[0]));
    } else if (is_math_func(op, "asin")) {
        // d/dx asin(x) = 1 / sqrt(1 - x^2)
        Expr one = make_one(op->type);
        accumulate(op->args[0], adjoint / sqrt(one - op->args[0] * op->args[0]));
    } else if (is_math_func(op, "cos", Call::fast_cos)) {
        // d/dx cos(x) = -sin(x)
        accumulate(op->args[0], -adjoint * sin(op->args[0]));
    } else if (is_math_func(op, "acos")) {
        // d/dx acos(x) = - 1 / sqrt(1 - x^2)
        Expr one = make_one(op->type);
        accumulate(op->args[0], -adjoint / sqrt(one - op->args[0] * op->args[0]));
    } else if (is_math_func(op, "tan", Call::fast_tan)) {
        // d/dx tan(x) = 1 / cos(x)^2
        Expr c = cos(op->args[0]);
        accumulate(op->args[0], adjoint / (c * c));
    } else if (is_math_func(op, "atan", Call::fast_atan)) {
        // d/dx atan(x) = 1 / (1 + x^2)
        Expr one = make_one(op->type);
        accumulate(op->args[0], adjoint / (one + op->args[0] * op->args[0]));
    } else if (is_math_func(op, "atan2", Call::fast_atan2)) {
        Expr x2y2 = op->args[0] * op->args[0] + op->args[1] * op->args[1];
        // d/dy atan2(y, x) = x / (x^2 + y^2)
        accumulate(op->args[0], adjoint * (op->args[1] / x2y2));
        // d/dx atan2(y, x) = -y / (x^2 + y^2)
        accumulate(op->args[1], adjoint * (-op->args[0] / x2y2));
    } else if (is_math_func(op, "sinh")) {
        // d/dx sinh(x) = cosh(x)
        accumulate(op->args[0], adjoint * cosh(op->args[0]));
    } else if (is_math_func(op, "asinh")) {
        // d/dx asin(x) = 1 / sqrt(1 + x^2)
        Expr one = make_one(op->type);
        accumulate(op->args[0], adjoint / sqrt(one + op->args[0] * op->args[0]));
    } else if (is_math_func(op, "cosh")) {
        // d/dx cosh(x) = sinh(x)
        accumulate(op->args[0], adjoint * sinh(op->args[0]));
    } else if (is_math_func(op, "acosh")) {
        // d/dx acosh(x) = 1 / (sqrt(x - 1) sqrt(x + 1)))
        Expr one = make_one(op->type);
        accumulate(op->args[0],
                   adjoint / (sqrt(op->args[0] - one) * sqrt(op->args[0] + one)));
    } else if (is_math_func(op, "tanh", Call::fast_tanh)) {
        // d/dx tanh(x) = 1 / cosh(x)^2
        Expr c = cosh(op->args[0]);
        accumulate(op->args[0], adjoint / (c * c));
    } else if (is_math_func(op, "atanh")) {
        // d/dx atanh(x) = 1 / (1 - x^2)
        Expr one = make_one(op->type);
        accumulate(op->args[0], adjoint / (one - op->args[0] * op->args[0]));
    } else if (is_math_func(op, "ceil")) {
        // TODO: d/dx = dirac(n) for n in Z ...
        accumulate(op->args[0], make_zero(op->type));
    } else if (is_math_func(op, "floor")) {
        // TODO: d/dx = dirac(n) for n in Z ...
        accumulate(op->args[0], make_zero(op->type));
    } else if (is_math_func(op, "round")) {
        accumulate(op->args[0], make_zero(op->type));
    } else if (is_math_func(op, "trunc")) {
        accumulate(op->args[0], make_zero(op->type));
    } else if (is_math_func(op, "sqrt")) {
        Expr half = make_const(op->type, 0.5);
        accumulate(op->args[0], adjoint * (half / sqrt(op->args[0])));
    } else if (is_math_func(op, "pow", Call::fast_pow)) {
        Expr one = make_one(op->type);
        accumulate(op->args[0],
                   adjoint * op->args[1] * pow(op->args[0], op->args[1] - one));
        accumulate(op->args[1],
                   adjoint * pow(op->args[0], op->args[1]) * log(op->args[0]));
    } else if (is_math_func(op, "fast_inverse")) {
        // d/dx 1/x = -1/x^2
        Expr inv_x = fast_inverse(op->args[0]);
        accumulate(op->args[0], -adjoint * inv_x * inv_x);
    } else if (is_math_func(op, "fast_inverse_sqrt")) {
        // d/dx x^(-0.5) = -0.5*x^(-1.5)
        Expr inv_sqrt_x = fast_inverse_sqrt(op->args[0]);
        Expr neg_half = make_const(op->type, -0.5);
        accumulate(op->args[0],
                   neg_half * adjoint * inv_sqrt_x * inv_sqrt_x * inv_sqrt_x);
    } else if (op->is_extern() && op->name == "halide_print") {
        for (const auto &arg : op->args) {
            accumulate(arg, make_zero(op->type));
        }
    } else if (op->is_extern()) {
        internal_error << "The derivative of " << op->name << " is not implemented.";
    } else if (op->is_intrinsic()) {
        if (op->is_intrinsic(Call::abs)) {
            accumulate(op->args[0],
                       adjoint * select(op->args[0] > 0,
                                        make_one(op->type), make_const(op->type, -1.0)));
        } else if (op->is_intrinsic(Call::lerp)) {
            // z = x * (1 - w) + y * w
            // dz/dx = 1 - w
            // dz/dy = w
            // dz/dw = y - x
            accumulate(op->args[0], adjoint * (make_one(op->type) - op->args[2]));
            accumulate(op->args[1], adjoint * op->args[2]);
            accumulate(op->args[2], adjoint * (op->args[1] - op->args[0]));
        } else if (Call::as_tag(op)) {
            accumulate(op->args[0], adjoint);
        } else if (op->is_intrinsic(Call::return_second)) {
            accumulate(op->args[0], make_const(op->type, 0.0));
            accumulate(op->args[1], adjoint);
        } else if (op->is_intrinsic(Call::undef)) {
            // do nothing
        } else if (op->is_intrinsic(Call::bitwise_and) ||
                   op->is_intrinsic(Call::bitwise_not) ||
                   op->is_intrinsic(Call::bitwise_or) ||
                   op->is_intrinsic(Call::bitwise_xor) ||
                   op->is_intrinsic(Call::shift_right) ||
                   op->is_intrinsic(Call::shift_left)) {
            // bit manipulations -- these have zero derivatives.
            for (const auto &arg : op->args) {
                accumulate(arg, make_zero(op->type));
            }
        } else {
            user_warning << "Dropping gradients at call to " << op->name << "\n";
            for (const auto &arg : op->args) {
                accumulate(arg, make_zero(op->type));
            }
        }
    } else if (op->call_type == Call::Halide ||
               op->call_type == Call::Image) {  // Halide function call or Halid buffer
        propagate_halide_function_call(adjoint, op->name, op->func, op->args, op->value_index, op->type);
    } else {
        // TODO: let user provide derivatives for external functions
        internal_error << "Unknown call type of operation: " << op->name << "\n";
    }
}

void ReverseAccumulationVisitor::propagate_halide_function_call(
    Expr adjoint, const std::string &name, const FunctionPtr &func_ptr,
    const std::vector<Expr> &call_args, int value_index, const Type &type) {
    if (!type.is_float()) {
        // If the function call does not return continuous output,
        // don't propagate to the function.
        return;
    }
    // Add Let expressions
    adjoint = add_let_expression(adjoint, let_var_mapping, let_variables);
    vector<Expr> lhs = call_args;
    for (auto &arg : lhs) {
        arg = add_let_expression(arg, let_var_mapping, let_variables);
    }
    Expr adjoint_before_canonicalize = adjoint;
    vector<Expr> lhs_before_canonicalize = lhs;

    if (is_forward_overwrite_detection_phase) {
        // Don't need to propagate through function in this phase, we're just
        // checking local derivatives
        // However, we'll accumulate the derivatives to self reference
        // for checking if the self update is harmful for gradients
        if (func_ptr.same_as(current_func.function().get_contents())) {
            self_reference_adjoint[value_index] =
                simplify(self_reference_adjoint[value_index] + adjoint);
            vector<Expr> args = call_args;
            for (auto &arg : args) {
                arg = add_let_expression(arg, let_var_mapping, let_variables);
            }
            self_reference_args.push_back(args);
        }
        return;
    }
    if (is_self_referencing_phase) {
        // We want to make sure we propagate to the self reference first.
        // In this phase only self reference is propagated
        if (!func_ptr.same_as(current_func.function().get_contents())) {
            return;
        }
    } else {
        // In the other phase we ignore the self reference
        if (func_ptr.same_as(current_func.function().get_contents())) {
            return;
        }
    }

    // We create different functions for the initial condition and each update
    // When update i uses value from update i-1, we accumulate the
    // adjoints to update i-1
    // If target is the current function itself, send to previous update
    // e.g. f(x) = ...
    //      f(x) = f(x) + 1
    // For the one with non-commutative-associative reductions
    // e.g. f(x, ver) = ...
    //      f(x, 0) = ...
    //      f(x, r.x + 1) = f(x, r.x) * f(x, r.x) + g(r.x)
    // We propagate the whole r.x to the current update.
    // In addition, we propagate the first one d_f(x, 0) to the previous update,
    // by setting all reduction variables to their min() values.
    // Because only f(x, 0) comes from the last update, and
    // the rest belongs to the current update.
    // The above case will be handled by the caller, here we just
    // propagate to current update.
    // TODO: make the comments clearer and clean up the code
    FuncKey func_key;
    if (func_ptr.defined()) {
        Function func(func_ptr);
        func_key = func.name() != current_func.name() ? FuncKey{func.name(), func.updates().size() - 1} : FuncKey{func.name(), current_update_id - 1};
        if (is_current_non_overwriting_scan && is_self_referencing_phase) {
            func_key = FuncKey{func.name(), current_update_id};
        }
    } else {
        func_key = FuncKey{name, -1};
    }
    internal_assert(adjoint_funcs.find(func_key) != adjoint_funcs.end());
    Func &func_to_update = adjoint_funcs[func_key];
    internal_assert(func_to_update.dimensions() == (int)lhs.size());

    bool debug_flag = false;
    adjoint = simplify(common_subexpression_elimination(adjoint));

    if (debug_flag) {
        debug(0) << "current_func:" << current_func.name() << "\n";
        debug(0) << "Scattering to " << name << "\n";
        debug(0) << "lhs is:";
        for (const auto &arg : lhs) {
            debug(0) << " " << arg;
        }
        debug(0) << "\n";
        debug(0) << "adjoint is:" << simplify(adjoint) << "\n";
    }

    // Gather argument & bounds information
    // current_args are the pure variables
    // current_update_args are the actual updates at left hand side
    Func current_adjoint_func =
        adjoint_funcs[FuncKey{current_func.name(), current_update_id}];
    vector<Var> current_args = current_adjoint_func.args();
    const Box &current_bounds = func_bounds[current_func.name()];

    // Replace implicit variables
    for (auto &arg : lhs) {
        set<string> implicit_variables = find_implicit_variables(arg);
        for (const auto &var : implicit_variables) {
            arg = substitute(var, current_args[Var::implicit_index(var)], arg);
        }
    }
    {
        set<string> implicit_variables =
            find_implicit_variables(adjoint);
        for (const auto &var : implicit_variables) {
            adjoint = substitute(
                var, current_args[Var::implicit_index(var)], adjoint);
        }
    }

    // We want to do this:
    // func_to_update(call_args) += adjoint(current_update_args);
    // But call_args can be invalid lhs, need to canonicalize.
    // We canonicalize by first trying to substitute with pure variables.
    // If that fails we will replace variables on lhs with RDoms
    // (general scattering).

    // We try canonicalize the left hand side arguments (call_args)
    // so that it's always x, y, z, ...
    //
    // Given:
    // g(x, y, z) = f(x, y-1, z+1)
    // we get an invalid update:
    // f'(x, y - 1, z + 1) += g'(x, y, z)
    // Goal: rewrite to
    //  ==> f'(x, y, z) += g'(x, y+1, z-1)
    // (below we would call g and g' the "current function" and
    //  we call f and d_f the "function to update")
    //
    // We do this by set up a new set of variables new_args
    // new_args contains a set of variable u0, u1, u2, ...
    // For each left hand side of the update (x, y - 1, z + 1 here),
    // we set up the equations u0 = x, u1 = y - 1, u2 = z + 1.
    // Then we solve for x, y, z and get x = u0, y = u1 + 1, z = u2 - 1
    // We get f'(u0, u1, u2) += g'(u0, u1 + 1, u2 - 1)
    // We then substitute the original variable names back to get
    // f'(x, y, z) += g'(x, x + 1, z - 1)
    //
    // Note that g' would correctly returns 0 outside g's boundary,
    // therefore we do not need to impose bounds on g'.
    // However, consider the case where f'(...) += g'(...) * h(...):
    // we need to clamp h's arguments such that it never goes out of g's domain,
    // otherwise we may get unwanted out-of-bound buffer access.
    //
    // Currently we don't want to mess with system solving.
    // Therefore we gather all arguments that contains multiple pure variables,
    // and invalidate all of them.
    // Inter-dependencies like:
    // g(x, y) = f(x * y, x + y)
    // can't be simplified.
    // In principle this can be inverted by solving a system of equations.
    // In this case we replace x and y with reduction variables that loop
    // through g's bounds
    // i.e.
    // f'(r.x * r.y, r.x + r.y) += g'(r.x, r.y)

    // Prepare a set of new substitution variables for func_to_update
    vector<Var> new_args;
    new_args.reserve(func_to_update.dimensions());
    for (int arg_id = 0; arg_id < func_to_update.dimensions(); arg_id++) {
        new_args.emplace_back(unique_name("u" + std::to_string(arg_id)));
    }

    // Loop over the left hand side of the update, construct equations
    // and invert them.
    vector<bool> canonicalized(lhs.size(), false);
    set<string> canonicalized_vars;
    map<string, Var> lhs_substitute_map;
    for (int arg_id = 0; arg_id < (int)lhs.size(); arg_id++) {
        // Gather all pure variables at call_args[arg_id],
        // substitute them with new_args
        // For now only support single pure variable
        vector<int> variable_ids =
            gather_variables(lhs[arg_id], current_args);
        if (variable_ids.size() != 1) {
            continue;
        }

        int variable_id = variable_ids[0];
        const string &variable = current_args[variable_id].name();
        auto [solved, result_rhs] =
            solve_inverse(new_args[arg_id] == lhs[arg_id],
                          new_args[arg_id].name(),
                          variable);
        if (!solved) {
            continue;
        }

        // Substitute all access to variable to clamped version
        Expr clamped_variable = clamp(likely(current_args[variable_id]),
                                      current_bounds[variable_id].min,
                                      current_bounds[variable_id].max);
        adjoint = substitute_rdom_predicate(variable, clamped_variable, adjoint);
        // However we don't want to clamp the access to adjoint function (we need
        // it to return 0 outside of its bounds). We replace the corresponding
        // clamped argument back with the pure variable. It is safe to do
        // so because pure variable in Halide's update function can only be appeared
        // unadorned in the same position.
        adjoint = substitute_call_arg_with_pure_arg(current_adjoint_func,
                                                    variable_id,
                                                    adjoint);

        // Replace pure variable with the reverse.
        // Make sure to also substitute predicates.
        adjoint = substitute_rdom_predicate(variable, result_rhs, adjoint);

        // Since we successfully invert, the left hand side becomes new_args
        lhs[arg_id] = new_args[arg_id];
        // Record that we successfully invert, for those we fail
        // we need to perform general scattering.
        canonicalized[arg_id] = true;
        canonicalized_vars.insert(variable);
        lhs_substitute_map[variable] = new_args[arg_id];
    }

    // Consider the following case:
    // f(x, y) = ...
    // k(n) = f(g(n), n)
    // When we update d_f, the second n would be replaced by y.
    // We need to make sure we also update the call argument to g.
    // Adjoint is automatically handled in the loop above.
    for (auto &let : lhs) {
        for (const auto &it : lhs_substitute_map) {
            let = substitute(it.first, it.second, let);
        }
    }

    // Sometimes the canonicalization above fails.
    // We replace the pure variables inside lhs with RDoms for general scattering
    Region bounds;
    bounds.reserve(current_args.size());
    for (int arg_id = 0; arg_id < (int)current_args.size(); arg_id++) {
        const Interval &interval = current_bounds[arg_id];
        bounds.emplace_back(interval.min, interval.max - interval.min + 1);
    }
    RDom r_bounds(bounds);
    for (int lhs_id = 0; lhs_id < (int)lhs.size(); lhs_id++) {
        if (!canonicalized[lhs_id]) {
            Expr lhs_arg = lhs[lhs_id];
            vector<string> adjoint_args = current_adjoint_func.function().args();
            vector<int> variable_ids = gather_variables(lhs_arg, adjoint_args);
            // For each variable found in lhs_arg, find the corresponding
            // bound (by looping through all variables) and substitute
            // with the bound reduction variable.
            for (int variable_id : variable_ids) {
                for (int arg_id = 0; arg_id < (int)current_args.size(); arg_id++) {
                    const string &variable = adjoint_args[variable_id];
                    if (current_args[arg_id].name() == variable &&
                        canonicalized_vars.find(
                            current_args[arg_id].name()) ==
                            canonicalized_vars.end()) {
                        lhs[lhs_id] = substitute(variable,
                                                 r_bounds[arg_id],
                                                 lhs[lhs_id]);
                        adjoint = substitute(variable, r_bounds[arg_id], adjoint);
                        break;
                    }
                }
            }
        }
    }

    // For each free variable on the rhs, replace it with current bounds
    // e.g. we have in forward pass f(x, y) = g(x)
    //      then we would have g'(x) += f'(x, y) by now
    //      now we need to replace y with a reduction variable over f's bound
    //      x is automatically excluded since it's currently
    //      replaced by the new substitution variable e.g. u_0

    // First gather all free variables
    Region bounds_subset;
    vector<int> arg_id_to_substitute;
    bounds_subset.reserve(current_args.size());
    arg_id_to_substitute.reserve(current_args.size());
    for (int arg_id = 0; arg_id < (int)current_args.size(); arg_id++) {
        if (expr_uses_var(adjoint, current_args[arg_id].name())) {
            const Interval &interval = current_bounds[arg_id];
            bounds_subset.emplace_back(
                interval.min, interval.max - interval.min + 1);
            arg_id_to_substitute.push_back(arg_id);
        }
    }

    // Create a new RDom to loop over all free variables
    if (!arg_id_to_substitute.empty()) {
        RDom r(bounds_subset);
        for (int i = 0; i < (int)arg_id_to_substitute.size(); i++) {
            int arg_id = arg_id_to_substitute[i];
            adjoint = substitute(current_args[arg_id].name(), r[i], adjoint);
        }
    }

    // Simplify expressions
    adjoint = simplify(common_subexpression_elimination(adjoint));
    for (auto &e : lhs) {
        e = simplify(common_subexpression_elimination(e));
    }

    vector<Var> func_to_update_args = func_to_update.args();

    // General scattering simplification rules:
    // For each expression in lhs,
    // check if it is an expression of a single (associative & commutative)
    // rvar and spans the same interval of the function's bound
    // if so we can rewrite it back to pure variables
    // e.g.
    // f(r.x) = g(r.x)
    // => f(x) = g(x)
    //
    // Another common pattern is the reverse of downsampling
    // if we see s * r.x + r.y and r.y has min == 0 and extent == s
    // we simplify them to x and replace all occurrences of r.x by x/4
    // e.g.
    // f(4 * r.x + r.y) = g(r.x) + h(4 * r.x + r.y)
    // => f(x) = g(x/4) + h(x)
    Expr new_adjoint = func_to_update.values().size() == 1 ? (func_to_update(lhs) + adjoint) : (func_to_update(lhs)[value_index] + adjoint);
    vector<Expr> new_adjoint_tuple(func_to_update.values().size(), Expr(0.f));
    new_adjoint_tuple[value_index] = new_adjoint;
    AssociativeOp associative_op = prove_associativity(
        func_to_update.name(), lhs, new_adjoint_tuple);
    if (associative_op.associative() && associative_op.commutative()) {
        for (int i = 0; i < (int)lhs.size(); i++) {
            Expr lhs_arg = substitute_in_all_lets(lhs[i]);
            const Variable *var = lhs_arg.as<Variable>();
            const Add *add = lhs_arg.as<Add>();
            // f(r.x) = ... && r is associative
            // => f(x) = ...
            if (var != nullptr && var->reduction_domain.defined() &&
                is_const_one(var->reduction_domain.predicate())) {
                ReductionDomain rdom = var->reduction_domain;
                int rvar_id = -1;
                for (int rid = 0; rid < (int)rdom.domain().size(); rid++) {
                    if (rdom.domain()[rid].var == var->name) {
                        rvar_id = rid;
                        break;
                    }
                }
                internal_assert(rvar_id != -1);
                ReductionVariable rvar = rdom.domain()[rvar_id];
                // Check if the min/max of the rvariable is equal to
                // the target function
                const Box &target_bounds = func_bounds[name];
                Interval t_interval = target_bounds[i];
                t_interval.min = simplify(t_interval.min);
                t_interval.max = simplify(t_interval.max);
                Interval r_interval(simplify(rvar.min),
                                    simplify(rvar.min + rvar.extent - 1));
                if (can_prove(r_interval.min <= t_interval.min &&
                              r_interval.max >= t_interval.max)) {
                    lhs[i] = func_to_update_args[i];
                    Expr clamped_arg = clamp(func_to_update_args[i],
                                             r_interval.min, r_interval.max);
                    // Replace other occurrence of rvar in lhs
                    for (int j = 0; j < (int)lhs.size(); j++) {
                        if (j != i) {
                            lhs[j] = simplify(substitute(
                                rvar.var, clamped_arg, lhs[j]));
                        }
                    }
                    // Take care of boundary condition
                    Expr in_bound = func_to_update_args[i] >= r_interval.min &&
                                    func_to_update_args[i] <= r_interval.max;
                    adjoint = select(in_bound,
                                     simplify(substitute(rvar.var, clamped_arg, adjoint)),
                                     make_zero(adjoint.type()));
                }
                // f(4 * r.x + r.y) = g(r.x) + h(4 * r.x + r.y)
                // => f(x) = g(x/4) + h(x)
            } else if (add != nullptr &&
                       ((add->a.as<Mul>() != nullptr &&
                         add->b.as<Variable>() != nullptr) ||
                        (add->a.as<Variable>() != nullptr &&
                         add->b.as<Mul>() != nullptr))) {
                // Find pattern s * r.x + r.y where r.y.min == 0 && r.y.extent == s
                Expr a = add->a, b = add->b;
                if (add->b.as<Mul>() != nullptr) {
                    // swap so that b is always the Variable
                    internal_assert(add->a.as<Variable>() != nullptr);
                    std::swap(a, b);
                }
                const Mul *mul = a.as<Mul>();
                const Variable *b_var = b.as<Variable>();
                internal_assert(mul != nullptr && b_var != nullptr);
                Expr mul_a = mul->a, mul_b = mul->b;
                if (mul_a.as<Variable>() != nullptr &&
                    mul_a.as<Variable>()->reduction_domain.defined()) {
                    std::swap(mul_a, mul_b);
                }
                const Variable *mul_b_var = mul_b.as<Variable>();
                if (mul_b_var == nullptr || !mul_b_var->reduction_domain.defined()) {
                    continue;
                }
                ReductionDomain b_rdom = b_var->reduction_domain;
                if (!b_rdom.defined()) {
                    continue;
                }

                int rvar_id = -1;
                for (int rid = 0; rid < (int)b_rdom.domain().size(); rid++) {
                    if (b_rdom.domain()[rid].var == b_var->name) {
                        rvar_id = rid;
                        break;
                    }
                }
                internal_assert(rvar_id != -1);
                ReductionVariable rvar = b_rdom.domain()[rvar_id];
                if (!equal(rvar.min, Expr(0)) || !equal(rvar.extent, mul_a)) {
                    continue;
                }

                ReductionDomain mul_b_rdom = mul_b_var->reduction_domain;
                int mulb_rvar_id = -1;
                for (int rid = 0; rid < (int)mul_b_rdom.domain().size(); rid++) {
                    if (mul_b_rdom.domain()[rid].var == mul_b_var->name) {
                        mulb_rvar_id = rid;
                        break;
                    }
                }
                internal_assert(mulb_rvar_id != -1);
                ReductionVariable mulb_rvar = b_rdom.domain()[mulb_rvar_id];

                // Check if the min/max of the s * r.x + r.y is equal to
                // the target function
                const Box &target_bounds = func_bounds[name];
                Interval t_interval = target_bounds[i];
                t_interval.min = simplify(t_interval.min);
                t_interval.max = simplify(t_interval.max);
                Interval r_interval(simplify(mul_a * mulb_rvar.min),
                                    simplify(mul_a * mulb_rvar.extent - 1));

                if (can_prove(r_interval.min <= t_interval.min &&
                              r_interval.max >= t_interval.max)) {
                    // We've finally made sure that the expression has the form we want
                    // Now replace everything
                    // replace s * r.x + r.y with x
                    lhs[i] = func_to_update_args[i];
                    adjoint = substitute(lhs_arg,
                                         func_to_update_args[i],
                                         substitute_in_all_lets(adjoint));
                    // replace r.x with x / s
                    adjoint = substitute(mul_b, func_to_update_args[i] / mul_a, adjoint);
                    adjoint = simplify(adjoint);
                }
            }
        }
    }

    // We can only have one RDom for each update.
    // Therefore we have to merge RDoms on both lhs and rhs
    // To make use of better locality we preserve partial order
    map<string, ReductionVariableInfo> rvar_maps =
        gather_rvariables(adjoint);
    for (const auto &lhs_arg : lhs) {
        map<string, ReductionVariableInfo> maps =
            gather_rvariables(lhs_arg);
        rvar_maps.insert(maps.begin(), maps.end());
    }
    // Original set of reduction variables
    map<string, ReductionVariableInfo> org_rvar_maps =
        gather_rvariables(adjoint_before_canonicalize);
    for (const auto &lhs_arg : lhs_before_canonicalize) {
        map<string, ReductionVariableInfo> maps =
            gather_rvariables(lhs_arg);
        org_rvar_maps.insert(maps.begin(), maps.end());
    }
    // If the update is non-commutative or non-associative, we need to flip the
    // original set of reduction variable
    if (is_current_non_overwriting_scan) {
        // For each lhs
        for (auto &lhs_arg : lhs) {
            // For each original rvar
            for (const auto &it : org_rvar_maps) {
                RVar r(it.second.domain, it.second.index);
                Expr max = simplify(it.second.min + it.second.extent - 1);
                // Replace the reduction with the flipped version
                lhs_arg = substitute(it.first, max - r, lhs_arg);
            }
        }
        // For adjoint
        // For each original rvar
        for (const auto &it : org_rvar_maps) {
            RVar r(it.second.domain, it.second.index);
            Expr max = simplify(it.second.min + it.second.extent - 1);
            // Replace the reduction with the flipped version
            adjoint = substitute(it.first, max - r, adjoint);
        }
    }

    // Order: newly introduced rvar -> original rvar
    vector<ReductionVariableInfo> new_rvar_vec, old_rvar_vec;
    for (const auto &it : rvar_maps) {
        if (org_rvar_maps.find(it.first) == org_rvar_maps.end()) {
            new_rvar_vec.push_back(it.second);
        } else {
            old_rvar_vec.push_back(it.second);
        }
    }

    // Sort by index & domain
    auto cmp_rv = [](const ReductionVariableInfo &rv0,
                     const ReductionVariableInfo &rv1) {
        ReductionDomain::Compare cmp;
        if (cmp(rv0.domain, rv1.domain)) {
            return true;
        } else {
            return rv0.index < rv1.index;
        }
    };
    std::sort(new_rvar_vec.begin(), new_rvar_vec.end(), cmp_rv);
    std::sort(old_rvar_vec.begin(), old_rvar_vec.end(), cmp_rv);
    // Flatten to an array
    vector<string> var_names;
    Region merged_bounds;
    for (const auto &it : new_rvar_vec) {
        var_names.push_back(it.name);
        merged_bounds.emplace_back(it.min, it.extent);
    }
    for (const auto &it : old_rvar_vec) {
        var_names.push_back(it.name);
        merged_bounds.emplace_back(it.min, it.extent);
    }
    // Produce final merged RDom
    RDom merged_r;
    if (!merged_bounds.empty()) {
        merged_r = RDom(merged_bounds);
        // Transfer the predicate from old RDoms to merged RDom
        // Gather the set of RDoms
        set<ReductionDomain, ReductionDomain::Compare> rdoms;
        for (const auto &it : rvar_maps) {
            rdoms.insert(it.second.domain);
        }
        Expr rdom_predicate = Internal::UIntImm::make(UInt(1), 1);
        for (const auto &rdom : rdoms) {
            rdom_predicate = simplify(rdom_predicate && rdom.predicate());
        }
        // Reference to new RDom
        for (int rid = 0; rid < merged_r.dimensions(); rid++) {
            adjoint = substitute(var_names[rid], merged_r[rid], adjoint);
            for (auto &lhs_arg : lhs) {
                lhs_arg = substitute(var_names[rid], merged_r[rid], lhs_arg);
            }
            rdom_predicate = substitute(
                var_names[rid], merged_r[rid], rdom_predicate);
        }
        if (!is_const(rdom_predicate)) {
            for (int arg_id = 0; arg_id < (int)func_to_update_args.size(); arg_id++) {
                // Substitute new_args back to original variables
                rdom_predicate = substitute(new_args[arg_id].name(),
                                            func_to_update_args[arg_id], rdom_predicate);
            }
            merged_r.where(rdom_predicate);
        }
    }

    // Substitute new_args back to original variables
    for (int arg_id = 0; arg_id < (int)func_to_update_args.size(); arg_id++) {
        for (auto &lhs_arg : lhs) {
            lhs_arg = substitute(new_args[arg_id].name(),
                                 func_to_update_args[arg_id], lhs_arg);
        }
        adjoint = substitute_rdom_predicate(
            new_args[arg_id].name(), func_to_update_args[arg_id], adjoint);
    }

    // Simplify expressions
    adjoint = simplify(common_subexpression_elimination(adjoint));
    for (auto &e : lhs) {
        e = simplify(common_subexpression_elimination(e));
    }

    if (debug_flag) {
        debug(0) << "func_to_update.name():" << func_to_update.name() << "\n";
        debug(0) << "lhs after canonicalization:";
        for (const auto &arg : lhs) {
            debug(0) << " " << arg;
        }
        debug(0) << "\n";
        debug(0) << "adjoint after canonicalization:" << simplify(adjoint) << "\n";
    }

    // Finally we update the function definitions, possibly merge with previous updates
    auto can_merge = [&](Func &func_to_update,
                         const vector<Expr> &lhs) -> bool {
        if (func_to_update.num_update_definitions() == 0) {
            // If lhs are not pure variables we can't merge to pure definition
            for (int i = 0; i < (int)lhs.size(); i++) {
                if (!equal(lhs[i], func_to_update.args()[i])) {
                    return false;
                }
            }
            ReductionDomain rdom = extract_rdom(adjoint);
            // If there are rdoms in adjoint we can't merge
            return !rdom.defined();
        }
        int update_id = func_to_update.num_update_definitions() - 1;
        const vector<Expr> &prev_lhs =
            func_to_update.update_args(update_id);
        internal_assert(prev_lhs.size() == lhs.size());
        // If previous update has different left hand side, don't merge
        for (int i = 0; i < (int)prev_lhs.size(); i++) {
            if (!equal(lhs[i], prev_lhs[i])) {
                return false;
            }
        }
        // If previous update has a different set of reduction variables,
        // don't merge
        Function func = func_to_update.function();
        const vector<ReductionVariable> &rvars = func.update(update_id).schedule().rvars();
        if (!merged_r.defined()) {
            return rvars.empty();
        }
        if ((int)rvars.size() != merged_r.dimensions()) {
            return false;
        }

        for (int i = 0; i < (int)rvars.size(); i++) {
            if (!equal(rvars[i].min, merged_r[i].min())) {
                return false;
            }
            if (!equal(rvars[i].extent, merged_r[i].extent())) {
                return false;
            }
        }
        return true;
    };
    if (is_self_referencing_phase) {
        // If this is a self reference call, the relation is = instead of +=
        // For example, consider this:
        // f(x) = g(x)
        // f(k(r.x)) += h(r.x)
        // Multiple k(r.x) may correspond to the same index,
        // but they are overwritten in the reduction loop.
        // Therefore we should also overwrite their derivatives
        // by using = instead of +=
        if (!can_merge(func_to_update, lhs)) {
            if (func_to_update.values().size() == 1) {
                func_to_update(lhs) = adjoint;
            } else {
                func_to_update(lhs)[value_index] = adjoint;
            }
        } else {
            Definition &def = func_to_update.num_update_definitions() == 0 ? func_to_update.function().definition() : func_to_update.function().update(func_to_update.num_update_definitions() - 1);
            vector<Expr> &values = def.values();
            ReductionDomain rdom;
            for (const auto &val : values) {
                rdom = extract_rdom(val);
                if (rdom.defined()) {
                    break;
                }
            }
            if (rdom.defined()) {
                internal_assert(func_to_update.num_update_definitions() > 0);
                // Make sure we're using the same set of reduction variables
                for (int i = 0; i < merged_r.dimensions(); i++) {
                    adjoint = substitute(merged_r[i].name(), RVar(rdom, i), adjoint);
                }
            }

            if (values.size() == 1) {
                values[0] = adjoint;
            } else {
                values[value_index] = adjoint;
            }
        }
        return;
    }

    if (!can_merge(func_to_update, lhs)) {
        if (func_to_update.values().size() == 1) {
            func_to_update(lhs) += adjoint;
        } else {
            func_to_update(lhs)[value_index] += adjoint;
        }
    } else {
        Definition &def = func_to_update.num_update_definitions() == 0 ? func_to_update.function().definition() : func_to_update.function().update(func_to_update.num_update_definitions() - 1);
        vector<Expr> &values = def.values();
        ReductionDomain rdom;
        for (const auto &val : values) {
            rdom = extract_rdom(val);
            if (rdom.defined()) {
                break;
            }
        }
        if (rdom.defined()) {
            internal_assert(func_to_update.num_update_definitions() > 0);
            // Make sure we're using the same set of reduction variables
            for (int i = 0; i < merged_r.dimensions(); i++) {
                adjoint = substitute(merged_r[i].name(), RVar(rdom, i), adjoint);
            }
        }

        if (values.size() == 1) {
            values[0] = simplify(values[0] + adjoint);
        } else {
            const Add *add = values[value_index].as<Add>();
            if (add != nullptr &&
                add->b.as<Call>() != nullptr &&
                add->b.as<Call>()->is_intrinsic(Call::undef)) {
                // Sometimes the expression is an undef for the case of a tuple.
                // Make sure we don't include the undefs
                values[value_index] = simplify(add->a + adjoint);
            } else {
                values[value_index] =
                    simplify(values[value_index] + adjoint);
            }
        }
    }
}

}  // namespace
}  // namespace Internal

Func Derivative::operator()(const Func &func, int update_id) const {
    auto it = adjoints.find(FuncKey{func.name(), update_id});
    if (it == adjoints.end()) {
        Internal::debug(1) << "Could not find Func " << func.name() << "\n";
        return Func();
    }
    return it->second;
}

Func Derivative::operator()(const Buffer<> &buffer) const {
    auto it = adjoints.find(FuncKey{buffer.name(), -1});
    if (it == adjoints.end()) {
        Internal::debug(1) << "Could not find Buffer " << buffer.name() << "\n";
        return Func();
    }
    return it->second;
}

Func Derivative::operator()(const Param<> &param) const {
    auto it = adjoints.find(FuncKey{param.name(), -1});
    if (it == adjoints.end()) {
        Internal::debug(1) << "Could not find Param " << param.name() << "\n";
        return Func();
    }
    return it->second;
}

Func Derivative::operator()(const std::string &name) const {
    auto it = adjoints.find(FuncKey{name, -1});
    if (it == adjoints.end()) {
        Internal::debug(1) << "Could not find name: " << name << "\n";
        return Func();
    }
    return it->second;
}

Derivative propagate_adjoints(const Func &output,
                              const Func &adjoint,
                              const Region &output_bounds) {
    user_assert(output.dimensions() == adjoint.dimensions())
        << "output dimensions and adjoint dimensions must match\n";
    user_assert((int)output_bounds.size() == adjoint.dimensions())
        << "output_bounds and adjoint dimensions must match\n";

    Internal::ReverseAccumulationVisitor visitor;
    visitor.propagate_adjoints(output, adjoint, output_bounds);
    // Since the return value of get_adjoint_funcs() is a temporary,
    // we should *not* use std::move.
    return Derivative{visitor.get_adjoint_funcs()};
}

Derivative propagate_adjoints(const Func &output,
                              const Buffer<float> &adjoint) {
    user_assert(output.dimensions() == adjoint.dimensions());
    Region bounds;
    for (int dim = 0; dim < adjoint.dimensions(); dim++) {
        bounds.emplace_back(adjoint.min(dim), adjoint.min(dim) + adjoint.extent(dim) - 1);
    }
    Func adjoint_func = BoundaryConditions::constant_exterior(adjoint, 0.f);
    return propagate_adjoints(output, adjoint_func, bounds);
}

Derivative propagate_adjoints(const Func &output) {
    Func adjoint("adjoint");
    adjoint(output.args()) = Internal::make_one(output.value().type());
    Region output_bounds;
    output_bounds.reserve(output.dimensions());
    for (int i = 0; i < output.dimensions(); i++) {
        output_bounds.emplace_back(0, 0);
    }
    return propagate_adjoints(output, adjoint, output_bounds);
}

}  // namespace Halide
