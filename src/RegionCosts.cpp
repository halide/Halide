#include "RegionCosts.h"

namespace Halide {
namespace Internal {

bool has_suffix(const std::string &str, const std::string &suffix) {
    return str.size() >= suffix.size() &&
            str.compare(str.size() - suffix.size(),
                        suffix.size(), suffix) == 0;
}

/** Visitor for tracking the arithmetic and memory cost. */
struct ExprCost : public IRVisitor {
    Cost cost;
    // Detailed breakdown of bytes loaded by the allocation/function
    // they are loaded from.
    map<string, int64_t> detailed_byte_loads;

    ExprCost() {
        cost = Cost(0, 0);
    }

    using IRVisitor::visit;

    // Immediate values and variables do not incur any cost.
    void visit(const IntImm *) {}
    void visit(const UIntImm *) {}
    void visit(const FloatImm *) {}
    void visit(const StringImm *) {}
    void visit(const Variable *) {}

    void visit(const Cast *op) {
        op->value.accept(this);
        cost.arith += 1;
    }

    template<typename T> void visit_binary_operator(const T *op, int op_cost) {
        op->a.accept(this);
        op->b.accept(this);
        cost.arith += op_cost;
    }

    // The costs of all the simple binary operations is set to one.
    // TODO: Changing the costs for division and multiplication may be
    // beneficial. Write a test case to validate this and update the costs
    // accordingly.

    void visit(const Add *op) { visit_binary_operator(op, 1); }
    void visit(const Sub *op) { visit_binary_operator(op, 1); }
    void visit(const Mul *op) { visit_binary_operator(op, 1); }
    void visit(const Div *op) { visit_binary_operator(op, 1); }
    void visit(const Mod *op) { visit_binary_operator(op, 1); }
    void visit(const Min *op) { visit_binary_operator(op, 1); }
    void visit(const Max *op) { visit_binary_operator(op, 1); }
    void visit(const EQ *op) { visit_binary_operator(op, 1); }
    void visit(const NE *op) { visit_binary_operator(op, 1); }
    void visit(const LT *op) { visit_binary_operator(op, 1); }
    void visit(const LE *op) { visit_binary_operator(op, 1); }
    void visit(const GT *op) { visit_binary_operator(op, 1); }
    void visit(const GE *op) { visit_binary_operator(op, 1); }
    void visit(const And *op) { visit_binary_operator(op, 1); }
    void visit(const Or *op) { visit_binary_operator(op, 1); }

    void visit(const Not *op) {
        op->a.accept(this);
        cost.arith += 1;
    }

    void visit(const Select *op) {
        op->condition.accept(this);
        op->true_value.accept(this);
        op->false_value.accept(this);
        cost.arith += 1;
    }

    void visit(const Call *call) {
        if (call->call_type == Call::Halide || call->call_type == Call::Image) {
            // Each call also counts as an op since it results in a load instruction.
            cost.arith += 1;
            cost.memory += call->type.bytes();
            if (detailed_byte_loads.find(call->name) == detailed_byte_loads.end()) {
                detailed_byte_loads[call->name] = call->type.bytes();
            } else {
                detailed_byte_loads[call->name] += call->type.bytes();
            }
        } else if (call->call_type == Call::Extern || call->call_type == Call::PureExtern ||
                   call->call_type == Call::ExternCPlusPlus) {
            // TODO: Suffix based matching is kind of sketchy; but going ahead
            // with it for now. Also not all the PureExtern's are accounted for
            // yet.
            if (has_suffix(call->name, "_f64")) {
                cost.arith += 20;
            } else if(has_suffix(call->name, "_f32")) {
                cost.arith += 10;
            } else if(has_suffix(call->name, "_f16")) {
                cost.arith += 5;
            } else {
                // There is no visibility into an extern stage so there is no
                // way to know the cost of the call statically. Modeling the
                // cost of an extern stage requires profiling or user annotation.
                user_warning << "Unknown extern call " << call->name << '\n';
            }
        } else if (call->call_type == Call::Intrinsic || call->call_type == Call::PureIntrinsic) {
            if (call->name == "shuffle_vector" || call->name == "interleave_vectors" ||
                    call->name == "concat_vectors" || call->name == "reinterpret" ||
                    call->name == "bitwise_and" || call->name == "bitwise_not" ||
                    call->name == "bitwise_xor" || call->name == "bitwise_or" ||
                    call->name == "shift_left" || call->name == "shift_right" ||
                    call->name == "shift_left" || call->name == "shift_right" ||
                    call->name == "div_round_to_zero" || call->name == "mod_round_to_zero" ||
                    call->name == "undef") {
                cost.arith += 1;
            } else if (call->name == "abs" || call->name == "absd" ||
                    call->name == "lerp" || call->name == "random" ||
                    call->name == "count_leading_zeros" ||
                    call->name == "count_trailing_zeros") {
                cost.arith += 5;
            } else if (call->name == "likely") {
                // Likely does not result in actual operations.
            } else {
                internal_error << "Unknown intrinsic call " << call->name << '\n';
            }
        }

        for (size_t i = 0; (i < call->args.size()); i++) {
            call->args[i].accept(this);
        }
    }

