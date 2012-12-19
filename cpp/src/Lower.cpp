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


namespace Halide {
namespace Internal {

using std::set;
using std::ostringstream;

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

    Stmt result = f.lower();

    assert(result.defined() && "Lowering returned trivial function");

    std::cout << "Lowering test passed" << std::endl;
}

Expr build_qualified_rhs(Function f) {
    // Fully qualify the var names in the function rhs
    Expr value = f.value();
    for (size_t i = 0; i < f.args().size(); i++) {
        value = substitute(f.args()[i], new Variable(Int(32), f.name() + "." + f.args()[i]), value);
    }
    return value;
}


// Turn a function into a loop nest that computes it. It will
// refer to external vars of the form function_name.arg_name.min
// and function_name.arg_name.extent to define the bounds over
// which it should be realized. It will compute at least those
// bounds (depending on splits, it may compute more). This loop
// won't do any allocation.

Stmt build_realization(Function f) {
    // We'll build it from inside out, starting from a store node,
    // then wrapping it in for loops.
            
    // All names will get prepended with the function name to avoid ambiguities
    string prefix = f.name() + ".";
            
    // Compute the site to store to as the function args
    vector<Expr> site;
    for (size_t i = 0; i < f.args().size(); i++) {
        site.push_back(new Variable(Int(32), prefix + f.args()[i]));
    }
            
    Expr value = build_qualified_rhs(f);
            
    // Make the (multi-dimensional) store node
    Stmt stmt = new Provide(f.name(), value, site);
            
    // Define the function args in terms of the loop variables using the splits
    for (size_t i = f.schedule().splits.size(); i > 0; i--) {
        const Schedule::Split &split = f.schedule().splits[i-1];
        Expr inner = new Variable(Int(32), prefix + split.inner);
        Expr outer = new Variable(Int(32), prefix + split.outer);
        Expr old_min = new Variable(Int(32), prefix + split.old_var + ".min");
        stmt = new LetStmt(prefix + split.old_var, outer * split.factor + inner + old_min, stmt);
    }
            
    // Build the loop nest
    for (size_t i = 0; i < f.schedule().dims.size(); i++) {
        const Schedule::Dim &dim = f.schedule().dims[i];
        Expr min = new Variable(Int(32), prefix + dim.var + ".min");
        Expr extent = new Variable(Int(32), prefix + dim.var + ".extent");
        stmt = new For(prefix + dim.var, min, extent, dim.for_type, stmt);
    }

    // Define the bounds on the split dimensions using the bounds
    // on the function args
    for (size_t i = f.schedule().splits.size(); i > 0; i--) {
        const Schedule::Split &split = f.schedule().splits[i-1];
        Expr old_var_extent = new Variable(Int(32), prefix + split.old_var + ".extent");
        Expr inner_extent = split.factor;
        Expr outer_extent = (old_var_extent + split.factor - 1)/split.factor;
        stmt = new LetStmt(prefix + split.inner + ".min", 0, stmt);
        stmt = new LetStmt(prefix + split.inner + ".extent", inner_extent, stmt);
        stmt = new LetStmt(prefix + split.outer + ".min", 0, stmt);
        stmt = new LetStmt(prefix + split.outer + ".extent", outer_extent, stmt);            
    }

    // TODO: inject bounds for any explicitly bounded dimensions        
    return stmt;
}

class InjectTracing : public IRMutator {
public:
    int level;
    InjectTracing() {
        char *trace = getenv("HL_TRACE");
        level = trace ? atoi(trace) : 0;
    }


private:
    void visit(const Call *op) {
        expr = op;
    }

    void visit(const Provide *op) {       
        IRMutator::visit(op);
        if (level >= 2) {
            const Provide *op = stmt.as<Provide>();
            vector<Expr> args = op->args;
            args.push_back(op->value);
            Stmt print = new PrintStmt("Provide " + op->buffer, args);
            stmt = new Block(print, op);
        }
    }

