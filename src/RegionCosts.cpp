#include "RegionCosts.h"
#include "FindCalls.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "PartitionLoops.h"
#include "RealizationOrder.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {

// Visitor for keeping track of all input images accessed and their types.
class FindImageInputs : public IRVisitor {
    using IRVisitor::visit;
    set<string> seen_image_param;

    void visit(const Call *call) override {
        if (call->call_type == Call::Image) {
            input_type[call->name] = call->type;

            // Call to an ImageParam
            if (call->param.defined() && (seen_image_param.count(call->name) == 0)) {
                for (int i = 0; i < call->param.dimensions(); ++i) {
                    const Expr &min = call->param.min_constraint_estimate(i);
                    const Expr &extent = call->param.extent_constraint_estimate(i);

                    user_assert(min.defined())
                        << "AutoSchedule: Estimate of the min value of ImageParam \""
                        << call->name << "\" in dimension " << i << " is not specified.\n";
                    user_assert(extent.defined())
                        << "AutoSchedule: Estimate of the extent value of ImageParam \""
                        << call->name << "\" in dimension " << i << " is not specified.\n";

                    string min_var = call->param.name() + ".min." + std::to_string(i);
                    string extent_var = call->param.name() + ".extent." + std::to_string(i);

                    input_estimates.emplace(min_var, Interval(min, min));
                    input_estimates.emplace(extent_var, Interval(extent, extent));
                    seen_image_param.insert(call->name);
                }
            }
        }
        for (const auto &arg : call->args) {
            arg.accept(this);
        }
    }

public:
    map<string, Type> input_type;
    map<string, Interval> input_estimates;
};

// Visitor for tracking the arithmetic and memory costs.
class ExprCost : public IRVisitor {
    using IRVisitor::visit;

    // Immediate values and variables do not incur any cost.
    void visit(const IntImm *) override {
    }
    void visit(const UIntImm *) override {
    }
    void visit(const FloatImm *) override {
    }
    void visit(const StringImm *) override {
    }
    void visit(const Variable *) override {
    }

    void visit(const Cast *op) override {
        op->value.accept(this);
        arith += 1;
    }

    template<typename T>
    void visit_binary_operator(const T *op, int op_cost) {
        op->a.accept(this);
        op->b.accept(this);
        arith += op_cost;
    }

    // The costs of all the simple binary operations is set to one.
    // TODO: Changing the costs for division and multiplication may be
    // beneficial. Write a test case to validate this and update the costs
    // accordingly.

    void visit(const Add *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const Sub *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const Mul *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const Div *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const Mod *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const Min *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const Max *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const EQ *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const NE *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const LT *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const LE *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const GT *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const GE *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const And *op) override {
        visit_binary_operator(op, 1);
    }
    void visit(const Or *op) override {
        visit_binary_operator(op, 1);
    }

    void visit(const Not *op) override {
        op->a.accept(this);
        arith += 1;
    }

    void visit(const Select *op) override {
        op->condition.accept(this);
        op->true_value.accept(this);
        op->false_value.accept(this);
        arith += 1;
    }