    void visit(const Let *let) {
        let->value.accept(this);
        let->body.accept(this);
    }

    // None of the following IR nodes should be encountered when traversing the
    // IR at the level at which the auto scheduler operates.
    void visit(const Load *) { internal_assert(0); }
    void visit(const Ramp *) { internal_assert(0); }
    void visit(const Broadcast *) { internal_assert(0); }
    void visit(const LetStmt *) { internal_assert(0); }
    void visit(const AssertStmt *) { internal_assert(0); }
    void visit(const ProducerConsumer *) { internal_assert(0); }
    void visit(const For *) { internal_assert(0); }
    void visit(const Store *) { internal_assert(0); }
    void visit(const Provide *) { internal_assert(0); }
    void visit(const Allocate *) { internal_assert(0); }
    void visit(const Free *) { internal_assert(0); }
    void visit(const Realize *) { internal_assert(0); }
    void visit(const Block *) { internal_assert(0); }
    void visit(const IfThenElse *) { internal_assert(0); }
    void visit(const Evaluate *) { internal_assert(0); }
};

/** Returns the number of bytes required to store a single value of the
 * function. */
int64_t get_func_value_size(const Function &f) {
    int64_t size = 0;
    const vector<Type> &types = f.output_types();
    for (size_t i = 0; i < types.size(); i++) {
        size += types[i].bytes();
    }
    internal_assert(types.size() != 0);
    return size;
}

/* Returns the size of a interval. */
int64_t get_extent(const Interval &i) {
    // The concrete extent of a interval can be determined only when both the
    // expressions for min and max are integers.
    if ((i.min.as<IntImm>()) && (i.max.as<IntImm>())) {
        const IntImm *bmin = i.min.as<IntImm>();
        const IntImm *bmax = i.max.as<IntImm>();
        // The extent only makes sense when the max >= min otherwise
        // it is considered to be zero.
        if (bmin->value <= bmax->value) {
            return (bmax->value - bmin->value + 1);
        } else {
            return 0;
        }
    }
    return unknown;
}

int64_t box_size(const Box &b) {
    int64_t size = 1;
    for (size_t i = 0; i < b.size(); i++) {
        int64_t extent = get_extent(b[i]);
        if (extent != unknown && size != unknown) {
            size *= extent;
        } else if (extent == 0) {
            size = 0;
            break;
        } else {
            size = unknown;
        }
    }
    return size;
}

/* Adds partial load costs to the corresponding function in the result costs. */
void combine_load_costs(map<string, int64_t> &result,
                        const map<string, int64_t> &partial) {
    for (auto &kv : partial) {
        if (result.find(kv.first) == result.end()) {
            result[kv.first] = kv.second;
        } else {
            if (kv.second != unknown) {
                result[kv.first] += kv.second;
            } else {
                result[kv.first] = unknown;
            }
        }
    }
}

/* Returns the appropriate definition based on the stage of a function. */
Definition get_stage_definition(const Function &f, int stage_num) {
    if (stage_num == 0) {
        return f.definition();
    }
    internal_assert((int)f.updates().size() >= stage_num);
    return f.updates()[stage_num - 1];
}

/* Returns the required bounds of an intermediate stage (f, stage_num) of
 * function f given the bounds of the pure dimensions of f. */
DimBounds get_stage_bounds(Function f, int stage_num,
                           const DimBounds &pure_bounds) {
    DimBounds bounds;
    Definition def = get_stage_definition(f, stage_num);

    // Assumes that the domain of the pure vars across all the update
    // definitions is the same. This may not be true and can result in
    // over estimation of the extent.
    for (auto &b : pure_bounds) {
        bounds[b.first] = b.second;
    }

    for (auto &rvar : def.schedule().rvars()) {
        Expr lower = SubstituteVarEstimates().mutate(rvar.min);
        Expr upper = SubstituteVarEstimates().mutate(rvar.min + rvar.extent - 1);
        Interval simple_bounds = Interval(rvar.min, simplify(rvar.min + rvar.extent - 1));
        bounds[rvar.var] = Interval(simplify(lower),simplify(upper));
    }

    return bounds;
}

/* Returns the required bounds for all the stages of the function f. Each entry
 * in the return vector corresponds to a stage.*/
vector<DimBounds> get_stage_bounds(Function f, const DimBounds &pure_bounds) {
    vector<DimBounds> stage_bounds;
    size_t num_stages = f.updates().size() + 1;
    for (size_t s = 0; s < num_stages; s++) {
        stage_bounds.push_back(get_stage_bounds(f, s, pure_bounds));
    }
    return stage_bounds;
}

