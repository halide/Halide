#include "Lower.h"
#include "IROperator.h"
#include "Substitute.h"
#include "Function.h"
#include "Scope.h"
#include "Bounds.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "Debug.h"
#include <iostream>
#include <set>
#include <sstream>
#include "Tracing.h"
#include "StorageFlattening.h"
#include "BoundsInference.h"
#include "VectorizeLoops.h"
#include "UnrollLoops.h"
#include "SlidingWindow.h"
#include "StorageFolding.h"
#include "RemoveTrivialForLoops.h"
#include "Deinterleave.h"
#include "DebugToFile.h"
#include "EarlyFree.h"
#include "UniquifyVariableNames.h"
#include "CSE.h"

namespace Halide {
namespace Internal {

using std::set;
using std::ostringstream;
using std::string;
using std::vector;
using std::map;
using std::pair;
using std::make_pair;

void lower_test() {

    Func f("f"), g("g"), h("h");
    Var x("x"), y("y"), xi, xo, yi, yo;

    h(x, y) = x-y;
    g(x, y) = h(x+1, y) + h(x-1, y);
    f(x, y) = g(x, y-1) + g(x, y+1);


    //f.split(x, xo, xi, 4).vectorize(xi).parallel(xo);
    //f.compute_root();

    //g.split(y, yo, yi, 2).unroll(yi);;
    g.store_at(f, y).compute_at(f, x);
    h.store_at(f, y).compute_at(f, y);

    Stmt result = lower(f.function());

    assert(result.defined() && "Lowering returned trivial function");

    std::cout << "Lowering test passed" << std::endl;
}

// Prefix all names in an expression with some string.
class QualifyExpr : public IRMutator {
    string prefix;

    using IRMutator::visit;

