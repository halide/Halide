#include "Lower.h"
#include "IROperator.h"
#include "Substitute.h"
#include "Function.h"
#include "Scope.h"
#include "Bounds.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "Log.h"
#include <iostream>
#include <set>
#include <sstream>
#include "RemoveDeadLets.h"
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
            expr = new Variable(v->type, prefix + v->name, v->reduction_domain);
        }
    }
    void visit(const Let *op) {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        expr = new Let(prefix + op->name, value, body);
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
    Stmt stmt = new Provide(buffer, value, site);
            
    // Define the function args in terms of the loop variables using the splits
    for (size_t i = 0; i < s.splits.size(); i++) {
        const Schedule::Split &split = s.splits[i];
        Expr inner = new Variable(Int(32), prefix + split.inner);
        Expr outer = new Variable(Int(32), prefix + split.outer);
        Expr old_min = new Variable(Int(32), prefix + split.old_var + ".min");
        stmt = new LetStmt(prefix + split.old_var, outer * split.factor + inner + old_min, stmt);
    }
            
    // Build the loop nest
    for (size_t i = 0; i < s.dims.size(); i++) {
        const Schedule::Dim &dim = s.dims[i];
        Expr min = new Variable(Int(32), prefix + dim.var + ".min");
        Expr extent = new Variable(Int(32), prefix + dim.var + ".extent");
        stmt = new For(prefix + dim.var, min, extent, dim.for_type, stmt);
    }

    // Define the bounds on the split dimensions using the bounds
    // on the function args
    for (size_t i = s.splits.size(); i > 0; i--) {
        const Schedule::Split &split = s.splits[i-1];
        Expr old_var_extent = new Variable(Int(32), prefix + split.old_var + ".extent");
        Expr inner_extent = split.factor;
        Expr outer_extent = (old_var_extent + split.factor - 1)/split.factor;
        stmt = new LetStmt(prefix + split.inner + ".min", 0, stmt);
        stmt = new LetStmt(prefix + split.inner + ".extent", inner_extent, stmt);
        stmt = new LetStmt(prefix + split.outer + ".min", 0, stmt);
        stmt = new LetStmt(prefix + split.outer + ".extent", outer_extent, stmt);            
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
        site.push_back(new Variable(Int(32), f.name() + "." + f.args()[i]));
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
        log(2) << "Reduction site " << i << " = " << site[i] << "\n";
    }

    Stmt loop = build_provide_loop_nest(f.name(), prefix, site, value, f.reduction_schedule());

    // Now define the bounds on the reduction domain
    const vector<ReductionVariable> &dom = f.reduction_domain().domain();
    for (size_t i = 0; i < dom.size(); i++) {
        string p = prefix + dom[i].var;
        loop = new LetStmt(p + ".min", dom[i].min, loop);
        loop = new LetStmt(p + ".extent", dom[i].extent, loop);
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
        map<string, Region> regions = regions_required(update);
        Region bounds = regions[func.name()];
        if (bounds.size() > 0) {
            assert(bounds.size() == func.args().size());
            // Expand the region to be computed using the region read in the update step
            for (size_t i = 0; i < bounds.size(); i++) {                        
                string var = func.name() + "." + func.args()[i];
                Expr update_min = new Variable(Int(32), var + ".update_min");
                Expr update_extent = new Variable(Int(32), var + ".update_extent");
                Expr consume_min = new Variable(Int(32), var + ".min");
                Expr consume_extent = new Variable(Int(32), var + ".extent");
                Expr init_min = new Min(update_min, consume_min);
                Expr init_max_plus_one = new Max(update_min + update_extent, consume_min + consume_extent);
                Expr init_extent = init_max_plus_one - init_min;
                produce = new LetStmt(var + ".min", init_min, produce);
                produce = new LetStmt(var + ".extent", init_extent, produce);
            }
            
            // Define the region read during the update step
            for (size_t i = 0; i < bounds.size(); i++) {
                string var = func.name() + "." + func.args()[i];
                produce = new LetStmt(var + ".update_min", bounds[i].min, produce);
                produce = new LetStmt(var + ".update_extent", bounds[i].extent, produce);
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
        Expr min_var = new Variable(Int(32), min_name);
        Expr extent_var = new Variable(Int(32), extent_name);
        Expr check = (b.min <= min_var) && ((b.min + b.extent) >= (min_var + extent_var)); 
        string error_msg = "Bounds given for " + b.var + " in " + func.name() + " don't cover required region";
        body = new Block(new AssertStmt(check, error_msg),
                         new LetStmt(min_name, b.min, 
                                     new LetStmt(extent_name, b.extent, body)));
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
    IsCalledInStmt(Function f, Stmt s) : func(f.name()), result(false) {
        s.accept(this);
    }
    
};

bool function_is_called_in_stmt(Function f, Stmt s) {
    return IsCalledInStmt(f, s).result;
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
        return new Pipeline(func.name(), realization.first, realization.second, s);
    }

    Stmt build_realize(Stmt s) {
        // The allocate should cover everything touched below this point
        map<string, Region> regions = regions_touched(s);
        Region bounds = regions[func.name()];
        
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
        s = new Realize(func.name(), func.value().type(), bounds, s);
        
        return inject_explicit_bounds(s, func);        
    }

    using IRMutator::visit;

    virtual void visit(const For *for_loop) {            
        log(3) << "InjectRealization of " << func.name() << " entering for loop over " << for_loop->name << "\n";
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
            log(2) << "Injecting realization of " << func.name() << " around node " << Stmt(for_loop) << "\n";
            stmt = build_realize(build_pipeline(for_loop));
            found_store_level = found_compute_level = true;
            return;
        }

        if (compute_level.match(for_loop->name)) {
            log(3) << "Found compute level\n";
            if (function_is_called_in_stmt(func, body)) {
                body = build_pipeline(body);
            }
            found_compute_level = true;
        } 

        if (store_level.match(for_loop->name)) {
            log(3) << "Found store level\n";
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
            stmt = new For(for_loop->name, 
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
            log(2) << "Injecting realization of " << func.name() << " around node " << Stmt(op) << "\n";
            stmt = build_realize(build_pipeline(op));
            found_store_level = found_compute_level = true;
        } else {
            stmt = op;
        }
    }

};

class InlineFunction : public IRMutator {
    Function func;
public:
    InlineFunction(Function f) : func(f) {
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

            /*
            assert(args.size() == func.args().size());
            for (size_t i = 0; i < args.size(); i++) {
                body = new Let(func.name() + "." + func.args()[i], 
                               args[i], 
                               body);
            }
            */
            
            // Paste in the args directly - introducing too many let
            // statements messes up all our peephole matching
            for (size_t i = 0; i < args.size(); i++) {
                body = substitute(func.name() + "." + func.args()[i], 
                                  args[i], body);
            
            }
            
            expr = body;
        } else {
            IRMutator::visit(op);
        }
    }
};

/* Find all the internal halide calls in an expr */
class FindCalls : public IRVisitor {
public:
    FindCalls(Expr e) {e.accept(this);}
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
    FindBuffers(Stmt s) {s.accept(this);}
    vector<string> buffers;

    void include(const string &name) {
        for (size_t i = 0; i < buffers.size(); i++) {
            if (buffers[i] == name) return;
        }
        buffers.push_back(name);        
    }

    using IRVisitor::visit;

    void visit(const Call *op) {
        IRVisitor::visit(op);
        if (op->call_type == Call::Image) include(op->name);
    }
};

void populate_environment(Function f, map<string, Function> &env, bool recursive = true) {    
    map<string, Function>::const_iterator iter = env.find(f.name());
    if (iter != env.end()) {
        assert(iter->second.same_as(f) && 
               "Can't compile a pipeline using multiple functions with same name");
        return;
    }
            
    FindCalls calls(f.value());

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
    for (map<string, Function>::const_iterator iter = env.begin(); iter != env.end(); iter++) {
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
        for (map<string, Function>::const_iterator iter = env.begin(); iter != env.end(); iter++) {
            const string &f = iter->first;
            if (result_set.find(f) == result_set.end()) {
                bool good_to_schedule = true;
                const set<string> &inputs = graph[f];
                for (set<string>::const_iterator i = inputs.begin(); i != inputs.end(); i++) {
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
        s = new Pipeline(f.name(), r.first, r.second, new AssertStmt(const_true(), "Dummy consume step"));
    }
    return inject_explicit_bounds(s, f);
}

Stmt schedule_functions(Stmt s, const vector<string> &order, 
                        const map<string, Function> &env, 
                        const map<string, set<string> > &graph) {

    // Inject a loop over root to give us a scheduling point
    string root_var = Schedule::LoopLevel::root().func + "." + Schedule::LoopLevel::root().var;
    s = new For(root_var, 0, 1, For::Serial, s);

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
            log(1) << "Inlining " << order[i-1] << '\n';
            s = InlineFunction(f).mutate(s);
        } else {
            log(1) << "Injecting realization of " << order[i-1] << '\n';
            InjectRealization injector(f);
            s = injector.mutate(s);
            assert(injector.found_store_level && injector.found_compute_level);
        }
        log(2) << s << '\n';
    }

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    assert(root_loop);
    return root_loop->body;    
    
}

// Insert checks to make sure a statement doesn't read out of bounds
// on inputs or outputs, and that the inputs and outputs conform to
// the format required (e.g. stride.0 must be 1).
Stmt add_image_checks(Stmt s, Function f) {
    vector<string> bufs = FindBuffers(s).buffers;    

    bufs.push_back(f.name());

    map<string, Region> regions = regions_touched(s);
    for (size_t i = 0; i < bufs.size(); i++) {
        // Validate the buffer arguments
        string var_name = bufs[i] + ".stride.0";
        Expr var = new Variable(Int(32), var_name);
        string error_msg = "stride on innermost dimension of " + bufs[i] + " must be one";
        s = new Block(new AssertStmt(var == 1, error_msg), 
                      new LetStmt(var_name, 1, s));


        // Bounds checking can be disabled via HL_DISABLE_BOUNDS_CHECKING
        const char *disable = getenv("HL_DISABLE_BOUNDS_CHECKING");
        if (!disable || atoi(disable) == 0) {

            // Figure out the region touched
            const Region &region = regions[bufs[i]];
            if (region.size()) {
                log(3) << "In image " << bufs[i] << " region touched is:\n";
                for (size_t j = 0; j < region.size(); j++) {
                    log(3) << region[j].min << ", " << region[j].extent << "\n";
                    ostringstream min_name, extent_name;
                    min_name << bufs[i] << ".min." << j;
                    extent_name << bufs[i] << ".extent." << j;
                    Expr actual_min = new Variable(Int(32), min_name.str());
                    Expr actual_extent = new Variable(Int(32), extent_name.str());
                    Expr min_used = region[j].min;
                    Expr extent_used = region[j].extent;
                    if (!min_used.defined() || !extent_used.defined()) {
                        std::cerr << "Region used of buffer " << bufs[i] 
                                  << " is unbounded in dimension " << i << std::endl;
                        assert(false);
                    }
                    ostringstream error_msg;
                    error_msg << bufs[i] << " is accessed out of bounds in dimension " << j;
                    Stmt check = new AssertStmt((actual_min <= min_used) &&
                                                (actual_min + actual_extent >= min_used + extent_used), 
                                                error_msg.str());
                    s = new Block(check, s);
                }
            }
        } else {
            log(2) << "Bounds checking disabled via HL_DISABLE_BOUNDS_CHECKING\n";
        }
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

    log(2) << "Initial statement: " << '\n' << s << '\n';
    s = schedule_functions(s, order, env, graph);
    log(2) << "All realizations injected:\n" << s << '\n';

    log(1) << "Injecting tracing...\n";
    s = inject_tracing(s);
    log(2) << "Tracing injected:\n" << s << '\n';

    log(1) << "Adding checks for images\n";
    s = add_image_checks(s, f);    
    log(2) << "Image checks injected:\n" << s << '\n';

    log(1) << "Performing bounds inference...\n";
    s = bounds_inference(s, order, env);
    log(2) << "Bounds inference:\n" << s << '\n';

    log(1) << "Performing sliding window optimization...\n";
    s = sliding_window(s, env);
    log(2) << "Sliding window:\n" << s << '\n';

    log(1) << "Simplifying...\n";
    s = simplify(s);
    log(2) << "Simplified: \n" << s << "\n\n";

    log(1) << "Performing storage folding optimization...\n";
    s = storage_folding(s);
    log(2) << "Storage folding:\n" << s << '\n';

    log(1) << "Injecting debug_to_file calls...\n";
    s = debug_to_file(s, env);
    log(2) << "Injected debug_to_file calls:\n" << s << '\n';

    log(1) << "Performing storage flattening...\n";
    s = storage_flattening(s);
    log(2) << "Storage flattening: " << '\n' << s << "\n\n";

    log(1) << "Simplifying...\n";
    s = simplify(s);
    log(2) << "Simplified: \n" << s << "\n\n";

    log(1) << "Vectorizing...\n";
    s = vectorize_loops(s);
    log(2) << "Vectorized: \n" << s << "\n\n";

    log(1) << "Unrolling...\n";
    s = unroll_loops(s);
    log(2) << "Unrolled: \n" << s << "\n\n";

    log(1) << "Simplifying...\n";
    s = simplify(s);
    log(2) << "Simplified: \n" << s << "\n\n";

    log(1) << "Detecting vector interleavings...\n";
    s = rewrite_interleavings(s);
    log(2) << "Rewrote vector interleavings: \n" << s << "\n\n";

    log(1) << "Simplifying...\n";
    s = simplify(s);
    s = remove_trivial_for_loops(s);
    s = remove_dead_lets(s);
    log(1) << "Simplified: \n" << s << "\n\n";

    return s;
} 
   
}
}
