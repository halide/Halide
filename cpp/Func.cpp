#include "Func.h"
#include "Util.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Substitute.h"
#include "Scope.h"
#include "Simplify.h"
#include <iostream>
#include <sstream>
#include <set>

namespace HalideInternal {

    using std::set;
    using std::max;
    using std::min;
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

            virtual void visit(const Var *op) {
                if (op->name == var) {
                    expr = replacement;
                } else if (scope.contains(op->name)) {
                    // The type of a var may have changed. E.g. if
                    // we're vectorizing across x we need to know the
                    // type of y has changed in the following example:
                    // let y = x + 1 in y*3
                    expr = new Var(scope.get(op->name), op->name);
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
                    int w = max(a.type().width, b.type().width);
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
                    int width = max(true_value.type().width, false_value.type().width);
                    width = max(width, condition.type().width);
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
                    expr = new Load(op->type.vector_of(w), op->buffer, index);
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
                    max_width = max(new_arg.type().width, max_width);
                }
                
                if (!changed) expr = op;
                else {
                    // Widen the args to have the same width as the max width found
                    for (size_t i = 0; i < new_args.size(); i++) {
                        new_args[i] = widen(new_args[i], max_width);
                    }
                    expr = new Call(op->type.vector_of(max_width), op->name, new_args, op->call_type);
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
                    max_width = max(new_arg.type().width, max_width);
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
                Expr for_var = new Var(Int(32), for_loop->name);                
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

        void visit(const Var *op) {
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

    Stmt Func::lower(const map<string, Func> &env) {
        // Compute a realization order
        vector<string> order = realization_order(name, env);

        // Generate initial loop nest
        Stmt s = realize(env.find(order[order.size()-1])->second);
        //std::cout << std::endl << "Initial statement: " << std::endl << s << std::endl;
        for (size_t i = order.size()-1; i > 0; i--) {
            //std::cout << std::endl << "Injecting realization of " << order[i-1] << std::endl;
            s = InjectRealization(env.find(order[i-1])->second).mutate(s);
            //std::cout << s << std::endl;
        }

        // Do bounds inference

        // Flatten everything to single-dimensional
        s = FlattenDimensions().mutate(s);

        // A constant folding pass
        s = Simplify().mutate(s);

        // Vectorize loops marked for vectorization
        s = VectorizeLoops().mutate(s);

        // Unroll loops marked for unrolling
        s = UnrollLoops().mutate(s);

        // Another constant folding pass
        s = Simplify().mutate(s);

        // Removed useless Let and LetStmt nodes
        s = RemoveDeadLets().mutate(s);

        return s;
    };


    void Func::test() {
        Expr x = new Var(Int(32), "x");
        Expr y = new Var(Int(32), "y");
        Schedule::Split split_x = {"x", "x_i", "x_o", 4};
        Schedule::Split split_y = {"y", "y_i", "y_o", 2};
        Schedule::Dim dim_x = {"x", For::Serial};
        Schedule::Dim dim_y = {"y", For::Serial};
        Schedule::Dim dim_yo = {"y_o", For::Serial};
        Schedule::Dim dim_yi = {"y_i", For::Unrolled};
        Schedule::Dim dim_xo = {"x_o", For::Parallel};
        Schedule::Dim dim_xi = {"x_i", For::Vectorized};

        map<string, Func> env;

        Schedule f_s = {"<root>", "<root>", vec(split_x), vec(dim_xi, dim_y, dim_xo)};
        Expr f_value = new Call(Int(32), "g", vec<Expr>(x+1, 1), Call::Halide);
        f_value += new Call(Int(32), "g", vec<Expr>(3, x-y), Call::Halide);    
        Func f = {"f", vec<string>("x", "y"), f_value, f_s};
        env["f"] = f;
        
        Schedule g_s = {"f.x_o", "f.y", vec(split_y), vec(dim_yi, dim_yo, dim_x)};
        Func g = {"g", vec<string>("x", "y"), x - y, g_s};
        env["g"] = g;

        Stmt result = f.lower(env);
        assert(result.defined() && "Lowering returned trivial function");

        // std::cout << result << std::endl;

        // TODO: actually assert something here
        std::cout << "Func test passed" << std::endl;
    }
};