    void visit(const Variable *v) {
        if (v->param.defined()) {
            expr = v;
        } else {
            expr = Variable::make(v->type, prefix + v->name, v->reduction_domain);
        }
    }
    void visit(const Let *op) {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        expr = Let::make(prefix + op->name, value, body);
    }
public:
    QualifyExpr(string p) : prefix(p) {}
};

Expr qualify_expr(string prefix, Expr value) {
    QualifyExpr q(prefix);
    return q.mutate(value);
}

// Build a loop nest about a provide node using a schedule
Stmt build_provide_loop_nest(string buffer, string prefix, vector<Expr> site, Expr value, const Schedule &s) {
    // We'll build it from inside out, starting from a store node,
    // then wrapping it in for loops.

    // Make the (multi-dimensional) store node
    Stmt stmt = Provide::make(buffer, value, site);

    // Define the function args in terms of the loop variables using the splits
    for (size_t i = 0; i < s.splits.size(); i++) {
        const Schedule::Split &split = s.splits[i];
        Expr outer = Variable::make(Int(32), prefix + split.outer);
        if (!split.is_rename) {
            Expr inner = Variable::make(Int(32), prefix + split.inner);
            Expr old_min = Variable::make(Int(32), prefix + split.old_var + ".min");
            // stmt = LetStmt::make(prefix + split.old_var, outer * split.factor + inner + old_min, stmt);
            stmt = substitute(prefix + split.old_var, outer * split.factor + inner + old_min, stmt);
        } else {
            stmt = substitute(prefix + split.old_var, outer, stmt);
        }
    }

    // Build the loop nest
    for (size_t i = 0; i < s.dims.size(); i++) {
        const Schedule::Dim &dim = s.dims[i];
        Expr min = Variable::make(Int(32), prefix + dim.var + ".min");
        Expr extent = Variable::make(Int(32), prefix + dim.var + ".extent");
        stmt = For::make(prefix + dim.var, min, extent, dim.for_type, stmt);
    }

    // Define the bounds on the split dimensions using the bounds
    // on the function args
    for (size_t i = s.splits.size(); i > 0; i--) {
        const Schedule::Split &split = s.splits[i-1];
        Expr old_var_extent = Variable::make(Int(32), prefix + split.old_var + ".extent");
        Expr old_var_min = Variable::make(Int(32), prefix + split.old_var + ".min");
        if (!split.is_rename) {
            Expr inner_extent = split.factor;
            Expr outer_extent = (old_var_extent + split.factor - 1)/split.factor;
            stmt = LetStmt::make(prefix + split.inner + ".min", 0, stmt);
            stmt = LetStmt::make(prefix + split.inner + ".extent", inner_extent, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".min", 0, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".extent", outer_extent, stmt);
        } else {
            stmt = LetStmt::make(prefix + split.outer + ".min", old_var_min, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".extent", old_var_extent, stmt);
        }
    }

    return stmt;
}

// Turn a function into a loop nest that computes it. It will
// refer to external vars of the form function_name.arg_name.min
// and function_name.arg_name.extent to define the bounds over
// which it should be realized. It will compute at least those
// bounds (depending on splits, it may compute more). This loop
// won't do any allocation.
Stmt build_produce(Function f) {

    string prefix = f.name() + ".";

    // Compute the site to store to as the function args
    vector<Expr> site;
    Expr value = qualify_expr(prefix, f.value());

    for (size_t i = 0; i < f.args().size(); i++) {
        site.push_back(Variable::make(Int(32), f.name() + "." + f.args()[i]));
    }

    return build_provide_loop_nest(f.name(), prefix, site, value, f.schedule());
}

// Build the loop nest that updates a function (assuming it's a reduction).
Stmt build_update(Function f) {
    if (!f.is_reduction()) return Stmt();

    string prefix = f.name() + ".";

    vector<Expr> site;
    Expr value = qualify_expr(prefix, f.reduction_value());

    for (size_t i = 0; i < f.reduction_args().size(); i++) {
        site.push_back(qualify_expr(prefix, f.reduction_args()[i]));
        debug(2) << "Reduction site " << i << " = " << site[i] << "\n";
    }

    Stmt loop = build_provide_loop_nest(f.name(), prefix, site, value, f.reduction_schedule());

    // Now define the bounds on the reduction domain
    const vector<ReductionVariable> &dom = f.reduction_domain().domain();
    for (size_t i = 0; i < dom.size(); i++) {
        string p = prefix + dom[i].var;
        loop = LetStmt::make(p + ".min", dom[i].min, loop);
        loop = LetStmt::make(p + ".extent", dom[i].extent, loop);
    }

    return loop;
}

pair<Stmt, Stmt> build_realization(Function func) {
    Stmt produce = build_produce(func);
    Stmt update = build_update(func);

    if (update.defined()) {
        // Expand the bounds computed in the produce step
        // using the bounds read in the update step. This is
        // necessary because later bounds inference does not
        // consider the bounds read during an update step
        Region bounds = region_called(update, func.name());
        if (!bounds.empty()) {
            assert(bounds.size() == func.args().size());
            // Expand the region to be computed using the region read in the update step
            for (size_t i = 0; i < bounds.size(); i++) {
                string var = func.name() + "." + func.args()[i];
                Expr update_min = Variable::make(Int(32), var + ".update_min");
                Expr update_extent = Variable::make(Int(32), var + ".update_extent");
                Expr consume_min = Variable::make(Int(32), var + ".min");
                Expr consume_extent = Variable::make(Int(32), var + ".extent");
                Expr init_min = Min::make(update_min, consume_min);
                Expr init_max_plus_one = Max::make(update_min + update_extent, consume_min + consume_extent);
                Expr init_extent = init_max_plus_one - init_min;
                produce = LetStmt::make(var + ".min", init_min, produce);
                produce = LetStmt::make(var + ".extent", init_extent, produce);
            }

            // Define the region read during the update step
            for (size_t i = 0; i < bounds.size(); i++) {
                string var = func.name() + "." + func.args()[i];
                produce = LetStmt::make(var + ".update_min", bounds[i].min, produce);
                produce = LetStmt::make(var + ".update_extent", bounds[i].extent, produce);
            }
        }
    }
    return make_pair(produce, update);
}

// A schedule may include explicit bounds on some dimension. This
// injects let statements that set those bounds, and assertions that
// check that those bounds are sufficiently large to cover the
// inferred bounds required.
Stmt inject_explicit_bounds(Stmt body, Function func) {
    // Inject any explicit bounds
    for (size_t i = 0; i < func.schedule().bounds.size(); i++) {
        Schedule::Bound b = func.schedule().bounds[i];
        string min_name = func.name() + "." + b.var + ".min";
        string extent_name = func.name() + "." + b.var + ".extent";
        Expr min_var = Variable::make(Int(32), min_name);
        Expr extent_var = Variable::make(Int(32), extent_name);
        Expr check = (b.min <= min_var) && ((b.min + b.extent) >= (min_var + extent_var));
        string error_msg = "Bounds given for " + b.var + " in " + func.name() + " don't cover required region";
        body = Block::make(AssertStmt::make(check, error_msg),
                         LetStmt::make(min_name, b.min,
                                     LetStmt::make(extent_name, b.extent, body)));
    }
    return body;
}

class IsCalledInStmt : public IRVisitor {
    string func;