    void visit(const Call *call) override {
        if (call->is_intrinsic(Call::if_then_else)) {
            internal_assert(call->args.size() == 2 || call->args.size() == 3);

            int64_t current_arith = arith, current_memory = memory;
            arith = 0, memory = 0;
            if (call->args.size() == 3) {
                call->args[2].accept(this);
            }

            // Check if this if_then_else is because of tracing or print_when.
            // If it is, we should only take into account the cost of computing
            // the false expr since the true expr is debugging/tracing code.
            const Call *true_value_call = call->args[1].as<Call>();
            if (!true_value_call || !true_value_call->is_intrinsic(Call::return_second)) {
                int64_t false_cost_arith = arith;
                int64_t false_cost_memory = memory;

                // For if_then_else intrinsic, the cost is the max of true and
                // false branch costs plus the predicate cost.
                arith = 0, memory = 0;
                call->args[0].accept(this);
                int64_t pred_cost_arith = arith;
                int64_t pred_cost_memory = memory;

                arith = 0, memory = 0;
                call->args[1].accept(this);
                int64_t true_cost_arith = arith;
                int64_t true_cost_memory = memory;

                arith = pred_cost_arith + std::max(true_cost_arith, false_cost_arith);
                memory = pred_cost_memory + std::max(true_cost_memory, false_cost_memory);
            }
            arith += current_arith;
            memory += current_memory;
            return;
        } else if (call->is_intrinsic(Call::return_second)) {
            // For return_second, since the first expr would usually either be a
            // print_when or tracing, we should only take into account the cost
            // of computing the second expr.
            internal_assert(call->args.size() == 2);
            call->args[1].accept(this);
            return;
        }

        if (call->call_type == Call::Halide || call->call_type == Call::Image) {
            // Each call also counts as an op since it results in a load instruction.
            arith += 1;
            memory += call->type.bytes();
            detailed_byte_loads[call->name] += (int64_t)call->type.bytes();
        } else if (call->is_extern()) {
            // TODO: Suffix based matching is kind of sketchy; but going ahead with
            // it for now. Also not all the PureExtern's are accounted for yet.
            if (ends_with(call->name, "_f64")) {
                arith += 20;
            } else if (ends_with(call->name, "_f32")) {
                arith += 10;
            } else if (ends_with(call->name, "_f16")) {
                arith += 5;
            } else {
                // There is no visibility into an extern stage so there is no
                // way to know the cost of the call statically. Modeling the
                // cost of an extern stage requires profiling or user annotation.
                user_warning << "Unknown extern call " << call->name << "\n";
            }
        } else if (call->is_intrinsic()) {
            // TODO: Improve the cost model. In some architectures (e.g. ARM or
            // NEON), count_leading_zeros should be as cheap as bitwise ops.
            // div_round_to_zero and mod_round_to_zero can also get fairly expensive.
            if (call->is_intrinsic(Call::reinterpret) || call->is_intrinsic(Call::bitwise_and) ||
                call->is_intrinsic(Call::bitwise_not) || call->is_intrinsic(Call::bitwise_xor) ||
                call->is_intrinsic(Call::bitwise_or) || call->is_intrinsic(Call::shift_left) ||
                call->is_intrinsic(Call::shift_right) || call->is_intrinsic(Call::div_round_to_zero) ||
                call->is_intrinsic(Call::mod_round_to_zero) || call->is_intrinsic(Call::undef) ||
                call->is_intrinsic(Call::mux)) {
                arith += 1;
            } else if (call->is_intrinsic(Call::abs) || call->is_intrinsic(Call::absd) ||
                       call->is_intrinsic(Call::lerp) || call->is_intrinsic(Call::random) ||
                       call->is_intrinsic(Call::count_leading_zeros) ||
                       call->is_intrinsic(Call::count_trailing_zeros)) {
                arith += 5;
            } else if (Call::as_tag(call)) {
                // Tags do not result in actual operations.
            } else {
                // For other intrinsics, use 1 for the arithmetic cost.
                arith += 1;
                user_warning << "Unhandled intrinsic call " << call->name << "\n";
            }
        }

        for (const auto &arg : call->args) {
            arg.accept(this);
        }
    }

    void visit(const Shuffle *op) override {
        arith += 1;
    }

    void visit(const Let *let) override {
        let->value.accept(this);
        let->body.accept(this);
    }

    // None of the following IR nodes should be encountered when traversing the
    // IR at the level at which the auto scheduler operates.
    void visit(const Load *) override {
        internal_error;
    }
    void visit(const Ramp *) override {
        internal_error;
    }
    void visit(const Broadcast *) override {
        internal_error;
    }
    void visit(const LetStmt *) override {
        internal_error;
    }
    void visit(const AssertStmt *) override {
        internal_error;
    }
    void visit(const ProducerConsumer *) override {
        internal_error;
    }
    void visit(const For *) override {
        internal_error;
    }
    void visit(const Store *) override {
        internal_error;
    }
    void visit(const Provide *) override {
        internal_error;
    }
    void visit(const Allocate *) override {
        internal_error;
    }
    void visit(const Free *) override {
        internal_error;
    }
    void visit(const Realize *) override {
        internal_error;
    }
    void visit(const Block *) override {
        internal_error;
    }
    void visit(const IfThenElse *) override {
        internal_error;
    }
    void visit(const Evaluate *) override {
        internal_error;
    }

public:
    int64_t arith = 0;
    int64_t memory = 0;
    // Detailed breakdown of bytes loaded by the allocation or function
    // they are loaded from.
    map<string, int64_t> detailed_byte_loads;

