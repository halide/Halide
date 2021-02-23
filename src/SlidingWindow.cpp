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
    bool result;
    string var;

    ExprDependsOnVar(string v)
        : result(false), var(std::move(v)) {
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

// Insert bounds on a dimension of a producer with a new min or max, or both.
class GuardProducer : public IRMutator {
    const Function &func;
    int dim_idx;
    // These may be undefined, indicating there is no bound.
    const Expr &min;
    const Expr &max;

    using IRMutator::visit;

    Stmt visit(const Provide *op) override {
        if (op->name != func.name()) {
            return op;
        }
        internal_assert(dim_idx < (int)op->args.size());
        Expr var = op->args[dim_idx];
        Expr guard_below, guard_above;
        if (min.defined()) {
            guard_below = likely_if_innermost(min <= var);
        }
        if (max.defined()) {
            guard_above = likely_if_innermost(var <= max);
        }
        Expr guard;
        if (guard_below.defined() && guard_above.defined()) {
            guard = guard_below && guard_above;
        } else if (guard_below.defined()) {
            guard = guard_below;
        } else if (guard_above.defined()) {
            guard = guard_above;
        }

        // Help bounds inference understand the clamp from this guard if.
        internal_assert(dim_idx < (int)func.args().size());
        string bounded_var = func.args()[dim_idx] + ".clamped";
        Stmt provide = substitute(var, Variable::make(Int(32), bounded_var), op);
        provide = LetStmt::make(bounded_var, promise_clamped(var, min, max), provide);

        internal_assert(guard.defined());
        return IfThenElse::make(guard, provide);
    }

public:
    GuardProducer(const Function &func, int dim_idx, const Expr &min, const Expr &max)
        : func(func), dim_idx(dim_idx), min(min), max(max) {
    }
};

Stmt guard_producer(const Stmt &s, const Function &func, int dim_idx, const Expr &min, const Expr &max) {
    return GuardProducer(func, dim_idx, min, max).mutate(s);
}

// Perform sliding window optimization for a function over a
// particular serial for loop
class SlidingWindowOnFunctionAndLoop : public IRMutator {
    Function func;
    string loop_var;
    Expr loop_min;
    set<int> &slid_dimensions;
    Scope<Expr> scope;
    Scope<Interval> &bounds;

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
            Stmt stmt = op;

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
                if (slid_dimensions.count(i)) {
                    debug(3) << "Already slid over dimension " << i << ", so skipping it.\n";
                    continue;
                }
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

            if (!min_required.defined()) {
                debug(3) << "Could not perform sliding window optimization of "
                         << func.name() << " over " << loop_var << " because multiple "
                         << "dimensions of the function dependended on the loop var\n";
                return stmt;
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
                return stmt;
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
                return stmt;
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
                return stmt;
            }

            string new_loop_min_name = unique_name('x');
            Expr new_loop_min_var = Variable::make(Int(32), new_loop_min_name);
            Expr new_loop_min_eq;
            if (can_slide_up) {
                new_loop_min_eq =
                    (substitute(loop_var, loop_min, min_required) >=
                     substitute(loop_var, new_loop_min_var, prev_max_plus_one));
            } else {
                new_loop_min_eq =
                    (substitute(loop_var, loop_min, max_required) <=
                     substitute(loop_var, new_loop_min_var, prev_min_minus_one));
            }
            new_loop_min_eq = simplify(new_loop_min_eq, true, bounds);
            Interval solve_result = solve_for_inner_interval(new_loop_min_eq, new_loop_min_name);
            internal_assert(!new_loop_min.defined());
            if (solve_result.has_upper_bound() &&
                can_prove(solve_result.max <= loop_min, bounds)) {
                new_loop_min = solve_result.max;
            }

            Expr new_min, new_max;
            if (can_slide_up) {
                new_min = prev_max_plus_one;
                new_max = max_required;
            } else {
                new_min = min_required;
                new_max = prev_min_minus_one;
            }

            if (!new_loop_min.defined()) {
                // If we don't have a new loop min, we need to just compute the warmup on the
                // first iteration.
                Expr need_explicit_warmup = loop_var_expr <= loop_min;
                if (can_slide_up) {
                    new_min = select(need_explicit_warmup, min_required, likely_if_innermost(new_min));
                    new_min = simplify(new_min, true, bounds);
                } else {
                    new_max = select(need_explicit_warmup, max_required, likely_if_innermost(new_max));
                    new_max = simplify(new_max, true, bounds);
                }
            }

            Expr early_stages_min_required = new_min;
            Expr early_stages_max_required = new_max;

            debug(3) << "Sliding " << func.name() << ", " << dim << "\n"
                     << "Pushing min up from " << min_required << " to " << new_min << "\n"
                     << "Shrinking max from " << max_required << " to " << new_max << "\n"
                     << "Adjusting loop_min from " << loop_min << " to " << new_loop_min << "\n"
                     << "Equation is " << new_loop_min_eq << "\n";

            slid_dimensions.insert(dim_idx);

            if (new_loop_min.defined()) {
                // Guard producers against running on expanded bounds.
                Expr orig_loop_min_expr = Variable::make(Int(32), loop_var + ".loop_min.orig");
                Expr produce_min, produce_max;
                if (can_slide_up) {
                    produce_min = substitute(loop_var, orig_loop_min_expr, min_required);
                } else {
                    produce_max = substitute(loop_var, orig_loop_min_expr, max_required);
                }
                debug(3) << "Guarding producer " << func.name() << ", " << dim << "\n"
                         << "min " << produce_min << "\n"
                         << "max " << produce_max << "\n";
                stmt = guard_producer(stmt, func, dim_idx, produce_min, produce_max);
            }

            // Now redefine the appropriate regions required
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
            if (!func.updates().empty()) {
                Box b = box_provided(op->body, func.name());
                if (can_slide_up) {
                    string n = prefix + dim + ".min";
                    Expr var = Variable::make(Int(32), n);
                    stmt = LetStmt::make(n, min(var, b[dim_idx].min), stmt);
                } else {
                    string n = prefix + dim + ".max";
                    Expr var = Variable::make(Int(32), n);
                    stmt = LetStmt::make(n, max(var, b[dim_idx].max), stmt);
                }
            }

            return stmt;
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
        if (is_const_one(extent)) {
            // Just treat it like a let
            Stmt s = LetStmt::make(op->name, min, op->body);
            s = mutate(s);
            // Unpack it back into the for
            const LetStmt *l = s.as<LetStmt>();
            internal_assert(l);
            return For::make(op->name, op->min, op->extent, op->for_type, op->device_api, l->body);
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
        Interval bounds_value = find_constant_bounds(op->value, bounds);
        ScopedBinding<Interval> b(bounds, op->name, bounds_value);

        ScopedBinding<Expr> bind(scope, op->name, simplify(expand_expr(op->value, scope), true, bounds));
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
    SlidingWindowOnFunctionAndLoop(Function f, string v, Expr v_min, set<int> &slid_dimensions, Scope<Interval> &bounds)
        : func(std::move(f)), loop_var(std::move(v)), loop_min(std::move(v_min)), slid_dimensions(slid_dimensions), bounds(bounds) {
    }

    Expr new_loop_min;
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

bool depends_on(const string &a, const string &b, const Stmt &s) {
    if (a == b) {
        return true;
    }
    Dependencies deps(b);
    s.accept(&deps);
    // Recursively search for dependencies. Repeatedly using this on the
    // same set of Funcs is algorithmically slow, but even an absurd number
    // of Funcs is still relatively small...
    for (const string &i : deps.dependencies) {
        if (depends_on(a, i, s)) {
            return true;
        }
    }
    return false;
}

// Update the loop variable referenced by prefetch directives.
class SubstitutePrefetchVar : public IRMutator {
    const string &old_var;
    const string &new_var;

    using IRMutator::visit;

    Stmt visit(const Prefetch *op) override {
        Stmt new_body = mutate(op->body);
        if (op->prefetch.var == old_var) {
            PrefetchDirective p = op->prefetch;
            p.var = new_var;
            return Prefetch::make(op->name, op->types, op->bounds, p, op->condition, new_body);
        } else if (!new_body.same_as(op->body)) {
            return Prefetch::make(op->name, op->types, op->bounds, op->prefetch, op->condition, new_body);
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

    Scope<Interval> bounds;

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

        Expr prev_loop_min = loop_min;
        const Function *prev_func = nullptr;

        list<pair<string, Expr>> new_lets;
        for (const Function &func : sliding) {
            debug(3) << "Doing sliding window analysis on function " << func.name() << "\n";

            Expr sliding_loop_min;
            if (prev_func && depends_on(func.name(), prev_func->name(), body)) {
                // The production of func depends on the production of prev_func.
                // The loop min needs to grow to warm up func before prev_func.
                sliding_loop_min = loop_min;
            } else {
                // The production of func does not depend on the production of prev_func.
                // We can use the previous loop_min, and move the min to accommodate
                // both func and prev_func.
                sliding_loop_min = prev_loop_min;
            }

            SlidingWindowOnFunctionAndLoop slider(func, name, sliding_loop_min, slid_dimensions[func.name()], bounds);
            body = slider.mutate(body);

            prev_loop_min = loop_min;
            prev_func = &func;

            if (slider.new_loop_min.defined()) {
                // Update the loop body to use the adjusted loop min.
                Expr new_loop_min = min(slider.new_loop_min, loop_min);
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
        }

        body = mutate(body);

        if (body.same_as(op->body) && loop_min.same_as(op->min) && loop_extent.same_as(op->extent) && name == op->name) {
            return op;
        } else {
            Stmt result = For::make(name, loop_min, loop_extent, op->for_type, op->device_api, body);
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

    Stmt visit(const LetStmt *op) override {
        Interval bounds_value = find_constant_bounds(op->value, bounds);
        ScopedBinding<Interval> b(bounds, op->name, bounds_value);
        return IRMutator::visit(op);
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
            result = For::make(op->name, min, extent, op->for_type, op->device_api, body);
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