    using IRVisitor::visit;

    void visit(const Call *op) {
        IRVisitor::visit(op);
        if (op->name == func) result = true;
    }

public:
    bool result;
    IsCalledInStmt(Function f) : func(f.name()), result(false) {
    }

};

bool function_is_called_in_stmt(Function f, Stmt s) {
    IsCalledInStmt is_called(f);
    s.accept(&is_called);
    return is_called.result;
}

// Inject the allocation and realization of a function into an
// existing loop nest using its schedule
class InjectRealization : public IRMutator {
public:
    const Function &func;
    bool found_store_level, found_compute_level;
    InjectRealization(const Function &f) : func(f), found_store_level(false), found_compute_level(false) {}

private:
    Stmt build_pipeline(Stmt s) {
        pair<Stmt, Stmt> realization = build_realization(func);
        return Pipeline::make(func.name(), realization.first, realization.second, s);
    }

    Stmt build_realize(Stmt s) {
        // The allocate should cover everything touched below this point
        //Region bounds = region_touched(s, func.name());

        /* The following works if the provide steps of a realization
         * always covers the region that will be used */
        debug(4) << "Computing region provided of " << func.name() << " by " << s << "\n";
        Region bounds = region_provided(s, func.name());
        debug(4) << "Done computing region provided\n";

        /* The following would work if things were only ever computed
         * exactly to cover the region read. Loop splitting (which
         * rounds things up, and reductions spraying writes
         * everywhere, both break this assumption */

        /*
        Region bounds;
        for (size_t i = 0; i < func.args().size(); i++) {
            Expr min = Variable::make(Int(32), func.name() + "." + func.args()[i] + ".min");
            Expr extent = Variable::make(Int(32), func.name() + "." + func.args()[i] + ".extent");
            bounds.push_back(Range(min, extent));
        }
        */

        for (size_t i = 0; i < bounds.size(); i++) {
            if (!bounds[i].min.defined()) {
                std::cerr << "Use of " << func.name() << " is unbounded below in dimension "
                          << func.args()[i] << " in the following statement:\n" << s << "\n";
                assert(false);
            }
            if (!bounds[i].extent.defined()) {
                std::cerr << "Use of " << func.name() << " is unbounded above in dimension "
                          << func.args()[i] << " in the following statement:\n" << s << "\n";
                assert(false);
            }
        }

        // Change the body of the for loop to do an allocation
        s = Realize::make(func.name(), func.value().type(), bounds, s);

        return inject_explicit_bounds(s, func);
    }

    using IRMutator::visit;

    virtual void visit(const For *for_loop) {
        debug(3) << "InjectRealization of " << func.name() << " entering for loop over " << for_loop->name << "\n";
        const Schedule::LoopLevel &compute_level = func.schedule().compute_level;
        const Schedule::LoopLevel &store_level = func.schedule().store_level;

        Stmt body = for_loop->body;

        // Can't schedule things inside a vector for loop
        if (for_loop->for_type != For::Vectorized) {
            body = mutate(for_loop->body);
        } else if (func.is_reduction() &&
                   func.schedule().compute_level.is_inline() &&
                   function_is_called_in_stmt(func, for_loop)) {
            // If we're trying to inline a reduction, schedule it here and bail out
            debug(2) << "Injecting realization of " << func.name() << " around node " << Stmt(for_loop) << "\n";
            stmt = build_realize(build_pipeline(for_loop));
            found_store_level = found_compute_level = true;
            return;
        }

        if (compute_level.match(for_loop->name)) {
            debug(3) << "Found compute level\n";
            if (function_is_called_in_stmt(func, body)) {
                body = build_pipeline(body);
            }
            found_compute_level = true;
        }

        if (store_level.match(for_loop->name)) {
            debug(3) << "Found store level\n";
            assert(found_compute_level &&
                   "The compute loop level was not found within the store loop level!");

            if (function_is_called_in_stmt(func, body)) {
                body = build_realize(body);
            }

            found_store_level = true;
        }


        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = For::make(for_loop->name,
                           for_loop->min,
                           for_loop->extent,
                           for_loop->for_type,
                           body);
        }
    }