    ExprCost() = default;
};

// Return the number of bytes required to store a single value of the
// function.
Expr get_func_value_size(const Function &f) {
    Expr size = 0;
    const vector<Type> &types = f.output_types();
    internal_assert(!types.empty());
    for (auto type : types) {
        size += type.bytes();
    }
    return simplify(size);
}

// Helper class that only accounts for the likely portion of the expression in
// the case of max, min, and select. This will help costing functions with
// boundary conditions better. The likely intrinsic triggers loop partitioning
// and on average (steady stage) the cost of the expression will be equivalent
// to the likely portion.
//
// TODO: Comment this out for now until we modify the compute expr cost and
// detailed byte loads functions to account for likely exprs.
/*class LikelyExpression : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Min *op) override {
        IRVisitor::visit(op);
        bool likely_a = has_likely_tag(op->a);
        bool likely_b = has_likely_tag(op->b);
        if (likely_a && !likely_b) {
            return op->a;
        } else if (likely_b && !likely_a) {
            return op->a;
        }
    }

    Expr visit(const Max *op) override {
        IRVisitor::visit(op);
        bool likely_a = has_likely_tag(op->a);
        bool likely_b = has_likely_tag(op->b);
        if (likely_a && !likely_b) {
            return op->a;
        } else if (likely_b && !likely_a) {
            return op->b;
        }
    }

    Expr visit(const Select *op) override {
        IRVisitor::visit(op);
        bool likely_t = has_likely_tag(op->true_value);
        bool likely_f = has_likely_tag(op->false_value);
        if (likely_t && !likely_f) {
            return op->true_value;
        } else if (likely_f && !likely_t) {
            return op->false_value;
        }
    }
};*/

Cost compute_expr_cost(Expr expr) {
    // TODO: Handle likely
    // expr = LikelyExpression().mutate(expr);
    expr = simplify(expr);
    ExprCost cost_visitor;
    expr.accept(&cost_visitor);
    return Cost(cost_visitor.arith, cost_visitor.memory);
}

map<string, Expr> compute_expr_detailed_byte_loads(Expr expr) {
    // TODO: Handle likely
    // expr = LikelyExpression().mutate(expr);
    expr = simplify(expr);
    ExprCost cost_visitor;
    expr.accept(&cost_visitor);

    map<string, Expr> loads;
    for (const auto &iter : cost_visitor.detailed_byte_loads) {
        loads.emplace(iter.first, Expr(iter.second));
    }
    return loads;
}

}  // anonymous namespace

RegionCosts::RegionCosts(const map<string, Function> &_env,
                         const vector<string> &_order)
    : env(_env), order(_order) {
    for (const auto &kv : env) {
        // Pre-compute the function costs without any inlining.
        func_cost[kv.first] = get_func_cost(kv.second);

        // Get the types of all the image inputs to the pipeline, including
        // their estimated min/extent values if applicable (i.e. if they are
        // ImageParam).
        FindImageInputs find;
        kv.second.accept(&find);
        for (const auto &in : find.input_type) {
            inputs[in.first] = in.second;
        }
        for (const auto &iter : find.input_estimates) {
            input_estimates.push(iter.first, iter.second);
        }
    }
}

Cost RegionCosts::stage_region_cost(const string &func, int stage, const DimBounds &bounds,
                                    const set<string> &inlines) {
    Function curr_f = get_element(env, func);

    Box stage_region;

    const vector<Dim> &dims = get_stage_dims(curr_f, stage);
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        stage_region.push_back(get_element(bounds, dims[d].var));
    }

    Expr size = box_size(stage_region);
    if (!size.defined()) {
        // Size could not be determined; therefore, it is not possible to
        // determine the arithmetic and memory costs.
        return Cost();
    }

    // If there is nothing to be inlined, use the pre-computed function cost.
    Cost cost = inlines.empty() ? get_element(func_cost, func)[stage] : get_func_stage_cost(curr_f, stage, inlines);
    if (!cost.defined()) {
        return Cost();
    }
    return Cost(simplify(size * cost.arith), simplify(size * cost.memory));
}

