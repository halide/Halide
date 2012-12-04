#include "Func.h"
#include "Util.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Substitute.h"
#include <iostream>
#include <sstream>
#include <set>

namespace HalideInternal {

    using std::set;
    using std::ostringstream;

    // Turn a function into a loop nest that computes it. It will
    // refer to external vars of the form function_name.arg_name.min
    // and function_name.arg_name.extent to define the bounds over
    // which it should be realized. It will compute at least those
    // bounds (depending on splits, it may compute more). This loop
    // won't do any allocation.
    Stmt realize(const Func &f) {
        // We'll build it from inside out. 
        
        // All names will get prepended with the function name to avoid ambiguities
        string prefix = f.name + ".";

        // Compute the site to store to as the function args
        vector<Expr> site;
        for (size_t i = 0; i < f.args.size(); i++) {
            site.push_back(new Var(Int(32), prefix + f.args[i]));
        }

        // Fully qualify the var names in the function rhs
        Expr value = f.value;
        for (size_t i = 0; i < f.args.size(); i++) {
            value = substitute(f.args[i], new Var(Int(32), prefix + f.args[i]), value);
        }

        // Make the (multi-dimensional) store node
        Stmt stmt = new Provide(f.name, value, site);

        // Define the function args in terms of the loop variables using the splits
        for (size_t i = f.schedule.splits.size(); i > 0; i--) {
            const Schedule::Split &split = f.schedule.splits[i-1];
            Expr inner = new Var(Int(32), prefix + split.inner);
            Expr outer = new Var(Int(32), prefix + split.outer);
            Expr old_min = new Var(Int(32), prefix + split.old_var + ".min");
            stmt = new LetStmt(prefix + split.old_var, outer * split.factor + inner + old_min, stmt);
        }
            
        // Build the loop nest
        for (size_t i = 0; i < f.schedule.dims.size(); i++) {
            const Schedule::Dim &dim = f.schedule.dims[i];
            Expr min = new Var(Int(32), prefix + dim.var + ".min");
            Expr extent = new Var(Int(32), prefix + dim.var + ".extent");
            stmt = new For(prefix + dim.var, min, extent, dim.for_type, stmt);
        }

        // Define the bounds on the split dimensions using the bounds
        // on the function args
        for (size_t i = f.schedule.splits.size(); i > 0; i--) {
            const Schedule::Split &split = f.schedule.splits[i-1];
            Expr old_var_extent = new Var(Int(32), prefix + split.old_var + ".extent");
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

    // Inject the allocation and realization of a function into an
    // existing loop nest using its schedule
    class InjectRealization : public IRMutator {
    public:
        const Func &func;
        bool found_store_level, found_compute_level;
        InjectRealization(const Func &f) : func(f) {}

        virtual void visit(const For *for_loop) {            
            if (for_loop->name == func.schedule.store_level) {
                // Inject the realization lower down
                Stmt body = mutate(for_loop->body);
                vector<pair<Expr, Expr> > bounds(func.args.size());
                for (size_t i = 0; i < func.args.size(); i++) {
                    string prefix = func.name + "." + func.args[i];
                    bounds[i].first = new Var(Int(32), prefix + ".min");
                    bounds[i].second = new Var(Int(32), prefix + ".extent");
                }
                // Change the body of the for loop to do an allocation
                body = new Realize(func.name, func.value.type(), bounds, body);
                stmt = new For(for_loop->name, 
                               for_loop->min, 
                               for_loop->extent, 
                               for_loop->for_type, 
                               body);
                found_store_level = true;
            } else if (for_loop->name == func.schedule.compute_level) {
                assert(found_store_level && "The compute loop level is outside the store loop level!");
                Stmt produce = realize(func);
                stmt = new Pipeline(func.name, produce, NULL, for_loop);
                found_compute_level = true;
            } else {
                stmt = new For(for_loop->name, 
                               for_loop->min, 
                               for_loop->extent, 
                               for_loop->for_type, 
                               mutate(for_loop->body));                
            }
        }
    };

    /* Find all the internal halide calls in an expr */
    class FindCalls : public IRVisitor {
    public:
        FindCalls(Expr e) {e.accept(this);}
        set<string> calls;
        void visit(const Call *call) {
            if (call->call_type == Call::Halide) calls.insert(call->name);
        }
    };

    vector<string> realization_order(string output, const map<string, Func> &env) {
        // Make a DAG representing the pipeline. Each function maps to the set describing its inputs.
        map<string, set<string> > graph;

        // Populate the graph
        // TODO: consider dependencies of reductions
        for (map<string, Func>::const_iterator iter = env.begin(); iter != env.end(); iter++) {
            graph[iter->first] = FindCalls(iter->second.value).calls;
        }

        vector<string> result;
        set<string> result_set;

        while (true) {
            // Find a function not in result_set, for which all its inputs are
            // in result_set. Stop when we reach the output function.
            bool scheduled_something = false;
            for (map<string, Func>::const_iterator iter = env.begin(); iter != env.end(); iter++) {
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
                Expr stride = new Var(Int(32), stride_name.str());
                Expr min = new Var(Int(32), min_name.str());
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
                Expr prev_stride = new Var(Int(32), prev_stride_name.str());
                Expr prev_extent = new Var(Int(32), prev_extent_name.str());
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
                expr = new Load(call->type, call->name, idx);
            }
        }

    };

    class VectorizeAndUnroll : public IRMutator {
    };

    Stmt lower(string func, const map<string, Func> &env) {
        // 1) Compute a realization order
        vector<string> order = realization_order(func, env);

        // 2) Generate initial loop nest
        Stmt s = realize(env.find(order[order.size()-1])->second);
        std::cout << std::endl << "Initial statement: " << std::endl << s << std::endl;
        for (size_t i = order.size()-1; i > 0; i--) {
            std::cout << std::endl << "Injecting realization of " << order[i-1] << std::endl;
            s = InjectRealization(env.find(order[i-1])->second).mutate(s);
            std::cout << s << std::endl;
        }

        // 3) Do bounds inference        

        // 4) Flatten everything to single-dimensional
        s = FlattenDimensions().mutate(s);

        // 5) Vectorization and Unrolling
        s = VectorizeAndUnroll().mutate(s);

        return s;
    };


    void test_lowering() {
        Expr x = new Var(Int(32), "x");
        Expr y = new Var(Int(32), "y");
        Schedule::Split split = {"x", "x_i", "x_o", 4};
        Schedule::Dim dim_x = {"x", For::Serial};
        Schedule::Dim dim_y = {"y", For::Serial};
        Schedule::Dim dim_i = {"x_i", For::Vectorized};
        Schedule::Dim dim_o = {"x_o", For::Parallel};

        map<string, Func> env;

        Schedule f_s = {"<root>", "<root>", vec(split), vec(dim_i, dim_y, dim_o)};
        Expr f_value = new Call(Int(32), "g", vec<Expr>(x+1, 1), Call::Halide);
        f_value += new Call(Int(32), "g", vec<Expr>(3, x-y), Call::Halide);    
        Func f = {"f", vec<string>("x", "y"), f_value, f_s};
        env["f"] = f;
        
        Schedule g_s = {"f.x_o", "f.y", vector<Schedule::Split>(), vec(dim_y, dim_x)};
        Func g = {"g", vec<string>("x", "y"), x - y, g_s};
        env["g"] = g;

        std::cout << lower("f", env) << std::endl;
    }
};
