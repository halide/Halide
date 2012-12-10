#ifndef HALIDE_FUNCTION_H
#define HALIDE_FUNCTION_H

#include "Var.h"
#include <string>
#include <vector>

namespace Halide { namespace Internal {

    using std::vector;
    using std::string;
        
    struct Schedule {
        string store_level, compute_level;
        
        struct Split {
            Var old_var, outer, inner;
            Expr factor;
        };
        vector<Split> splits;
        
        struct Dim {
            Var var;
            For::ForType for_type;
        };
        vector<Dim> dims;
    };
        
    struct Function {
        mutable int ref_count;
        string name;
        vector<Var> args;
        Expr value;
        Schedule schedule;
        // TODO: reduction step lhs, rhs, and schedule
    };        
        
}}

#endif