Cost RegionCosts::stage_region_cost(const string &func, int stage, const Box &region,
                                    const set<string> &inlines) {
    Function curr_f = get_element(env, func);

    DimBounds pure_bounds;
    const vector<string> &args = curr_f.args();
    internal_assert(args.size() == region.size());
    for (size_t d = 0; d < args.size(); d++) {
        pure_bounds.emplace(args[d], region[d]);
    }

    DimBounds stage_bounds = get_stage_bounds(curr_f, stage, pure_bounds);
    return stage_region_cost(func, stage, stage_bounds, inlines);
}

Cost RegionCosts::region_cost(const string &func, const Box &region, const set<string> &inlines) {
    Function curr_f = get_element(env, func);
    Cost region_cost(0, 0);

    int num_stages = curr_f.updates().size() + 1;
    for (int s = 0; s < num_stages; s++) {
        Cost stage_cost = stage_region_cost(func, s, region, inlines);
        if (!stage_cost.defined()) {
            return Cost();
        } else {
            region_cost.arith += stage_cost.arith;
            region_cost.memory += stage_cost.memory;
        }
    }

    internal_assert(region_cost.defined());
    region_cost.simplify();
    return region_cost;
}

Cost RegionCosts::region_cost(const map<string, Box> &regions, const set<string> &inlines) {
    Cost total_cost(0, 0);
    for (const auto &f : regions) {
        // The cost for pure inlined functions will be accounted in the
        // consumer of the inlined function so they should be skipped.
        if (inlines.find(f.first) != inlines.end()) {
            internal_assert(get_element(env, f.first).is_pure());
            continue;
        }

        Cost cost = region_cost(f.first, f.second, inlines);
        if (!cost.defined()) {
            return Cost();
        } else {
            total_cost.arith += cost.arith;
            total_cost.memory += cost.memory;
        }
    }

    internal_assert(total_cost.defined());
    total_cost.simplify();
    return total_cost;
}

map<string, Expr>
RegionCosts::stage_detailed_load_costs(const string &func, int stage,
                                       const set<string> &inlines) {
    map<string, Expr> load_costs;
    Function curr_f = get_element(env, func);

    if (curr_f.has_extern_definition()) {
        // TODO(psuriana): We need a better cost for extern function
        // load_costs.emplace(func, Int(64).max());
        load_costs.emplace(func, Expr());
    } else {
        Definition def = get_stage_definition(curr_f, stage);
        for (const auto &e : def.values()) {
            Expr inlined_expr = perform_inline(e, env, inlines, order);
            inlined_expr = simplify(inlined_expr);

            map<string, Expr> expr_load_costs = compute_expr_detailed_byte_loads(inlined_expr);
            combine_load_costs(load_costs, expr_load_costs);

            auto iter = load_costs.find(func);
            if (iter != load_costs.end()) {
                internal_assert(iter->second.defined());
                iter->second = simplify(iter->second + e.type().bytes());
            } else {
                load_costs.emplace(func, make_const(Int(64), e.type().bytes()));
            }
        }
    }

    return load_costs;
}

map<string, Expr>
RegionCosts::stage_detailed_load_costs(const string &func, int stage,
                                       DimBounds &bounds,
                                       const set<string> &inlines) {
    Function curr_f = get_element(env, func);

    Box stage_region;

    const vector<Dim> &dims = get_stage_dims(curr_f, stage);
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        stage_region.push_back(get_element(bounds, dims[d].var));
    }

    map<string, Expr> load_costs = stage_detailed_load_costs(func, stage, inlines);

    Expr size = box_size(stage_region);
    for (auto &kv : load_costs) {
        if (!kv.second.defined()) {
            continue;
        } else if (!size.defined()) {
            kv.second = Expr();
        } else {
            kv.second = simplify(kv.second * size);
        }
    }

    return load_costs;
}