 /** Recursively inlines all the functions in the set inlines into the
  * expression e and returns the resulting expression. */
Expr perform_inline(Expr e, const map<string, Function> &env,
                    const set<string> &inlines) {
    if (inlines.empty()) {
        return e;
    }

    bool funcs_to_inline = false;
    Expr inlined_expr = e;

    do {
        funcs_to_inline = false;
        // Find all the function calls in the current expression.
        FindAllCalls find;
        inlined_expr.accept(&find);
        set<string> &calls = find.funcs_called;
        // Check if any of the calls are in the set of functions to be inlined.
        for (auto &call : calls) {
            if (inlines.find(call) != inlines.end()) {
                Function prod_func = env.at(call);
                // Impure functions cannot be inlined.
                internal_assert(prod_func.is_pure());
                // Inline the function call and set the flag to check for
                // further inlining opportunities.
                inlined_expr = inline_function(inlined_expr, prod_func);
                funcs_to_inline = true;
                break;
            }
        }
    } while (funcs_to_inline);

    return inlined_expr;
}

set<string> get_parents(Function f, int stage) {
    set<string> parents;
    if (f.has_extern_definition()) {
        internal_assert(stage == 0);
        for (const ExternFuncArgument &arg : f.extern_arguments()) {
            if (arg.is_func()) {
                string prod_name = Function(arg.func).name();
                parents.insert(prod_name);
            } else if (arg.is_expr()) {
                FindAllCalls find;
                arg.expr.accept(&find);
                parents.insert(find.funcs_called.begin(),
                               find.funcs_called.end());
            } else if (arg.is_image_param() || arg.is_buffer()) {
                BufferPtr buf;
                if (arg.is_image_param()) {
                    buf = arg.image_param.get_buffer();
                } else {
                    buf = arg.buffer;
                }
                parents.insert(buf.name());
            }
        }
    } else {
        FindAllCalls find;
        Definition def = get_stage_definition(f, stage);
        def.accept(&find);
        parents.insert(find.funcs_called.begin(), find.funcs_called.end());
    }
    return parents;
}

void disp_regions(const map<string, Box> &regions, int dlevel) {
    for (auto &reg : regions) {
        debug(dlevel) << reg.first;
        debug(dlevel) << reg.second;
        debug(dlevel) << "\n";
    }
}

RegionCosts::RegionCosts(const map<string, Function> &_env) : env(_env) {
    for (auto &kv : env) {
        // Pre-compute the function costs without any inlining.
        func_cost[kv.first] = get_func_cost(kv.second);

        FindImageInputs find;
        kv.second.accept(&find);
        // Get the types of all the image inputs to the pipeline.
        for (auto &in : find.input_type) {
            inputs[in.first] = in.second;
        }
    }
}

Cost RegionCosts::stage_region_cost(string func, int stage,
                                    DimBounds &bounds, const set<string> &inlines) {
    Function curr_f = env.at(func);
    Definition def = get_stage_definition(curr_f, stage);

    Box stage_region;

    const vector<Dim> &dims = def.schedule().dims();
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        stage_region.push_back(bounds.at(dims[d].var));
    }

    int64_t size = box_size(stage_region);
    if (size == unknown) {
        // Size could not be determined therefore it is not possible to
        // determine the arithmetic and memory costs.
        return Cost(unknown, unknown);
    }

