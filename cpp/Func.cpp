#include "Func.h"
#include "Util.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Substitute.h"
#include <iostream>

namespace HalideInternal {

    // Turn a function into a loop nest that computes it. It will
    // refer to external vars of the form function_name.arg_name.min
    // and function_name.arg_name.extent to define the bounds over
    // which it should be realized. It will compute at least those
    // bounds (depending on splits, it may compute more).
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
            stmt = new For(dim.var, min, extent, dim.for_type, stmt);
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

    Stmt lower(string func, const map<string, Func> &env) {
        const Func &f = env.find(func)->second;
        return realize(f);
        // 1) Compute a realization order
        // 2) 
    };


    void test_lowering() {
        Expr x = new Var(Int(32), "x");
        Expr y = new Var(Int(32), "y");
        Schedule::Split split = {"x", "x_i", "x_o", 4};
        Schedule::Dim dim_y = {"y", For::Serial};
        Schedule::Dim dim_i = {"x_i", For::Vectorized};
        Schedule::Dim dim_o = {"x_o", For::Parallel};
        Schedule s = {"<root>", "<root>", vec(split), vec(dim_i, dim_y, dim_o)};
        Func f = {"f", vec<string>("x", "y"), x + y, s};
        
        std::cout << realize(f) << std::endl;
    }
};
