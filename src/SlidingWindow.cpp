#include "SlidingWindow.h"

#include "Bounds.h"
#include "CompilerLogger.h"
#include "Debug.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Monotonic.h"
#include "Scope.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include <list>
#include <set>
#include <utility>

namespace Halide {
namespace Internal {

using std::list;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

// Does an expression depend on a particular variable?
class ExprDependsOnVar : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (op->name == var) {
            result = true;
        }
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        // The name might be hidden within the body of the let, in
        // which case there's no point descending.
        if (op->name != var) {
            op->body.accept(this);
        }
    }

public:
    bool result = false;
    string var;

    ExprDependsOnVar(string v)
        : var(std::move(v)) {
    }
};

bool expr_depends_on_var(const Expr &e, string v) {
    ExprDependsOnVar depends(std::move(v));
    e.accept(&depends);
    return depends.result;
}

class ExpandExpr : public IRMutator {
    using IRMutator::visit;
    const Scope<Expr> &scope;

    Expr visit(const Variable *var) override {
        if (scope.contains(var->name)) {
            Expr expr = scope.get(var->name);
            debug(4) << "Fully expanded " << var->name << " -> " << expr << "\n";
            return expr;
        } else {
            return var;
        }
    }

public:
    ExpandExpr(const Scope<Expr> &s)
        : scope(s) {
    }
};

// Perform all the substitutions in a scope
Expr expand_expr(const Expr &e, const Scope<Expr> &scope) {
    ExpandExpr ee(scope);
    Expr result = ee.mutate(e);
    debug(4) << "Expanded " << e << " into " << result << "\n";
    return result;
}

class FindProduce : public IRVisitor {
    const string &func;

    using IRVisitor::visit;