map<string, Expr>
RegionCosts::detailed_load_costs(const string &func, const Box &region,
                                 const set<string> &inlines) {
    Function curr_f = get_element(env, func);
    map<string, Expr> load_costs;

    int num_stages = curr_f.updates().size() + 1;

    DimBounds pure_bounds;
    const vector<string> &args = curr_f.args();
    internal_assert(args.size() == region.size());
    for (size_t d = 0; d < args.size(); d++) {
        pure_bounds.emplace(args[d], region[d]);
    }

    vector<DimBounds> stage_bounds = get_stage_bounds(curr_f, pure_bounds);

    for (int s = 0; s < num_stages; s++) {
        map<string, Expr> stage_load_costs = stage_detailed_load_costs(func, s, inlines);

        Box stage_region;

        const vector<Dim> &dims = get_stage_dims(curr_f, s);
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            stage_region.push_back(get_element(stage_bounds[s], dims[d].var));
        }

        Expr size = box_size(stage_region);
        for (auto &kv : stage_load_costs) {
            if (!kv.second.defined()) {
                continue;
            } else if (!size.defined()) {
                kv.second = Expr();
            } else {
                kv.second = simplify(kv.second * size);
            }
        }

        combine_load_costs(load_costs, stage_load_costs);
    }

    return load_costs;
}

map<string, Expr>
RegionCosts::detailed_load_costs(const map<string, Box> &regions,
                                 const set<string> &inlines) {
    map<string, Expr> load_costs;
    for (const auto &r : regions) {
        // The cost for pure inlined functions will be accounted in the
        // consumer of the inlined function so they should be skipped.
        if (inlines.find(r.first) != inlines.end()) {
            internal_assert(get_element(env, r.first).is_pure());
            continue;
        }

        map<string, Expr> partial_load_costs = detailed_load_costs(r.first, r.second, inlines);
        combine_load_costs(load_costs, partial_load_costs);
    }

    return load_costs;
}

Cost RegionCosts::get_func_stage_cost(const Function &f, int stage,
                                      const set<string> &inlines) const {
    if (f.has_extern_definition()) {
        return Cost();
    }

    Definition def = get_stage_definition(f, stage);

    Cost cost(0, 0);

    for (const auto &e : def.values()) {
        Expr inlined_expr = perform_inline(e, env, inlines, order);
        inlined_expr = simplify(inlined_expr);

        Cost expr_cost = compute_expr_cost(inlined_expr);
        internal_assert(expr_cost.defined());
        cost.arith += expr_cost.arith;
        cost.memory += expr_cost.memory;

        // Accounting for the store
        cost.memory += e.type().bytes();
        cost.arith += 1;
    }

    if (!f.is_pure()) {
        for (const auto &arg : def.args()) {
            Expr inlined_arg = perform_inline(arg, env, inlines, order);
            inlined_arg = simplify(inlined_arg);

            Cost expr_cost = compute_expr_cost(inlined_arg);
            internal_assert(expr_cost.defined());
            cost.arith += expr_cost.arith;
            cost.memory += expr_cost.memory;
        }
    }

    cost.simplify();
    return cost;
}

vector<Cost> RegionCosts::get_func_cost(const Function &f, const set<string> &inlines) {
    if (f.has_extern_definition()) {
        return {Cost()};
    }

    vector<Cost> func_costs;
    size_t num_stages = f.updates().size() + 1;
    for (size_t s = 0; s < num_stages; s++) {
        func_costs.push_back(get_func_stage_cost(f, s, inlines));
    }
    return func_costs;
}

Expr RegionCosts::region_size(const string &func, const Box &region) {
    const Function &f = get_element(env, func);
    Expr size = box_size(region);
    if (!size.defined()) {
        return Expr();
    }
    Expr size_per_ele = get_func_value_size(f);
    internal_assert(size_per_ele.defined());
    return simplify(size * size_per_ele);
}