    void visit(const Realize *op) {
        IRMutator::visit(op);
        if (level >= 1) {
            const Realize *op = stmt.as<Realize>();
            vector<Expr> args;
            for (size_t i = 0; i < op->bounds.size(); i++) {
                args.push_back(op->bounds[i].first);
                args.push_back(op->bounds[i].second);
            }
            Stmt print = new PrintStmt("Realizing " + op->buffer + " over ", args);
            Stmt body = new Block(print, op->body);
            stmt = new Realize(op->buffer, op->type, op->bounds, body);
        }        
    }
};

// Inject let stmts defining the bounds of a function required at each loop level
class BoundsInference : public IRMutator {
public:
    const vector<string> &funcs;
    const map<string, Function> &env;

    BoundsInference(const vector<string> &f, const map<string, Function> &e) : funcs(f), env(e) {}    

    virtual void visit(const For *for_loop) {
        
        vector<vector<pair<Expr, Expr> > > regions;
        Scope<pair<Expr, Expr> > scope;

        // Compute the region required of each function within this loop body
        for (size_t i = 0; i < funcs.size(); i++) {
            regions.push_back(region_required(funcs[i], for_loop->body, scope));
        }
        
        // TODO: For reductions we also need to consider the region
        // provided within any update statements over this function
        // (but not within the produce statement)

        Stmt body = mutate(for_loop->body);


        log(3) << "Bounds inference considering loop over " << for_loop->name << '\n';

        // Inject let statements defining those bounds
        for (size_t i = 0; i < funcs.size(); i++) {
            const vector<pair<Expr, Expr> > &region = regions[i];
            const Function &f = env.find(funcs[i])->second;
            if (region.empty()) continue;
            log(3) << "Injecting bounds for " << funcs[i] << '\n';
            assert(region.size() == f.args().size() && "Dimensionality mismatch between function and region required");
            for (size_t j = 0; j < region.size(); j++) {
                const string &arg_name = f.args()[j];
                body = new LetStmt(f.name() + "." + arg_name + ".min", region[j].first, body);
                body = new LetStmt(f.name() + "." + arg_name + ".extent", region[j].second, body);
            }
        }

        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = new For(for_loop->name, for_loop->min, for_loop->extent, for_loop->for_type, body);
        }
    }
    
};

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

// Inject the allocation and realization of a function into an
// existing loop nest using its schedule
class InjectRealization : public IRMutator {
public:
    const Function &func;
    bool found_store_level, found_compute_level;
    InjectRealization(const Function &f) : func(f), found_store_level(false), found_compute_level(false) {}

    virtual void visit(const For *for_loop) {            
        if (!found_compute_level && for_loop->name == func.schedule().compute_level) {
            assert((for_loop->name == func.schedule().store_level || found_store_level) && 
                   "The compute loop level is outside the store loop level!");
            Stmt produce = build_realization(func);
            stmt = new Pipeline(func.name(), produce, NULL, for_loop->body);
            stmt = new For(for_loop->name, for_loop->min, for_loop->extent, for_loop->for_type, stmt);
            found_compute_level = true;
            stmt = mutate(stmt);
        } else if (for_loop->name == func.schedule().store_level) {
            // Inject the realization lower down
            found_store_level = true;
            Stmt body = mutate(for_loop->body);
            vector<pair<Expr, Expr> > bounds(func.args().size());
            for (size_t i = 0; i < func.args().size(); i++) {
                string prefix = func.name() + "." + func.args()[i];
                bounds[i].first = new Variable(Int(32), prefix + ".min");
                bounds[i].second = new Variable(Int(32), prefix + ".extent");
            }
            // Change the body of the for loop to do an allocation
            body = new Realize(func.name(), func.value().type(), bounds, body);
            
            body = inject_explicit_bounds(body, func);

            /*
              vector<Expr> print_args;
              for (size_t i = 0; i < func.args().size(); i++) {
              print_args.push_back(bounds[i].first);
              print_args.push_back(bounds[i].second);                
              }            

              body = new Block(
              new PrintStmt("Allocating " + func.name() + " over", print_args), 
              new Block(body,
              new PrintStmt("Freeing " + func.name(), vector<Expr>())));
            */

            stmt = new For(for_loop->name, 
                           for_loop->min, 
                           for_loop->extent, 
                           for_loop->for_type, 
                           body);
        } else {
            stmt = new For(for_loop->name, 
                           for_loop->min, 
                           for_loop->extent, 
                           for_loop->for_type, 
                           mutate(for_loop->body));                
        }
    }
};