    // If there is nothing to be inlined use the pre-computed function cost.
    vector<Cost> costs =
            inlines.empty() ? func_cost[func] : get_func_cost(curr_f, inlines);

    return Cost(size * costs[stage].arith, size * costs[stage].memory);
}

Cost RegionCosts::stage_region_cost(string func, int stage, Box &region,
                                    const set<string> &inlines) {
    Function curr_f = env.at(func);
    Definition def = get_stage_definition(curr_f, stage);

    DimBounds pure_bounds;
    const vector<string> &args = curr_f.args();
    for (size_t d = 0; d < args.size(); d++) {
        pure_bounds[args[d]] = region[d];
    }

    DimBounds stage_bounds = get_stage_bounds(curr_f, stage, pure_bounds);

    // TODO: Remove code duplication. The exact same code is present in
    // the other stage_region_cost.
    Box stage_region;

    const vector<Dim> &dims = def.schedule().dims();
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        stage_region.push_back(stage_bounds.at(dims[d].var));
    }

    int64_t size = box_size(stage_region);
    if (size == unknown) {
        // Size could not be determined therefore it is not possible to
        // determine the arithmetic and memory costs.
        return Cost(unknown, unknown);
    }

    vector<Cost> costs =
            inlines.empty() ? func_cost.at(func) : get_func_cost(curr_f, inlines);

    return Cost(size * costs.at(stage).arith, size * costs.at(stage).memory);
}

Cost RegionCosts::region_cost(string func, Box &region, const set<string> &inlines) {
    Function curr_f = env.at(func);
    Cost region_cost(0, 0);

    int num_stages = curr_f.updates().size() + 1;
    for (int s = 0; s < num_stages; s++) {
        Cost stage_cost = stage_region_cost(func, s, region, inlines);

        if (stage_cost.arith == unknown) {
            return Cost(unknown, unknown);
        } else {
            region_cost.arith += stage_cost.arith;
            region_cost.memory += stage_cost.memory;
        }
    }

    internal_assert(region_cost.arith != unknown && region_cost.memory != unknown);
    return region_cost;
}

Cost RegionCosts::region_cost(map<string, Box> &regions, const set<string> &inlines){
    Cost total_cost(0, 0);
    for (auto &f : regions) {
        // The cost for pure inlined functions will be accounted in the
        // consumer of the inlined function so they should be skipped.
        if (inlines.find(f.first) != inlines.end()) {
            internal_assert(env.at(f.first).is_pure());
            continue;
        }

        Cost cost = region_cost(f.first, f.second, inlines);
        if (cost.arith == unknown) {
            return Cost(unknown, unknown);
        } else {
            total_cost.arith += cost.arith;
            total_cost.memory += cost.memory;
        }
    }

    internal_assert(total_cost.arith != unknown && total_cost.memory != unknown);
    return total_cost;
}

/* Helper class for only accounting for the likely portion of the expression in
 * the case of max, min, and select. This will help costing functions with
 * boundary conditions better. The likely intrinsic triggers loop partitioning
 * and on average (steady stage) the cost of the expression will be equivalent
 * to the likely portion. */
class LikelyExpression : public IRMutator {
public:
    using IRMutator::mutate;
    using IRMutator::visit;

    void visit(const Min *op) {
        Expr new_a = mutate(op->a);
        Expr new_b = mutate(op->b);

        const Call *call_a = (new_a).as<Call>();
        const Call *call_b = (new_b).as<Call>();
        if (call_a && call_a->name == "likely") {
            expr = new_a;
        } else if (call_b && call_b->name == "likely") {
            expr = new_b;
        } else {
            expr = Min::make(new_a, new_b);
        }
    }

    void visit(const Max *op) {
        Expr new_a = mutate(op->a);
        Expr new_b = mutate(op->b);

        const Call *call_a = (new_a).as<Call>();
        const Call *call_b = (new_b).as<Call>();
        if (call_a && call_a->name == "likely") {
            expr = new_a;
        } else if (call_b && call_b->name == "likely") {
            expr = new_b;
        } else {
            expr = Max::make(new_a, new_b);
        }
    }
};