Expr RegionCosts::region_footprint(const map<string, Box> &regions,
                                   const set<string> &inlined) {
    map<string, int> num_consumers;
    for (const auto &f : regions) {
        num_consumers[f.first] = 0;
    }
    for (const auto &f : regions) {
        map<string, Function> prods = find_direct_calls(get_element(env, f.first));
        for (const auto &p : prods) {
            auto iter = num_consumers.find(p.first);
            if (iter != num_consumers.end()) {
                iter->second += 1;
            }
        }
    }

    vector<Function> outs;
    for (const auto &f : num_consumers) {
        if (f.second == 0) {
            outs.push_back(get_element(env, f.first));
        }
    }

    vector<string> top_order = topological_order(outs, env);

    Expr working_set_size = make_zero(Int(64));
    Expr curr_size = make_zero(Int(64));

    map<string, Expr> func_sizes;

    for (const auto &f : regions) {
        // Inlined functions do not have allocations
        bool is_inlined = inlined.find(f.first) != inlined.end();
        Expr size = is_inlined ? make_zero(Int(64)) : region_size(f.first, f.second);
        if (!size.defined()) {
            return Expr();
        } else {
            func_sizes.emplace(f.first, size);
        }
    }

    for (const auto &f : top_order) {
        if (regions.find(f) != regions.end()) {
            curr_size += get_element(func_sizes, f);
        }
        working_set_size = max(curr_size, working_set_size);
        map<string, Function> prods = find_direct_calls(get_element(env, f));
        for (const auto &p : prods) {
            auto iter = num_consumers.find(p.first);
            if (iter != num_consumers.end()) {
                iter->second -= 1;
                if (iter->second == 0) {
                    curr_size -= get_element(func_sizes, p.first);
                    internal_assert(!can_prove(curr_size < 0));
                }
            }
        }
    }

    return simplify(working_set_size);
}

Expr RegionCosts::input_region_size(const string &input, const Box &region) {
    Expr size = box_size(region);
    if (!size.defined()) {
        return Expr();
    }
    Expr size_per_ele = make_const(Int(64), get_element(inputs, input).bytes());
    internal_assert(size_per_ele.defined());
    return simplify(size * size_per_ele);
}

Expr RegionCosts::input_region_size(const map<string, Box> &input_regions) {
    Expr total_size = make_zero(Int(64));
    for (const auto &reg : input_regions) {
        Expr size = input_region_size(reg.first, reg.second);
        if (!size.defined()) {
            return Expr();
        } else {
            total_size += size;
        }
    }
    return simplify(total_size);
}

void RegionCosts::disp_func_costs() {
    debug(0) << "===========================\n"
             << "Pipeline per element costs:\n"
             << "===========================\n";
    for (const auto &kv : env) {
        int stage = 0;
        for (const auto &cost : func_cost[kv.first]) {
            if (kv.second.has_extern_definition()) {
                debug(0) << "Extern func\n";
            } else {
                Definition def = get_stage_definition(kv.second, stage);
                for (const auto &e : def.values()) {
                    debug(0) << simplify(e) << "\n";
                }
            }
            debug(0) << "(" << kv.first << ", " << stage << ") -> ("
                     << cost.arith << ", " << cost.memory << ")\n";
            stage++;
        }
    }
    debug(0) << "===========================\n";
}

bool is_func_trivial_to_inline(const Function &func) {
    if (!func.can_be_inlined()) {
        return false;
    }

    // For multi-dimensional tuple, we want to take the max over the arithmetic
    // and memory cost separately for conservative estimate.

    Cost inline_cost(0, 0);
    for (const auto &val : func.values()) {
        Cost cost = compute_expr_cost(val);
        internal_assert(cost.defined());
        inline_cost.arith = max(cost.arith, inline_cost.arith);
        inline_cost.memory = max(cost.memory, inline_cost.memory);
    }

    // Compute the cost if we were to call the function instead of inline it
    Cost call_cost(1, 0);
    for (const auto &type : func.output_types()) {
        call_cost.memory = max(type.bytes(), call_cost.memory);
    }

    Expr is_trivial = (call_cost.arith + call_cost.memory) >= (inline_cost.arith + inline_cost.memory);
    return can_prove(is_trivial);
}

void Cost::simplify() {
    if (arith.defined()) {
        arith = Internal::simplify(arith);
    }
    if (memory.defined()) {
        memory = Internal::simplify(memory);
    }
}

}  // namespace Internal
}  // namespace Halide