    // If we're an inline reduction, we may need to inject a realization here
    virtual void visit(const Provide *op) {
        if (op->name != func.name() &&
            func.is_reduction() &&
            func.schedule().compute_level.is_inline() &&
            function_is_called_in_stmt(func, op)) {
            debug(2) << "Injecting realization of " << func.name() << " around node " << Stmt(op) << "\n";
            stmt = build_realize(build_pipeline(op));
            found_store_level = found_compute_level = true;
        } else {
            stmt = op;
        }
    }

};

class InlineFunction : public IRMutator {
    Function func;
    bool found;
public:
    InlineFunction(Function f) : func(f), found(false) {
        assert(!f.is_reduction());
    }
private:
    using IRMutator::visit;

    void visit(const Call *op) {
        // std::cout << "Found call to " << op->name << endl;
        if (op->name == func.name()) {
            // Mutate the args
            vector<Expr> args(op->args.size());
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(op->args[i]);
            }
            // Grab the body
            Expr body = qualify_expr(func.name() + ".", func.value());


            // Bind the args using Let nodes
            assert(args.size() == func.args().size());

            for (size_t i = 0; i < args.size(); i++) {
                body = Let::make(func.name() + "." + func.args()[i],
                               args[i],
                               body);
            }

            expr = body;

            found = true;
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Provide *op) {
        found = false;
        IRMutator::visit(op);

        if (found) {
            // Clean up so that we don't get code explosion due to recursive inlining
            stmt = common_subexpression_elimination(stmt);
        }
    }
};

/* Find all the internal halide calls in an expr */
class FindCalls : public IRVisitor {
public:
    map<string, Function> calls;

    using IRVisitor::visit;

    void visit(const Call *call) {
        IRVisitor::visit(call);
        if (call->call_type == Call::Halide) {
            map<string, Function>::iterator iter = calls.find(call->name);
            if (iter == calls.end()) {
                calls[call->name] = call->func;
            } else {
                assert(iter->second.same_as(call->func) &&
                       "Can't compile a pipeline using multiple functions with same name");
            }
        }
    }
};

/* Find all the externally referenced buffers in a stmt */
class FindBuffers : public IRVisitor {
public:
    struct Result {
        Buffer image;
        Parameter param;
        Type type;
    };

    map<string, Result> buffers;

    using IRVisitor::visit;

