#ifndef FUNC_H
#define FUNC_H

#include "IR.h"

namespace HalideInternal {

    struct Schedule {
        string store_level, compute_level;

        struct Split {
            string old_var, inner, outer;
            Expr factor;
        };
        vector<Split> splits;

        struct Dim {
            string var;
            For::ForType for_type;
        };
        vector<Dim> dims;
    };

    struct Func {
        string name;
        vector<string> args;
        Expr value;
        Schedule schedule;
        // TODO: reduction step lhs, rhs, and schedule        
    };

    Stmt lower(string func, const map<string, Func> &);    

    void test_lowering();
}

#endif