map<string, int64_t>
RegionCosts::stage_detailed_load_costs(string func, int stage,
                                       const set<string> &inlines) {
    map<string, int64_t> load_costs;
    Function curr_f = env.at(func);
    Definition def = get_stage_definition(curr_f, stage);

    for (auto &e : def.values()) {
        Expr inlined_expr = perform_inline(e, env, inlines);
        // TODO: Handle likely.
        //inlined_expr = LikelyExpression().mutate(inlined_expr);
        inlined_expr = simplify(inlined_expr);

        ExprCost cost_visitor;
        inlined_expr.accept(&cost_visitor);
        const map<string, int64_t> &expr_load_costs =
                        cost_visitor.detailed_byte_loads;

        combine_load_costs(load_costs, expr_load_costs);

        if (load_costs.find(func) == load_costs.end()) {
            load_costs[func] = e.type().bytes();
        } else {
            load_costs[func] += e.type().bytes();
        }
    }

    return load_costs;
}

map<string, int64_t>
RegionCosts::stage_detailed_load_costs(string func, int stage,
                                       DimBounds &bounds,
                                       const set<string> &inlines) {
    Function curr_f = env.at(func);
    Definition def = get_stage_definition(curr_f, stage);

    Box stage_region;

    const vector<Dim> &dims = def.schedule().dims();
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        stage_region.push_back(bounds.at(dims[d].var));
    }

    map<string, int64_t> load_costs =
            stage_detailed_load_costs(func, stage, inlines);

    int64_t size = box_size(stage_region);
    for (auto &kv : load_costs) {
        if (size != unknown) {
            load_costs[kv.first] *= size;
        } else {
            load_costs[kv.first] = unknown;
        }
    }

    return load_costs;
}

map<string, int64_t>
RegionCosts::detailed_load_costs(string func, const Box &region,
                                 const set<string> &inlines) {
    Function curr_f = env.at(func);
    map<string, int64_t> load_costs;

    int num_stages = curr_f.updates().size() + 1;

    DimBounds pure_bounds;
    const vector<string> &args = curr_f.args();
    for (size_t d = 0; d < args.size(); d++) {
        pure_bounds[args[d]] = region[d];
    }

    vector<DimBounds> stage_bounds = get_stage_bounds(curr_f, pure_bounds);

    for (int s = 0; s < num_stages; s++) {
        map<string, int64_t> stage_load_costs =
                        stage_detailed_load_costs(func, s, inlines);

        Definition def = get_stage_definition(curr_f, s);

        Box stage_region;

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            stage_region.push_back(stage_bounds[s].at(dims[d].var));
        }

        int64_t size = box_size(stage_region);

        for (auto &kv : stage_load_costs) {
            if (size != unknown) {
                stage_load_costs[kv.first] *= size;
            } else {
                stage_load_costs[kv.first] = unknown;
            }
        }

        combine_load_costs(load_costs, stage_load_costs);
    }

    return load_costs;
}

map<string, int64_t>
RegionCosts::detailed_load_costs(const map<string, Box> &regions,
                                 const set<string> &inlines) {
    map<string, int64_t> load_costs;
    for (auto &r : regions) {
        // The cost for pure inlined functions will be accounted in the
        // consumer of the inlined function so they should be skipped.
        if (inlines.find(r.first) != inlines.end()) {
            internal_assert(env.at(r.first).is_pure());
            continue;
        }

        map<string, int64_t> partial_load_costs =
                detailed_load_costs(r.first, r.second, inlines);

        combine_load_costs(load_costs, partial_load_costs);
    }

    return load_costs;
}

vector<Cost> RegionCosts::get_func_cost(const Function &f,
                                        const set<string> &inlines) {
    if (f.has_extern_definition()) {
        return { Cost(unknown, unknown) };
    }
    vector<Cost> func_costs;
    Cost pure_cost(0, 0);
    for (auto &e : f.values()) {
        Expr inlined_expr = perform_inline(e, env, inlines);
        // TODO: Handle likely.
        //inlined_expr = LikelyExpression().mutate(inlined_expr);
        inlined_expr = simplify(inlined_expr);

        ExprCost cost_visitor;
        inlined_expr.accept(&cost_visitor);
        pure_cost.arith += cost_visitor.cost.arith;
        pure_cost.memory += cost_visitor.cost.memory;

        // Accounting for the store
        pure_cost.memory += e.type().bytes();
        pure_cost.arith += 1;
    }

    func_costs.push_back(pure_cost);

    // Estimating cost when reductions are involved
    if (!f.is_pure()) {
        for (const Definition &u : f.updates()) {
            Cost def_cost(0, 0);
            for (auto &e : u.values()) {
                Expr inlined_expr = perform_inline(e, env, inlines);
                // TODO: Handle likely.
                //inlined_expr = LikelyExpression().mutate(e);
                inlined_expr = simplify(inlined_expr);

                ExprCost cost_visitor;
                inlined_expr.accept(&cost_visitor);
                def_cost.arith += cost_visitor.cost.arith;
                def_cost.memory += cost_visitor.cost.memory;

                // Accounting for the store
                def_cost.memory += e.type().bytes();
                def_cost.arith += 1;
            }

            for (auto &arg : u.args()) {
                Expr inlined_arg = perform_inline(arg, env, inlines);
                // TODO: Handle likely.
                //inlined_arg = LikelyExpression().mutate(inlined_arg);
                inlined_arg = simplify(inlined_arg);

                ExprCost cost_visitor;
                inlined_arg.accept(&cost_visitor);
                def_cost.arith += cost_visitor.cost.arith;
                def_cost.memory += cost_visitor.cost.memory;
            }

            func_costs.push_back(def_cost);
        }
    }
    return func_costs;
}