    void visit(const Call *op) {
        IRVisitor::visit(op);
        if (op->image.defined()) {
            Result r;
            r.image = op->image;
            r.type = op->type.element_of();
            buffers[op->name] = r;
        } else if (op->param.defined()) {
            Result r;
            r.param = op->param;
            r.type = op->type.element_of();
            buffers[op->name] = r;
        }
    }
};

void populate_environment(Function f, map<string, Function> &env, bool recursive = true) {
    map<string, Function>::const_iterator iter = env.find(f.name());
    if (iter != env.end()) {
        assert(iter->second.same_as(f) &&
               "Can't compile a pipeline using multiple functions with same name");
        return;
    }

    FindCalls calls;
    f.value().accept(&calls);

    // Consider reductions
    if (f.is_reduction()) {
        f.reduction_value().accept(&calls);
        for (size_t i = 0; i < f.reduction_args().size(); i++) {
            f.reduction_args()[i].accept(&calls);
        }
    }

    if (!recursive) {
        env.insert(calls.calls.begin(), calls.calls.end());
    } else {
        env[f.name()] = f;

        for (map<string, Function>::const_iterator iter = calls.calls.begin();
             iter != calls.calls.end(); ++iter) {
            populate_environment(iter->second, env);
        }
    }
}

vector<string> realization_order(string output, const map<string, Function> &env, map<string, set<string> > &graph) {
    // Make a DAG representing the pipeline. Each function maps to the set describing its inputs.
    // Populate the graph
    for (map<string, Function>::const_iterator iter = env.begin();
         iter != env.end(); ++iter) {
        map<string, Function> calls;
        populate_environment(iter->second, calls, false);

        for (map<string, Function>::const_iterator j = calls.begin();
             j != calls.end(); ++j) {
            graph[iter->first].insert(j->first);
        }
    }

    vector<string> result;
    set<string> result_set;

    while (true) {
        // Find a function not in result_set, for which all its inputs are
        // in result_set. Stop when we reach the output function.
        bool scheduled_something = false;
        for (map<string, Function>::const_iterator iter = env.begin();
             iter != env.end(); ++iter) {
            const string &f = iter->first;
            if (result_set.find(f) == result_set.end()) {
                bool good_to_schedule = true;
                const set<string> &inputs = graph[f];
                for (set<string>::const_iterator i = inputs.begin();
                     i != inputs.end(); ++i) {
                    if (*i != f && result_set.find(*i) == result_set.end()) {
                        good_to_schedule = false;
                    }
                }

                if (good_to_schedule) {
                    scheduled_something = true;
                    result_set.insert(f);
                    result.push_back(f);
                    if (f == output) return result;
                }
            }
        }

        assert(scheduled_something && "Stuck in a loop computing a realization order. Perhaps this pipeline has a loop?");
    }

}

Stmt create_initial_loop_nest(Function f) {
    // Generate initial loop nest
    pair<Stmt, Stmt> r = build_realization(f);
    Stmt s = r.first;
    if (r.second.defined()) {
        // This must be in a pipeline so that bounds inference understands the update step
        s = Pipeline::make(f.name(), r.first, r.second, AssertStmt::make(const_true(), "Dummy consume step"));
    }
    return inject_explicit_bounds(s, f);
}

Stmt schedule_functions(Stmt s, const vector<string> &order,
                        const map<string, Function> &env,
                        const map<string, set<string> > &graph) {

    // Inject a loop over root to give us a scheduling point
    string root_var = Schedule::LoopLevel::root().func + "." + Schedule::LoopLevel::root().var;
    s = For::make(root_var, 0, 1, For::Serial, s);

    for (size_t i = order.size()-1; i > 0; i--) {
        Function f = env.find(order[i-1])->second;

        if (f.schedule().compute_level.is_inline() &&
            !f.schedule().store_level.is_inline()) {
            std::cerr << "Function " << f.name() << " is scheduled to be computed inline, "
                      << "but is not scheduled to be stored inline. A storage schedule "
                      << "makes no sense for functions computed inline" << std::endl;
            assert(false);
        }

        if (!f.is_reduction() && f.schedule().compute_level.is_inline()) {
            debug(1) << "Inlining " << order[i-1] << '\n';
            s = InlineFunction(f).mutate(s);
        } else {
            debug(1) << "Injecting realization of " << order[i-1] << '\n';
            InjectRealization injector(f);
            s = injector.mutate(s);
            assert(injector.found_store_level && injector.found_compute_level);
        }
        debug(2) << s << '\n';
    }

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    assert(root_loop);
    return root_loop->body;

}

// Insert checks to make sure a statement doesn't read out of bounds
// on inputs or outputs, and that the inputs and outputs conform to
// the format required (e.g. stride.0 must be 1). Returns two
// statements, the first one is the mutated statement with the checks
// inserted. The second is a piece of code which will rewrite the
// buffer_t sizes, mins, and strides in order to satisfy the
// requirements.
Stmt add_image_checks(Stmt s, Function f) {

    // First hunt for all the referenced buffers
    FindBuffers finder;
    s.accept(&finder);
    map<string, FindBuffers::Result> bufs = finder.buffers;

    // Add the output buffer
    FindBuffers::Result output_buffer;
    output_buffer.type = f.value().type();
    output_buffer.param = f.output_buffer();
    bufs[f.name()] = output_buffer;

    // Now compute what regions of each buffer are touched
    map<string, Region> regions = regions_touched(s);

    // Now iterate through all the buffers, creating a list of lets
    // and a list of asserts.
    vector<pair<string, Expr> > lets_required;
    vector<pair<string, Expr> > lets_constrained;
    vector<pair<string, Expr> > lets_proposed;
    vector<Stmt> asserts_required;
    vector<Stmt> asserts_constrained;
    vector<Stmt> asserts_proposed;
    vector<Stmt> asserts_elem_size;
    vector<Stmt> buffer_rewrites;

    // Inject the code that conditionally returns if we're in inference mode
    Expr maybe_return_condition = const_false();

    // We're also going to apply the constraints to the required min
    // and extent. To do this we have to substitute all references to
    // the actual sizes of the input images in the constraints with
    // references to the required sizes.
    map<string, Expr> replace_with_required;

    for (map<string, FindBuffers::Result>::iterator iter = bufs.begin();
         iter != bufs.end(); ++iter) {
        const string &name = iter->first;

        for (char dim = '0'; dim < '4'; dim++) {
            Expr min_required = Variable::make(Int(32), name + ".min." + dim + ".required");
            replace_with_required[name + ".min." + dim] = min_required;

            Expr extent_required = Variable::make(Int(32), name + ".extent." + dim + ".required");
            replace_with_required[name + ".extent." + dim] = extent_required;

            Expr stride_required = Variable::make(Int(32), name + ".stride." + dim + ".required");
            replace_with_required[name + ".stride." + dim] = stride_required;
        }
    }

    // We also want to build a map that lets us replace values passed
    // in with the constrained version. This is applied to the rest of
    // the lowered pipeline to take advantage of the constraints,
    // e.g. for constant folding.
    map<string, Expr> replace_with_constrained;

    for (map<string, FindBuffers::Result>::iterator iter = bufs.begin();
         iter != bufs.end(); ++iter) {
        const string &name = iter->first;
        Buffer &image = iter->second.image;
        Parameter &param = iter->second.param;
        Type type = iter->second.type;
        const Region &region = regions[name];

        // An expression returning whether or not we're in inference mode
        Expr inference_mode = Variable::make(UInt(1), name + ".host_and_dev_are_null");

        maybe_return_condition = maybe_return_condition || inference_mode;

        // Check the elem size matches the internally-understood type
        {
            string elem_size_name = name + ".elem_size";
            Expr elem_size = Variable::make(Int(32), elem_size_name);
            int correct_size = type.bits / 8;
            ostringstream error_msg;
            error_msg << "Element size for " << name << " should be " << correct_size;
            asserts_elem_size.push_back(AssertStmt::make(elem_size == correct_size, error_msg.str()));
        }

        // Check that the region passed in (after applying constraints) is within the region used
        debug(3) << "In image " << name << " region touched is:\n";
        for (size_t j = 0; j < region.size(); j++) {
            char dim = '0' + j;
            debug(3) << region[j].min << ", " << region[j].extent << "\n";
            string actual_min_name = name + ".min." + dim;
            string actual_extent_name = name + ".extent." + dim;
            Expr actual_min = Variable::make(Int(32), actual_min_name);
            Expr actual_extent = Variable::make(Int(32), actual_extent_name);
            Expr min_required = region[j].min;
            Expr extent_required = region[j].extent;
            if (!min_required.defined() || !extent_required.defined()) {
                std::cerr << "Region required of buffer " << name
                          << " is unbounded in dimension " << j << std::endl;
                assert(false);
            }
            string error_msg_extent = name + " is accessed beyond the extent in dimension " + dim;
            string error_msg_min = name + " is accessed before the min in dimension " + dim;
            string min_required_name = name + ".min." + dim + ".required";
            string extent_required_name = name + ".extent." + dim + ".required";
            Expr min_required_var = Variable::make(Int(32), min_required_name);
            Expr extent_required_var = Variable::make(Int(32), extent_required_name);

            lets_required.push_back(make_pair(extent_required_name, extent_required));
            lets_required.push_back(make_pair(min_required_name, min_required));
            asserts_required.push_back(AssertStmt::make(actual_min <= min_required_var, error_msg_min));
            asserts_required.push_back(AssertStmt::make(actual_min + actual_extent >=
                                                        min_required_var + extent_required_var, error_msg_extent));

            // Come up with a required stride to use in bounds
            // inference mode. We don't assert it. It's just used to
            // apply the constraints to to come up with a proposed
            // stride. Strides actually passed in may not be in this
            // order (e.g if storage is swizzled relative to dimension
            // order).
            Expr stride_required;
            if (dim == '0') {
                stride_required = 1;
            } else {
                stride_required = (Variable::make(Int(32), name + ".stride." + (char)(dim-1) + ".required") *
                                   Variable::make(Int(32), name + ".extent." + (char)(dim-1) + ".required"));
            }
            lets_required.push_back(make_pair(name + ".stride." + dim + ".required", stride_required));
        }

        // Create code that mutates the input buffers if we're in bounds inference mode.
        Expr buffer_name = Call::make(Int(32), name, vector<Expr>(), Call::Intrinsic);
        vector<Expr> args = vec(inference_mode, buffer_name, Expr(type.bits/8));
        for (size_t i = 0; i < 4; i++) {
            char dim = '0' + i;
            if (i < region.size()) {
                args.push_back(Variable::make(Int(32), name + ".min." + dim + ".proposed"));
                args.push_back(Variable::make(Int(32), name + ".extent." + dim + ".proposed"));
                args.push_back(Variable::make(Int(32), name + ".stride." + dim + ".proposed"));
            } else {
                args.push_back(0);
                args.push_back(0);
                args.push_back(0);
            }
        }
        Expr call = Call::make(UInt(1), Call::maybe_rewrite_buffer, args, Call::Intrinsic);
        buffer_rewrites.push_back(AssertStmt::make(call, "Failure in maybe_rewrite_buffer"));

        // Build the constraints tests and proposed sizes.
        vector<pair<string, Expr> > constraints;
        for (size_t i = 0; i < region.size(); i++) {
            char dim = '0' + i;
            string min_name = name + ".min." + dim;
            string stride_name = name + ".stride." + dim;
            string extent_name = name + ".extent." + dim;

            Expr stride_constrained, extent_constrained, min_constrained;

            Expr stride_orig = Variable::make(Int(32), stride_name);
            Expr extent_orig = Variable::make(Int(32), extent_name);
            Expr min_orig    = Variable::make(Int(32), min_name);

            Expr stride_required = Variable::make(Int(32), stride_name + ".required");
            Expr extent_required = Variable::make(Int(32), extent_name + ".required");
            Expr min_required = Variable::make(Int(32), min_name + ".required");

            Expr stride_proposed = Variable::make(Int(32), stride_name + ".proposed");
            Expr extent_proposed = Variable::make(Int(32), extent_name + ".proposed");
            Expr min_proposed = Variable::make(Int(32), min_name + ".proposed");

            if (image.defined() && (int)i < image.dimensions()) {
                stride_constrained = image.stride(i);
                extent_constrained = image.extent(i);
                min_constrained = image.min(i);
            } else if (param.defined()) {
                stride_constrained = param.stride_constraint(i);
                extent_constrained = param.extent_constraint(i);
                min_constrained = param.min_constraint(i);
            }

            if (stride_constrained.defined()) {
                // Come up with a suggested stride by passing the
                // required region through this constraint.
                constraints.push_back(make_pair(stride_name, stride_constrained));
                stride_constrained = substitute(replace_with_required, stride_constrained);
                lets_proposed.push_back(make_pair(stride_name + ".proposed", stride_constrained));
            } else {
                lets_proposed.push_back(make_pair(stride_name + ".proposed", stride_required));
            }

            if (min_constrained.defined()) {
                constraints.push_back(make_pair(min_name, min_constrained));
                min_constrained = substitute(replace_with_required, min_constrained);
                lets_proposed.push_back(make_pair(min_name + ".proposed", min_constrained));
            } else {
                lets_proposed.push_back(make_pair(min_name + ".proposed", min_required));
            }

            if (extent_constrained.defined()) {
                constraints.push_back(make_pair(extent_name, extent_constrained));
                extent_constrained = substitute(replace_with_required, extent_constrained);
                lets_proposed.push_back(make_pair(extent_name + ".proposed", extent_constrained));
            } else {
                lets_proposed.push_back(make_pair(extent_name + ".proposed", extent_required));
            }

            // In bounds inference mode, make sure the proposed
            // versions still satisfy the constraints.
            Expr check = ((min_proposed <= min_required) &&
                          (min_proposed + extent_proposed >=
                           min_required + extent_required));
            string error = "Applying the constraints to the required region made it smaller";
            asserts_proposed.push_back(AssertStmt::make((!inference_mode) || check, error));

            check = (stride_proposed >= stride_required);
            error = "Applying the constraints to the required stride made it smaller";
            asserts_proposed.push_back(AssertStmt::make((!inference_mode) || check, error));
        }

        // Assert all the conditions, and set the new values
        for (size_t i = 0; i < constraints.size(); i++) {
            Expr var = Variable::make(Int(32), constraints[i].first);
            Expr constrained_var = Variable::make(Int(32), constraints[i].first + ".constrained");
            Expr value = constraints[i].second;
            ostringstream error;
            error << "Static constraint violated: " << constraints[i].first << " == " << value;

            replace_with_constrained[constraints[i].first] = constrained_var;

            lets_constrained.push_back(make_pair(constraints[i].first + ".constrained", value));

            // Check the var passed in equals the constrained version (when not in inference mode)
            asserts_constrained.push_back(AssertStmt::make(var == constrained_var, error.str()));
        }
    }

    // Replace uses of the var with the constrained versions in the
    // rest of the program. We also need to respect the existence of
    // constrained versions during storage flattening and bounds
    // inference.
    s = substitute(replace_with_constrained, s);

    // Now we add a bunch of code to the top of the pipeline. This is
    // all in reverse order compared to execution, as we incrementally
    // prepending code.

    // Inject the code that checks the constraints are correct.
    for (size_t i = asserts_constrained.size(); i > 0; i--) {
        s = Block::make(asserts_constrained[i-1], s);
    }

    // Inject the code that checks for out-of-bounds access to the buffers.
    for (size_t i = asserts_required.size(); i > 0; i--) {
        s = Block::make(asserts_required[i-1], s);
    }

    // Inject the code that checks that elem_sizes are ok.
    for (size_t i = asserts_elem_size.size(); i > 0; i--) {
        s = Block::make(asserts_elem_size[i-1], s);
    }

    // Inject the code that returns early for inference mode.
    Expr maybe_return = Call::make(UInt(1), Call::maybe_return,
                                   vec(maybe_return_condition), Call::Intrinsic);
    s = Block::make(AssertStmt::make(maybe_return, "Failure in maybe_return"), s);

    // Inject the code that does the buffer rewrites for inference mode.
    for (size_t i = buffer_rewrites.size(); i > 0; i--) {
        s = Block::make(buffer_rewrites[i-1], s);
    }

    // Inject the code that checks the proposed sizes still pass the bounds checks
    for (size_t i = asserts_proposed.size(); i > 0; i--) {
        s = Block::make(asserts_proposed[i-1], s);
    }

    // Inject the code that defines the proposed sizes.
    for (size_t i = lets_proposed.size(); i > 0; i--) {
        s = LetStmt::make(lets_proposed[i-1].first, lets_proposed[i-1].second, s);
    }

    // Inject the code that defines the constrained sizes.
    for (size_t i = lets_constrained.size(); i > 0; i--) {
        s = LetStmt::make(lets_constrained[i-1].first, lets_constrained[i-1].second, s);
    }

    // Inject the code that defines the required sizes produced by bounds inference.
    for (size_t i = lets_required.size(); i > 0; i--) {
        s = LetStmt::make(lets_required[i-1].first, lets_required[i-1].second, s);
    }

    return s;
}

Stmt lower(Function f) {
    // Compute an environment
    map<string, Function> env;
    populate_environment(f, env);

    // Compute a realization order
    map<string, set<string> > graph;
    vector<string> order = realization_order(f.name(), env, graph);
    Stmt s = create_initial_loop_nest(f);

    debug(2) << "Initial statement: " << '\n' << s << '\n';
    s = schedule_functions(s, order, env, graph);
    debug(2) << "All realizations injected:\n" << s << '\n';

    debug(1) << "Injecting tracing...\n";
    s = inject_tracing(s);
    debug(2) << "Tracing injected:\n" << s << '\n';

    debug(1) << "Adding checks for images\n";
    s = add_image_checks(s, f);
    debug(2) << "Image checks injected:\n" << s << '\n';

    // This pass injects nested definitions of variable names, so we
    // can't simplify from here until we fix them up
    debug(1) << "Performing bounds inference...\n";
    s = bounds_inference(s, order, env);
    debug(2) << "Bounds inference:\n" << s << '\n';

    debug(1) << "Performing sliding window optimization...\n";
    s = sliding_window(s, env);
    debug(2) << "Sliding window:\n" << s << '\n';

    // This uniquifies the variable names, so we're good to simplify
    // after this point. This lets later passes assume syntactic
    // equivalence means semantic equivalence.
    debug(1) << "Uniquifying variable names...\n";
    s = uniquify_variable_names(s);
    debug(2) << "Uniquified variable names: \n" << s << "\n\n";

    debug(1) << "Performing storage folding optimization...\n";
    s = storage_folding(s);
    debug(2) << "Storage folding:\n" << s << '\n';

    debug(1) << "Injecting debug_to_file calls...\n";
    s = debug_to_file(s, env);
    debug(2) << "Injected debug_to_file calls:\n" << s << '\n';

    debug(1) << "Performing storage flattening...\n";
    s = storage_flattening(s, env);
    debug(2) << "Storage flattening: " << '\n' << s << "\n\n";

    debug(1) << "Simplifying...\n";
    s = simplify(s);
    debug(2) << "Simplified: \n" << s << "\n\n";

    debug(1) << "Vectorizing...\n";
    s = vectorize_loops(s);
    debug(2) << "Vectorized: \n" << s << "\n\n";

    debug(1) << "Unrolling...\n";
    s = unroll_loops(s);
    debug(2) << "Unrolled: \n" << s << "\n\n";

    debug(1) << "Simplifying...\n";
    s = simplify(s);
    debug(2) << "Simplified: \n" << s << "\n\n";

    debug(1) << "Detecting vector interleavings...\n";
    s = rewrite_interleavings(s);
    debug(2) << "Rewrote vector interleavings: \n" << s << "\n\n";

    debug(1) << "Injecting early frees...\n";
    s = inject_early_frees(s);
    debug(2) << "Injected early frees: \n" << s << "\n\n";

    debug(1) << "Simplifying...\n";
    s = remove_trivial_for_loops(s);
    s = simplify(s);
    s = common_subexpression_elimination(s);
    s = simplify(s);
    debug(1) << "Simplified: \n" << s << "\n\n";

    return s;
}

}
}