    void visit(const ProducerConsumer *op) override {
        if (op->is_producer && op->name == func) {
            found = true;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    bool found = false;

    FindProduce(const string &func)
        : func(func) {
    }
};

bool find_produce(const Stmt &s, const string &func) {
    FindProduce finder(func);
    s.accept(&finder);
    return finder.found;
}

// This mutator rewrites calls and provides to a particular
// func:
// - Calls and Provides are shifted to be relative to the min.
// - Provides additionally are rewritten to load values from the
//   previous iteration of the loop if they were computed in the
//   last iteration.
class RollFunc : public IRMutator {
    const Function &func;
    int dim;
    const string &loop_var;
    const Interval &old_bounds;
    const Interval &new_bounds;

    Scope<Expr> scope;

    // It helps simplify the shifted calls/provides to rebase the
    // loops that are subtracted from to have a min of 0.
    set<string> loops_to_rebase;
    bool in_produce = false;

    using IRMutator::visit;

    Stmt visit(const ProducerConsumer *op) override {
        bool produce_func = op->name == func.name() && op->is_producer;
        ScopedValue<bool> old_in_produce(in_produce, in_produce || produce_func);
        return IRMutator::visit(op);
    }

    Stmt visit(const Provide *op) override {
        if (!(in_produce && op->name == func.name())) {
            return IRMutator::visit(op);
        }
        vector<Expr> values = op->values;
        for (Expr &i : values) {
            i = mutate(i);
        }
        vector<Expr> args = op->args;
        for (Expr &i : args) {
            i = mutate(i);
        }
        bool sliding_up = old_bounds.max.same_as(new_bounds.max);
        Expr is_new = sliding_up ? new_bounds.min <= args[dim] : args[dim] <= new_bounds.max;
        args[dim] -= old_bounds.min;
        vector<Expr> old_args = args;
        Expr old_arg_dim = expand_expr(old_args[dim], scope);
        old_args[dim] = substitute(loop_var, Variable::make(Int(32), loop_var) - 1, old_arg_dim);
        for (int i = 0; i < (int)values.size(); i++) {
            Type t = values[i].type();
            Expr old_value =
                Call::make(t, op->name, old_args, Call::Halide, func.get_contents(), i);
            values[i] = Call::make(values[i].type(), Call::if_then_else, {is_new, values[i], likely(old_value)}, Call::PureIntrinsic);
        }
        if (const Variable *v = op->args[dim].as<Variable>()) {
            // The subtractions above simplify more easily if the loop is rebased to 0.
            loops_to_rebase.insert(v->name);
        }
        return Provide::make(func.name(), values, args, op->predicate);
    }

    Expr visit(const Call *op) override {
        if (!(op->call_type == Call::Halide && op->name == func.name())) {
            return IRMutator::visit(op);
        }
        vector<Expr> args = op->args;
        for (Expr &i : args) {
            i = mutate(i);
        }
        args[dim] -= old_bounds.min;
        return Call::make(op->type, op->name, args, Call::Halide, op->func, op->value_index, op->image, op->param);
    }

    Stmt visit(const For *op) override {
        Stmt result = IRMutator::visit(op);
        op = result.as<For>();
        internal_assert(op);
        if (loops_to_rebase.count(op->name)) {
            string new_name = op->name + ".rebased";
            Stmt body = substitute(op->name, Variable::make(Int(32), new_name) + op->min, op->body);
            // use op->name *before* the re-assignment of result, which will clobber it
            loops_to_rebase.erase(op->name);
            result = For::make(new_name, 0, op->extent, op->for_type, op->partition_policy, op->device_api, body);
        }
        return result;
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<Expr> bind(scope, op->name, simplify(expand_expr(op->value, scope)));
        return IRMutator::visit(op);
    }

public:
    RollFunc(const Function &func, int dim, const string &loop_var,
             const Interval &old_bounds, const Interval &new_bounds)
        : func(func), dim(dim), loop_var(loop_var), old_bounds(old_bounds), new_bounds(new_bounds) {
    }
};

// Perform sliding window optimization for a function over a
// particular serial for loop
class SlidingWindowOnFunctionAndLoop : public IRMutator {
    Function func;
    string loop_var;
    Expr loop_min;
    set<int> &slid_dimensions;
    Scope<Expr> scope;

    // Loops between the loop being slid over and the produce node
    Scope<> enclosing_loops;

    map<string, Expr> replacements;

    using IRMutator::visit;

    // Check if the dimension at index 'dim_idx' is always pure (i.e. equal to 'dim')
    // in the definition (including in its specializations)
    bool is_dim_always_pure(const Definition &def, const string &dim, int dim_idx) {
        const Variable *var = def.args()[dim_idx].as<Variable>();
        if ((!var) || (var->name != dim)) {
            return false;
        }

        for (const auto &s : def.specializations()) {
            bool pure = is_dim_always_pure(s.definition, dim, dim_idx);
            if (!pure) {
                return false;
            }
        }
        return true;
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            if (op->name != func.name()) {
                return IRMutator::visit(op);
            }

            // We're interested in the case where exactly one of the
            // dimensions of the buffer has a min/extent that depends
            // on the loop_var.
            string dim = "";
            int dim_idx = 0;
            Expr min_required, max_required;

            debug(3) << "Considering sliding " << func.name()
                     << " along loop variable " << loop_var << "\n"
                     << "Region provided:\n";

            string prefix = func.name() + ".s" + std::to_string(func.updates().size()) + ".";
            const std::vector<string> func_args = func.args();
            for (int i = 0; i < func.dimensions(); i++) {
                // Look up the region required of this function's last stage
                string var = prefix + func_args[i];
                internal_assert(scope.contains(var + ".min") && scope.contains(var + ".max"));
                Expr min_req = scope.get(var + ".min");
                Expr max_req = scope.get(var + ".max");
                min_req = expand_expr(min_req, scope);
                max_req = expand_expr(max_req, scope);

                debug(3) << func_args[i] << ":" << min_req << ", " << max_req << "\n";
                if (expr_depends_on_var(min_req, loop_var) ||
                    expr_depends_on_var(max_req, loop_var)) {
                    if (!dim.empty()) {
                        dim = "";
                        min_required = Expr();
                        max_required = Expr();
                        break;
                    } else {
                        dim = func_args[i];
                        dim_idx = i;
                        min_required = min_req;
                        max_required = max_req;
                    }
                } else if (!min_required.defined() &&
                           i == func.dimensions() - 1 &&
                           is_pure(min_req) &&
                           is_pure(max_req)) {
                    // The footprint doesn't depend on the loop var. Just compute everything on the first loop iteration.
                    dim = func_args[i];
                    dim_idx = i;
                    min_required = min_req;
                    max_required = max_req;
                }
            }

            if (!dim.empty() && slid_dimensions.count(dim_idx)) {
                debug(1) << "Already slid over dimension " << dim_idx << ", so skipping it.\n";
                dim = "";
                min_required = Expr();
                max_required = Expr();
            }
            if (!min_required.defined()) {
                debug(3) << "Could not perform sliding window optimization of "
                         << func.name() << " over " << loop_var << " because multiple "
                         << "dimensions of the function dependended on the loop var\n";
                return op;
            }

            // If the function is not pure in the given dimension, give up. We also
            // need to make sure that it is pure in all the specializations
            bool pure = true;
            for (const Definition &def : func.updates()) {
                pure = is_dim_always_pure(def, dim, dim_idx);
                if (!pure) {
                    break;
                }
            }
            if (!pure) {
                debug(3) << "Could not performance sliding window optimization of "
                         << func.name() << " over " << loop_var << " because the function "
                         << "scatters along the related axis.\n";
                return op;
            }

            bool can_slide_up = false;
            bool can_slide_down = false;

            Monotonic monotonic_min = is_monotonic(min_required, loop_var);
            Monotonic monotonic_max = is_monotonic(max_required, loop_var);

            if (monotonic_min == Monotonic::Increasing ||
                monotonic_min == Monotonic::Constant) {
                can_slide_up = true;
            } else if (monotonic_min == Monotonic::Unknown) {
                if (get_compiler_logger()) {
                    get_compiler_logger()->record_non_monotonic_loop_var(loop_var, min_required);
                }
            }

            if (monotonic_max == Monotonic::Decreasing ||
                monotonic_max == Monotonic::Constant) {
                can_slide_down = true;
            } else if (monotonic_max == Monotonic::Unknown) {
                if (get_compiler_logger()) {
                    get_compiler_logger()->record_non_monotonic_loop_var(loop_var, max_required);
                }
            }

            if (!can_slide_up && !can_slide_down) {
                debug(3) << "Not sliding " << func.name()
                         << " over dimension " << dim
                         << " along loop variable " << loop_var
                         << " because I couldn't prove it moved monotonically along that dimension\n"
                         << "Min is " << min_required << "\n"
                         << "Max is " << max_required << "\n";
                return op;
            }

            // Ok, we've isolated a function, a dimension to slide
            // along, and loop variable to slide over.
            debug(3) << "Sliding " << func.name()
                     << " over dimension " << dim
                     << " along loop variable " << loop_var << "\n";

            Expr loop_var_expr = Variable::make(Int(32), loop_var);

            Expr prev_max_plus_one = substitute(loop_var, loop_var_expr - 1, max_required) + 1;
            Expr prev_min_minus_one = substitute(loop_var, loop_var_expr - 1, min_required) - 1;

            // If there's no overlap between adjacent iterations, we shouldn't slide.
            if (can_prove(min_required >= prev_max_plus_one) ||
                can_prove(max_required <= prev_min_minus_one)) {
                debug(3) << "Not sliding " << func.name()
                         << " over dimension " << dim
                         << " along loop variable " << loop_var
                         << " there's no overlap in the region computed across iterations\n"
                         << "Min is " << min_required << "\n"
                         << "Max is " << max_required << "\n";
                return op;
            }

            // Update the bounds of this producer assuming the previous iteration
            // has run already.
            Expr new_min, new_max;
            if (can_slide_up) {
                new_min = prev_max_plus_one;
                new_max = max_required;
            } else {
                new_min = min_required;
                new_max = prev_min_minus_one;
            }

            // See if we can find a new min for the loop that can warm up the
            // sliding window. We're going to do this by building an equation
            // that describes the constraints we have on our new loop min. The
            // first constraint is that the new loop min is not after the
            // loop min.
            string new_loop_min_name = unique_name('x');
            Expr new_loop_min_var = Variable::make(Int(32), new_loop_min_name);
            Expr new_loop_min_eq = new_loop_min_var <= loop_min;
            Expr new_min_at_new_loop_min = substitute(loop_var, new_loop_min_var, new_min);
            Expr new_max_at_new_loop_min = substitute(loop_var, new_loop_min_var, new_max);
            if (can_slide_up) {
                // We need to find a new loop min that satisfies these constraints:
                // - The new min at the new loop min needs to be before the min
                //   required at the original min.
                // - The new max needs to be greater than the new min, both at the
                //   new loop min. This guarantees that the sliding window.
                // Together, these conditions guarantee the sliding window is warmed
                // up. The first condition checks that we reached the original loop
                // min, and the second condition checks that the iterations before
                // the original min weren't empty.
                Expr min_required_at_loop_min = substitute(loop_var, loop_min, min_required);
                new_loop_min_eq = new_loop_min_eq &&
                                  new_min_at_new_loop_min <= min_required_at_loop_min &&
                                  new_max_at_new_loop_min >= new_min_at_new_loop_min;
            } else {
                // When sliding down, the constraints are similar, just swapping
                // the roles of the min and max.
                Expr max_required_at_loop_min = substitute(loop_var, loop_min, max_required);
                new_loop_min_eq = new_loop_min_eq &&
                                  new_max_at_new_loop_min >= max_required_at_loop_min &&
                                  new_min_at_new_loop_min <= new_max_at_new_loop_min;
            }
            // Try to solve the equation.
            new_loop_min_eq = simplify(new_loop_min_eq);
            Interval solve_result = solve_for_inner_interval(new_loop_min_eq, new_loop_min_name);
            internal_assert(!new_loop_min.defined());
            if (solve_result.has_upper_bound() &&
                !equal(solve_result.max, loop_min) &&
                !expr_uses_vars(solve_result.max, enclosing_loops)) {
                new_loop_min = simplify(solve_result.max);

                // We have a new loop min, so we an assume every iteration has
                // a previous iteration. In order for this to be safe, we need
                // the new min/max at the new loop min to be less than or equal to
                // the min/max required at the original loop min.
                Expr loop_var_expr = Variable::make(Int(32), loop_var);
                Expr orig_loop_min_expr = Variable::make(Int(32), loop_var + ".loop_min.orig");
                if (can_slide_up) {
                    Expr min_required_at_loop_min = substitute(loop_var, orig_loop_min_expr, min_required);
                    new_min = max(new_min, min_required_at_loop_min);
                } else {
                    Expr max_required_at_loop_min = substitute(loop_var, orig_loop_min_expr, max_required);
                    new_max = min(new_max, max_required_at_loop_min);
                }
            } else {
                // We couldn't find a suitable new loop min, we can't assume
                // every iteration has a previous iteration. The first iteration
                // will warm up the loop.
                Expr need_explicit_warmup = loop_var_expr <= loop_min;
                if (can_slide_up) {
                    new_min = select(need_explicit_warmup, min_required, likely_if_innermost(new_min));
                } else {
                    new_max = select(need_explicit_warmup, max_required, likely_if_innermost(new_max));
                }
            }
            new_min = simplify(new_min);
            new_max = simplify(new_max);

            debug(3) << "Sliding " << func.name() << ", " << dim << "\n"
                     << "Pushing min up from " << min_required << " to " << new_min << "\n"
                     << "Shrinking max from " << max_required << " to " << new_max << "\n"
                     << "Adjusting loop_min from " << loop_min << " to " << new_loop_min << "\n"
                     << "Equation is " << new_loop_min_eq << "\n";

            slid_dimensions.insert(dim_idx);

            // If we want to slide in registers, we're done here, we just need to
            // save the updated bounds for later.
            if (func.schedule().memory_type() == MemoryType::Register) {
                this->dim_idx = dim_idx;
                old_bounds = {min_required, max_required};
                new_bounds = {new_min, new_max};
                return op;
            }

            // If we aren't sliding in registers, we need to update the bounds of
            // the producer to be only the bounds of the region newly computed.
            internal_assert(replacements.empty());
            if (can_slide_up) {
                replacements[prefix + dim + ".min"] = new_min;
            } else {
                replacements[prefix + dim + ".max"] = new_max;
            }

            for (size_t i = 0; i < func.updates().size(); i++) {
                string n = func.name() + ".s" + std::to_string(i) + "." + dim;
                replacements[n + ".min"] = Variable::make(Int(32), prefix + dim + ".min");
                replacements[n + ".max"] = Variable::make(Int(32), prefix + dim + ".max");
            }

            // Ok, we have a new min/max required and we're going to
            // rewrite all the lets that define bounds required. Now
            // we need to additionally expand the bounds required of
            // the last stage to cover values produced by stages
            // before the last one. Because, e.g., an intermediate
            // stage may be unrolled, expanding its bounds provided.
            Stmt result = op;
            if (!func.updates().empty()) {
                Box b = box_provided(op->body, func.name());
                if (can_slide_up) {
                    string n = prefix + dim + ".min";
                    Expr var = Variable::make(Int(32), n);
                    result = LetStmt::make(n, min(var, b[dim_idx].min), result);
                } else {
                    string n = prefix + dim + ".max";
                    Expr var = Variable::make(Int(32), n);
                    result = LetStmt::make(n, max(var, b[dim_idx].max), result);
                }
            }

            return result;
        } else if (!find_produce(op, func.name()) && new_loop_min.defined()) {
            // The producer might have expanded the loop before the min to warm
            // up the window. This consumer doesn't contain a producer that might
            // be part of the warmup, so guard it with an if to only run it on
            // the original loop bounds.
            Expr loop_var_expr = Variable::make(Int(32), loop_var);
            Expr orig_loop_min_expr = Variable::make(Int(32), loop_var + ".loop_min.orig");
            Expr guard = likely_if_innermost(orig_loop_min_expr <= loop_var_expr);

            // Put the if inside the consumer node, so semaphores end up outside the if.
            // TODO: This is correct, but it produces slightly suboptimal code: if we
            // didn't do this, the loop could likely be trimmed and the if simplified away.
            Stmt body = mutate(op->body);
            if (const IfThenElse *old_guard = body.as<IfThenElse>()) {
                Expr x = Variable::make(Int(32), "*");
                vector<Expr> matches;
                if (expr_match(likely_if_innermost(x <= loop_var_expr), old_guard->condition, matches)) {
                    // There's already a condition on loop_var_expr here. Since we're
                    // adding a condition at the old loop min, this if must already be
                    // guarding more than we will.
                    guard = Expr();
                }
            }
            if (guard.defined()) {
                debug(3) << "Guarding body " << guard << "\n";
                body = IfThenElse::make(guard, body);
            }
            if (body.same_as(op->body)) {
                return op;
            } else {
                return ProducerConsumer::make_consume(op->name, body);
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const For *op) override {
        // It's not safe to enter an inner loop whose bounds depend on
        // the var we're sliding over.
        Expr min = expand_expr(op->min, scope);
        Expr extent = expand_expr(op->extent, scope);
        ScopedBinding<> bind(enclosing_loops, op->name);
        if (is_const_one(extent)) {
            // Just treat it like a let
            Stmt s = LetStmt::make(op->name, min, op->body);
            s = mutate(s);
            // Unpack it back into the for
            const LetStmt *l = s.as<LetStmt>();
            internal_assert(l);
            return For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api, l->body);
        } else if (is_monotonic(min, loop_var) != Monotonic::Constant ||
                   is_monotonic(extent, loop_var) != Monotonic::Constant) {
            debug(3) << "Not entering loop over " << op->name
                     << " because the bounds depend on the var we're sliding over: "
                     << min << ", " << extent << "\n";
            return op;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<Expr> bind(scope, op->name, simplify(expand_expr(op->value, scope)));
        Stmt new_body = mutate(op->body);

        Expr value = op->value;

        map<string, Expr>::iterator iter = replacements.find(op->name);
        if (iter != replacements.end()) {
            value = iter->second;
            replacements.erase(iter);
        }

        if (new_body.same_as(op->body) && value.same_as(op->value)) {
            return op;
        } else {
            return LetStmt::make(op->name, value, new_body);
        }
    }

public:
    SlidingWindowOnFunctionAndLoop(Function f, string v, Expr v_min, set<int> &slid_dimensions)
        : func(std::move(f)), loop_var(std::move(v)), loop_min(std::move(v_min)), slid_dimensions(slid_dimensions) {
    }

    Expr new_loop_min;
    int dim_idx;
    Interval old_bounds;
    Interval new_bounds;

    Stmt translate_loop(const Stmt &s) {
        return RollFunc(func, dim_idx, loop_var, old_bounds, new_bounds).mutate(s);
    }
};

// In Stmt s, does the production of b depend on a?
// We can't use produce/consume nodes to determine this, because they're "loose".
// For example, we get this:
//
//  produce a {
//   a(...) = ...
//  }
//  consume a {
//   produce b {
//    b(...) = ... // not depending on a
//   }
//   consume b {
//    c(...) = a(...) + b(...)
//   }
//  }
//
// When we'd rather see this:
//
//  produce a {
//   a(...) = ...
//  }
//  produce b {
//   b(...) = ... // not depending on a
//  }
//  consume a {
//   consume b {
//    c(...) = a(...) + b(...)
//   }
//  }
//
// TODO: We might also need to figure out transitive dependencies...? If so, it
// would be best to just fix the produce/consume relationships as above. We would
// just be able to look for produce b inside produce a.
class Dependencies : public IRVisitor {
    using IRVisitor::visit;

    const string &producer;
    bool in_producer = false;

    void visit(const ProducerConsumer *op) override {
        ScopedValue<bool> old_finding_a(in_producer, in_producer || (op->is_producer && op->name == producer));
        return IRVisitor::visit(op);
    }

    void visit(const Call *op) override {
        if (in_producer && op->call_type == Call::Halide) {
            if (op->name != producer) {
                dependencies.insert(op->name);
            }
        }
        IRVisitor::visit(op);
    }

public:
    set<string> dependencies;

    Dependencies(const string &producer)
        : producer(producer) {
    }
};

bool depends_on(const string &a, const string &b, const Stmt &s, map<string, bool> &cache) {
    if (a == b) {
        return true;
    }
    auto cached = cache.find(b);
    if (cached != cache.end()) {
        return cached->second;
    }
    Dependencies deps(b);
    s.accept(&deps);
    // Recursively search for dependencies.
    for (const string &i : deps.dependencies) {
        if (depends_on(a, i, s, cache)) {
            cache[b] = true;
            return true;
        }
    }
    cache[b] = false;
    return false;
}

bool depends_on(const string &a, const string &b, const Stmt &s) {
    map<string, bool> cache;
    return depends_on(a, b, s, cache);
}

// Update the loop variable referenced by prefetch directives.
class SubstitutePrefetchVar : public IRMutator {
    const string &old_var;
    const string &new_var;

    using IRMutator::visit;

    Stmt visit(const Prefetch *op) override {
        Stmt new_body = mutate(op->body);
        if (op->prefetch.at == old_var || op->prefetch.from == old_var) {
            PrefetchDirective p = op->prefetch;
            if (op->prefetch.at == old_var) {
                p.at = new_var;
            }
            if (op->prefetch.from == old_var) {
                p.from = new_var;
            }
            return Prefetch::make(op->name, op->types, op->bounds, p, op->condition, std::move(new_body));
        } else if (!new_body.same_as(op->body)) {
            return Prefetch::make(op->name, op->types, op->bounds, op->prefetch, op->condition, std::move(new_body));
        } else {
            return op;
        }
    }

public:
    SubstitutePrefetchVar(const string &old_var, const string &new_var)
        : old_var(old_var), new_var(new_var) {
    }
};

// Perform sliding window optimization for all functions
class SlidingWindow : public IRMutator {
    const map<string, Function> &env;

    // A map of which dimensions we've already slid over, by Func name.
    map<string, set<int>> slid_dimensions;

    // Keep track of realizations we want to slide, from innermost to
    // outermost.
    list<Function> sliding;

    using IRMutator::visit;

    Stmt visit(const Realize *op) override {
        // Find the args for this function
        map<string, Function>::const_iterator iter = env.find(op->name);

        // If it's not in the environment it's some anonymous
        // realization that we should skip (e.g. an inlined reduction)
        if (iter == env.end()) {
            return IRMutator::visit(op);
        }

        // If the Function in question has the same compute_at level
        // as its store_at level, skip it.
        const FuncSchedule &sched = iter->second.schedule();
        if (sched.compute_level() == sched.store_level()) {
            return IRMutator::visit(op);
        }

        // We want to slide innermost first, so put it on the front of
        // the list.
        sliding.push_front(iter->second);
        Stmt new_body = mutate(op->body);
        sliding.pop_front();
        // Remove tracking of slid dimensions when we're done realizing
        // it in case a realization appears elsewhere.
        auto slid_it = slid_dimensions.find(iter->second.name());
        if (slid_it != slid_dimensions.end()) {
            slid_dimensions.erase(slid_it);
        }

        if (new_body.same_as(op->body)) {
            return op;
        } else {
            return Realize::make(op->name, op->types, op->memory_type,
                                 op->bounds, op->condition, new_body);
        }
    }

    Stmt visit(const For *op) override {
        if (!(op->for_type == ForType::Serial || op->for_type == ForType::Unrolled)) {
            return IRMutator::visit(op);
        }
        debug(3) << "Doing sliding window analysis on loop " << op->name << "\n";

        string name = op->name;
        Stmt body = op->body;
        Expr loop_min = op->min;
        Expr loop_extent = op->extent;
        Expr loop_max = Variable::make(Int(32), op->name + ".loop_max");

        list<pair<string, Expr>> prev_loop_mins;
        list<pair<string, Expr>> new_lets;
        for (const Function &func : sliding) {
            debug(3) << "Doing sliding window analysis on function " << func.name() << "\n";

            // Figure out where we should start sliding from. If no
            // other func needs this func, we can just start at the
            // original loop min.
            Expr prev_loop_min = op->min;
            // If a previously slid func needs this func to be warmed
            // up, then we need to back up the loop to warm up this
            // func before the already slid func starts warming up.
            for (const auto &i : prev_loop_mins) {
                if (depends_on(func.name(), i.first, body)) {
                    prev_loop_min = i.second;
                    break;
                }
            }

            set<int> &slid_dims = slid_dimensions[func.name()];
            size_t old_slid_dims_size = slid_dims.size();
            SlidingWindowOnFunctionAndLoop slider(func, name, prev_loop_min, slid_dims);
            body = slider.mutate(body);

            if (func.schedule().memory_type() == MemoryType::Register &&
                slider.old_bounds.has_lower_bound()) {
                body = slider.translate_loop(body);
            }

            if (slider.new_loop_min.defined()) {
                Expr new_loop_min = slider.new_loop_min;
                if (!prev_loop_min.same_as(loop_min)) {
                    // If we didn't start sliding from the previous
                    // loop min, we the old loop min might already
                    // be further back than this new one.
                    new_loop_min = min(new_loop_min, loop_min);
                }

                // Put this at the front of the list, so we find it first
                // when checking subsequent funcs.
                prev_loop_mins.emplace_front(func.name(), new_loop_min);

                // Update the loop body to use the adjusted loop min.
                string new_name = name + ".$n";
                loop_min = Variable::make(Int(32), new_name + ".loop_min");
                loop_extent = Variable::make(Int(32), new_name + ".loop_extent");
                body = substitute({
                                      {name, Variable::make(Int(32), new_name)},
                                      {name + ".loop_min", loop_min},
                                      {name + ".loop_extent", loop_extent},
                                  },
                                  body);
                body = SubstitutePrefetchVar(name, new_name).mutate(body);

                name = new_name;

                // The new loop interval is the new loop min to the loop max.
                new_lets.emplace_front(name + ".loop_min", new_loop_min);
                new_lets.emplace_front(name + ".loop_min.orig", loop_min);
                new_lets.emplace_front(name + ".loop_extent", (loop_max - loop_min) + 1);
            }

            if (slid_dims.size() > old_slid_dims_size) {
                // Let storage folding know there's now a read-after-write hazard here
                Expr marker = Call::make(Int(32),
                                         Call::sliding_window_marker,
                                         {func.name(), Variable::make(Int(32), op->name)},
                                         Call::Intrinsic);
                body = Block::make(Evaluate::make(marker), body);
            }
        }

        body = mutate(body);

        if (body.same_as(op->body) && loop_min.same_as(op->min) && loop_extent.same_as(op->extent) && name == op->name) {
            return op;
        } else {
            Stmt result = For::make(name, loop_min, loop_extent, op->for_type, op->partition_policy, op->device_api, body);
            if (!new_lets.empty()) {
                result = LetStmt::make(name + ".loop_max", loop_max, result);
            }
            for (const auto &i : new_lets) {
                result = LetStmt::make(i.first, i.second, result);
            }
            return result;
        }
    }

    Stmt visit(const IfThenElse *op) override {
        // Don't let specializations corrupt the tracking of which
        // dimensions have been slid.
        map<string, set<int>> old_slid_dimensions = slid_dimensions;
        Stmt then_case = mutate(op->then_case);
        slid_dimensions = old_slid_dimensions;
        Stmt else_case = mutate(op->else_case);
        slid_dimensions = old_slid_dimensions;
        if (then_case.same_as(op->then_case) && else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(op->condition, then_case, else_case);
        }
    }

public:
    SlidingWindow(const map<string, Function> &e)
        : env(e) {
    }
};

// It is convenient to be able to assume that loops have a .loop_min.orig
// let in addition to .loop_min. Most of these will get simplified away.
class AddLoopMinOrig : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        Stmt body = mutate(op->body);
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);

        Stmt result;
        if (body.same_as(op->body) && min.same_as(op->min) && extent.same_as(op->extent)) {
            result = op;
        } else {
            result = For::make(op->name, min, extent, op->for_type, op->partition_policy, op->device_api, body);
        }
        return LetStmt::make(op->name + ".loop_min.orig", Variable::make(Int(32), op->name + ".loop_min"), result);
    }
};

}  // namespace

Stmt sliding_window(const Stmt &s, const map<string, Function> &env) {
    return SlidingWindow(env).mutate(AddLoopMinOrig().mutate(s));
}

}  // namespace Internal
}  // namespace Halide