int64_t RegionCosts::region_size(string func, const Box &region) {
    const Function &f = env.at(func);
    int64_t size = box_size(region);
    if (size == unknown) {
        return unknown;
    }
    int64_t size_per_ele = get_func_value_size(f);
    return size * size_per_ele;
}

int64_t RegionCosts::region_footprint(const map<string, Box> &regions,
                                      const set<string> &inlined) {
    map<string, int> num_consumers;
    for (auto &f : regions) {
        num_consumers[f.first] = 0;
    }

    for (auto &f : regions) {
        map<string, Function> prods = find_direct_calls(env.at(f.first));
        for (auto &p : prods) {
            if (regions.find(p.first) != regions.end())
                num_consumers[p.first] += 1;
        }
    }

    vector<Function> outs;
    for (auto &f : num_consumers) {
        if (f.second == 0) {
            outs.push_back(env.at(f.first));
        }
    }

    // Realization order
    vector<string> order = realization_order(outs, env);

    int64_t working_set_size = 0;
    int64_t curr_size = 0;

    map<string, int64_t> func_sizes;

    for (auto &f : regions) {
        // Inlined functions do not have allocations
        bool is_inlined = inlined.find(f.first) != inlined.end();
        int64_t size = is_inlined? 0: region_size(f.first, f.second);
        if (size == unknown) {
            return unknown;
        } else {
            func_sizes[f.first] = size;
        }
    }

    for (auto &f : order) {
        if (regions.find(f) != regions.end()) {
            curr_size += func_sizes.at(f);
        }
        working_set_size = std::max(curr_size, working_set_size);
        map<string, Function> prods = find_direct_calls(env.at(f));
        for (auto &p : prods) {
            if (num_consumers.find(p.first) != num_consumers.end()) {
                num_consumers[p.first] -= 1;
                if (num_consumers[p.first] == 0) {
                    curr_size -= func_sizes.at(p.first);
                    internal_assert(curr_size >= 0);
                }
            }
        }
    }

    return working_set_size;
}

int64_t RegionCosts::input_region_size(string input, const Box &region) {
    int64_t size = box_size(region);
    if (size == unknown) {
        return unknown;
    }
    int64_t size_per_ele = inputs.at(input).bytes();
    return size * size_per_ele;
}

int64_t RegionCosts::input_region_size(const map<string, Box> &input_regions) {
    int64_t total_size = 0;
    for (auto &reg : input_regions) {
        int64_t size = input_region_size(reg.first, reg.second);
        if (size == unknown) {
            return unknown;
        } else {
            total_size += size;
        }
    }
    return total_size;
}

void RegionCosts::disp_func_costs() {
    debug(debug_level) << "===========================" << '\n';
    debug(debug_level) << "Pipeline per element costs:" << '\n';
    debug(debug_level) << "===========================" << '\n';
    for (auto &kv : env) {
        int stage = 0;
        for (auto &cost : func_cost[kv.first]) {
            Definition def = get_stage_definition(kv.second, stage);
            for (auto &e : def.values()) {
                debug(debug_level) << simplify(e) << '\n';
            }
            debug(debug_level) << "(" << kv.first << "," << stage << ")" <<
                     "(" << cost.arith << "," << cost.memory << ")" << '\n';
            stage++;
        }
    }
    debug(debug_level) << "===========================" << '\n';
}

}
}