class InlineFunction : public IRMutator {
    Function func;
public:
    InlineFunction(Function f) : func(f) {}
private:

    void visit(const Call *op) {        
        // std::cout << "Found call to " << op->name << endl;
        if (op->name == func.name()) {
            // Mutate the args
            vector<Expr> args(op->args.size());
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(op->args[i]);
            }
            // Grab the body
            Expr body = build_qualified_rhs(func);
            // Bind the args
            assert(args.size() == func.args().size());
            for (size_t i = 0; i < args.size(); i++) {
                body = new Let(func.name() + "." + func.args()[i], 
                               args[i], 
                               body);
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
    void visit(const Call *call) {                
        IRVisitor::visit(call);
        if (call->call_type == Call::Halide) {
            calls[call->name] = call->func;
        }
    }
};

/* Find all the external buffers in a stmt */
class FindBuffers : public IRVisitor {
public:
    FindBuffers(Stmt s) {s.accept(this);}
    vector<string> buffers;
    void visit(const Load *load) {
        IRVisitor::visit(load);
        Buffer b = load->image;
        if (b.defined()) {
            for (size_t i = 0; i < buffers.size(); i++) {
                if (buffers[i] == b.name()) return;
            }
            buffers.push_back(b.name());
        }
    }
};

void populate_environment(Function f, map<string, Function> &env, bool recursive = true) {
    if (env.find(f.name()) != env.end()) return;
            
    // TODO: consider reductions
    FindCalls calls(f.value());

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

vector<string> realization_order(string output, const map<string, Function> &env) {
    // Make a DAG representing the pipeline. Each function maps to the set describing its inputs.
    map<string, set<string> > graph;

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
                    if (result_set.find(*i) == result_set.end()) {
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

class FlattenDimensions : public IRMutator {
    Expr flatten_args(const string &name, const vector<Expr> &args) {
        Expr idx = 0;
        for (size_t i = 0; i < args.size(); i++) {
            ostringstream stride_name, min_name;
            stride_name << name << ".stride." << i;
            min_name << name << ".min." << i;
            Expr stride = new Variable(Int(32), stride_name.str());
            Expr min = new Variable(Int(32), min_name.str());
            idx += (args[i] - min) * stride;
        }
        return idx;            
    }

    void visit(const Realize *realize) {
        Stmt body = mutate(realize->body);

        // Compute the size
        Expr size = 1;
        for (size_t i = 0; i < realize->bounds.size(); i++) {
            size *= realize->bounds[i].second;
        }


        size = mutate(size);

        stmt = new Allocate(realize->buffer, realize->type, size, body);

        // Compute the strides 
        for (int i = (int)realize->bounds.size()-1; i > 0; i--) {
            ostringstream stride_name;
            stride_name << realize->buffer << ".stride." << i;
            ostringstream prev_stride_name;
            prev_stride_name << realize->buffer << ".stride." << (i-1);
            ostringstream prev_extent_name;
            prev_extent_name << realize->buffer << ".extent." << (i-1);
            Expr prev_stride = new Variable(Int(32), prev_stride_name.str());
            Expr prev_extent = new Variable(Int(32), prev_extent_name.str());
            stmt = new LetStmt(stride_name.str(), prev_stride * prev_extent, stmt);
        }
        // Innermost stride is one
        stmt = new LetStmt(realize->buffer + ".stride.0", 1, stmt);           

        // Assign the mins and extents stored
        for (int i = realize->bounds.size(); i > 0; i--) { 
            ostringstream min_name, extent_name;
            min_name << realize->buffer << ".min." << (i-1);
            extent_name << realize->buffer << ".extent." << (i-1);
            stmt = new LetStmt(min_name.str(), realize->bounds[i-1].first, stmt);
            stmt = new LetStmt(extent_name.str(), realize->bounds[i-1].second, stmt);
        }
    }

    void visit(const Provide *provide) {
        Expr idx = mutate(flatten_args(provide->buffer, provide->args));
        Expr val = mutate(provide->value);
        stmt = new Store(provide->buffer, val, idx); 
    }

    void visit(const Call *call) {            
        if (call->call_type == Call::Extern) {
            expr = call;
        } else {
            Expr idx = mutate(flatten_args(call->name, call->args));
            expr = new Load(call->type, call->name, idx, call->image, call->param);
        }
    }

};

class VectorizeLoops : public IRMutator {
    class VectorSubs : public IRMutator {
        string var;
        Expr replacement;
        Scope<Type> scope;

        Expr widen(Expr e, int width) {
            if (e.type().width == width) return e;
            else if (e.type().width == 1) return new Broadcast(e, width);
            else assert(false && "Mismatched vector widths in VectorSubs");
            return Expr();
        }
            
        virtual void visit(const Cast *op) {
            Expr value = mutate(op->value);
            if (value.same_as(op->value)) {
                expr = op;
            } else {
                Type t = op->type.vector_of(value.type().width);
                expr = new Cast(t, value);
            }
        }

        virtual void visit(const Variable *op) {
            if (op->name == var) {
                expr = replacement;
            } else if (scope.contains(op->name)) {
                // The type of a var may have changed. E.g. if
                // we're vectorizing across x we need to know the
                // type of y has changed in the following example:
                // let y = x + 1 in y*3
                expr = new Variable(scope.get(op->name), op->name);
            } else {
                expr = op;
            }
        }

        template<typename T> 
        void mutate_binary_operator(const T *op) {
            Expr a = mutate(op->a), b = mutate(op->b);
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                int w = std::max(a.type().width, b.type().width);
                expr = new T(widen(a, w), widen(b, w));
            }
        }

        void visit(const Add *op) {mutate_binary_operator(op);}
        void visit(const Sub *op) {mutate_binary_operator(op);}
        void visit(const Mul *op) {mutate_binary_operator(op);}
        void visit(const Div *op) {mutate_binary_operator(op);}
        void visit(const Mod *op) {mutate_binary_operator(op);}
        void visit(const Min *op) {mutate_binary_operator(op);}
        void visit(const Max *op) {mutate_binary_operator(op);}
        void visit(const EQ *op)  {mutate_binary_operator(op);}
        void visit(const NE *op)  {mutate_binary_operator(op);}
        void visit(const LT *op)  {mutate_binary_operator(op);}
        void visit(const LE *op)  {mutate_binary_operator(op);}
        void visit(const GT *op)  {mutate_binary_operator(op);}
        void visit(const GE *op)  {mutate_binary_operator(op);}
        void visit(const And *op) {mutate_binary_operator(op);}
        void visit(const Or *op)  {mutate_binary_operator(op);}

        void visit(const Select *op) {
            Expr condition = mutate(op->condition);
            Expr true_value = mutate(op->true_value);
            Expr false_value = mutate(op->false_value);
            if (condition.same_as(op->condition) &&
                true_value.same_as(op->true_value) &&
                false_value.same_as(op->false_value)) {
                expr = op;
            } else {
                int width = std::max(true_value.type().width, false_value.type().width);
                width = std::max(width, condition.type().width);
                // Widen the true and false values, but we don't have to widen the condition
                true_value = widen(true_value, width);
                false_value = widen(false_value, width);
                expr = new Select(condition, true_value, false_value);
            }
        }

        void visit(const Load *op) {
            Expr index = mutate(op->index);
            if (index.same_as(op->index)) {
                expr = op;
            } else {
                int w = index.type().width;
                expr = new Load(op->type.vector_of(w), op->buffer, index, op->image, op->param);
            }
        }

        void visit(const Call *op) {
            vector<Expr> new_args(op->args.size());
            bool changed = false;
                
            // Mutate the args
            int max_width = 0;
            for (size_t i = 0; i < op->args.size(); i++) {
                Expr old_arg = op->args[i];
                Expr new_arg = mutate(old_arg);
                if (!new_arg.same_as(old_arg)) changed = true;
                new_args[i] = new_arg;
                max_width = std::max(new_arg.type().width, max_width);
            }
                
            if (!changed) expr = op;
            else {
                // Widen the args to have the same width as the max width found
                for (size_t i = 0; i < new_args.size(); i++) {
                    new_args[i] = widen(new_args[i], max_width);
                }
                expr = new Call(op->type.vector_of(max_width), op->name, new_args, 
                                op->call_type, op->func, op->image, op->param);
            }
        }

        void visit(const Let *op) {
            Expr value = mutate(op->value);
            if (value.type().is_vector()) {
                scope.push(op->name, value.type());
            }

            Expr body = mutate(op->body);

            if (value.type().is_vector()) {
                scope.pop(op->name);
            }

            if (value.same_as(op->value) && body.same_as(op->body)) {
                expr = op;
            } else {
                expr = new Let(op->name, value, body);
            }                
        }

        void visit(const LetStmt *op) {
            Expr value = mutate(op->value);
            if (value.type().is_vector()) {
                scope.push(op->name, value.type());
            }

            Stmt body = mutate(op->body);

            if (value.type().is_vector()) {
                scope.pop(op->name);
            }

            if (value.same_as(op->value) && body.same_as(op->body)) {
                stmt = op;
            } else {
                stmt = new LetStmt(op->name, value, body);
            }                
        }

        void visit(const Provide *op) {
            vector<Expr> new_args(op->args.size());
            bool changed = false;
                
            // Mutate the args
            int max_width = 0;
            for (size_t i = 0; i < op->args.size(); i++) {
                Expr old_arg = op->args[i];
                Expr new_arg = mutate(old_arg);
                if (!new_arg.same_as(old_arg)) changed = true;
                new_args[i] = new_arg;
                max_width = std::max(new_arg.type().width, max_width);
            }
                
            Expr value = mutate(op->value);

            if (!changed && value.same_as(op->value)) stmt = op;
            else {
                // Widen the args to have the same width as the max width found
                for (size_t i = 0; i < new_args.size(); i++) {
                    new_args[i] = widen(new_args[i], max_width);
                }
                value = widen(value, max_width);
                stmt = new Provide(op->buffer, value, new_args);
            }                
        }

    public: 
        VectorSubs(string v, Expr r) : var(v), replacement(r) {
        }
    };
        
    void visit(const For *for_loop) {
        if (for_loop->for_type == For::Vectorized) {
            const IntImm *extent = for_loop->extent.as<IntImm>();
            assert(extent && "Can only vectorize for loops over a constant extent");    

            // Replace the var with a ramp within the body
            Expr for_var = new Variable(Int(32), for_loop->name);                
            Expr replacement = new Ramp(for_var, 1, extent->value);
            Stmt body = VectorSubs(for_loop->name, replacement).mutate(for_loop->body);
                
            // The for loop becomes a simple let statement
            stmt = new LetStmt(for_loop->name, for_loop->min, body);

        } else {
            IRMutator::visit(for_loop);
        }
    }
};

class UnrollLoops : public IRMutator {
    void visit(const For *for_loop) {
        if (for_loop->for_type == For::Unrolled) {
            const IntImm *extent = for_loop->extent.as<IntImm>();
            assert(extent && "Can only unroll for loops over a constant extent");
            Stmt body = mutate(for_loop->body);
                
            Block *block = NULL;
            // Make n copies of the body, each wrapped in a let that defines the loop var for that body
            for (int i = extent->value-1; i >= 0; i--) {
                Stmt iter = new LetStmt(for_loop->name, for_loop->min + i, body);
                block = new Block(iter, block);
            }
            stmt = block;

        } else {
            IRMutator::visit(for_loop);
        }
    }
};

class RemoveDeadLets : public IRMutator {
    Scope<int> references;

    void visit(const Variable *op) {
        if (references.contains(op->name)) references.ref(op->name)++;
        expr = op;
    }

    void visit(const For *op) {            
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        references.push(op->name, 0);
        Stmt body = mutate(op->body);
        references.pop(op->name);
        if (min.same_as(op->min) && extent.same_as(op->extent) && body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = new For(op->name, min, extent, op->for_type, body);
        }
    }

    void visit(const LetStmt *op) {
        references.push(op->name, 0);
        Stmt body = mutate(op->body);
        if (references.get(op->name) > 0) {
            Expr value = mutate(op->value);
            if (body.same_as(op->body) && value.same_as(op->value)) {
                stmt = op;
            } else {
                stmt = new LetStmt(op->name, value, body);
            }
        } else {
            stmt = body;
        }
        references.pop(op->name);
    }

    void visit(const Let *op) {
        references.push(op->name, 0);
        Expr body = mutate(op->body);
        if (references.get(op->name) > 0) {
            Expr value = mutate(op->value);
            if (body.same_as(op->body) && value.same_as(op->value)) {
                expr = op;
            } else {
                expr = new Let(op->name, value, body);
            }
        } else {
            expr = body;
        }
        references.pop(op->name);
    }
};


Stmt lower(Function f) {
    // Compute an environment
    map<string, Function> env;
    populate_environment(f, env);

    // Compute a realization order
    vector<string> order = realization_order(f.name(), env);

    // Generate initial loop nest
    Stmt s = build_realization(env.find(order[order.size()-1])->second);
    s = inject_explicit_bounds(s, f);
    s = new For("<root>", 0, 1, For::Serial, s);

    log(2) << "Initial statement: " << '\n' << s << '\n';
    for (size_t i = order.size()-1; i > 0; i--) {
        Function f = env.find(order[i-1])->second;
        if (f.schedule().compute_level.empty()) {
            log(2) << "Inlining " << order[i-1] << '\n';
            s = InlineFunction(f).mutate(s);
        } else {
            log(2) << "Injecting realization of " << order[i-1] << '\n';
            InjectRealization injector(f);
            s = injector.mutate(s);
            assert(injector.found_store_level && injector.found_compute_level);
        }
        log(2) << s << '\n';
    }

    log(2) << "All realizations injected:\n" << s << '\n';

    s = InjectTracing().mutate(s);

    log(2) << "Tracing injected:\n" << s << '\n';

    // Do bounds inference
    s = BoundsInference(order, env).mutate(s);

    log(2) << "Bounds inference: " << '\n' << s << '\n';

    // For the output function, the bounds required is the size of the buffer
    for (size_t i = 0; i < f.args().size(); i++) {
        ostringstream buf_min_name, buf_extent_name;
        buf_min_name << f.name() << ".min." << i;
        buf_extent_name << f.name() << ".extent." << i;
        Expr buf_min = new Variable(Int(32), buf_min_name.str());
        Expr buf_extent = new Variable(Int(32), buf_extent_name.str());
        s = new LetStmt(f.name() + "." + f.args()[i] + ".min", buf_min, s);
        s = new LetStmt(f.name() + "." + f.args()[i] + ".extent", buf_extent, s);
    }

    log(2) << "Bounds of output buffer: " << '\n' << s << "\n\n";

    // Flatten everything to single-dimensional
    s = FlattenDimensions().mutate(s);

    log(2) << "Storage flattening: " << '\n' << s << "\n\n";

    // Assert that the strides on dimension zero of the input and output buffers are one
    vector<string> bufs = FindBuffers(s).buffers;
    bufs.push_back(f.name());
    for (size_t i = 0; i < bufs.size(); i++) {
        string var_name = bufs[i] + ".stride.0";
        Expr var = new Variable(Int(32), var_name);
        string error_msg = "stride on innermost dimension of " + bufs[i] + " must be one";
        s = new Block(new AssertStmt(var == 1, error_msg), 
                      new LetStmt(var_name, 1, s));
                      
    }

    log(2) << "Set buffer stride 0 to 1: \n" << s << "\n\n";

    // A constant folding pass
    s = simplify(s);

    log(2) << "Simplified: \n" << s << "\n\n";

    // Vectorize loops marked for vectorization
    s = VectorizeLoops().mutate(s);

    log(2) << "Vectorized: \n" << s << "\n\n";

    // Unroll loops marked for unrolling
    s = UnrollLoops().mutate(s);

    log(2) << "Unrolled: \n" << s << "\n\n";

    for (size_t i = 0; i < 2; i++) {
        // Another constant folding pass
        s = simplify(s);
        log(2) << "Simplified: \n" << s << "\n\n";
        
        // Removed useless Let and LetStmt nodes
        //s = RemoveDeadLets().mutate(s);
        //log(2) << "Remove dead lets: \n" << s << "\n\n";
    }

    log(1) << "Lowered statement: \n" << s << "\n\n";

    return s;
} 
   
}
}
