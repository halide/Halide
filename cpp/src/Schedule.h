#ifndef HALIDE_SCHEDULE_H
#define HALIDE_SCHEDULE_H

#include "IR.h"

namespace Halide {
namespace Internal {

        
struct Schedule {
    struct LoopLevel {
        string func, var;

        // Some variable of some function
        LoopLevel(string f, string v) : func(f), var(v) {}

        // Represents inline
        LoopLevel() {} 
        bool is_inline() const {return var.empty();}

        // Represents outside the outermost loop
        static LoopLevel root() {
            return LoopLevel("", "<root>");
        }
        bool is_root() const {return var == "<root>";}

        // Does this loop level refer to a particular for loop variable?
        bool match(const string &loop) const {
            return starts_with(loop, func + ".") && ends_with(loop, "." + var);
        }

    };

    // At what granularity do we store and compute values of this function?
    LoopLevel store_level, compute_level;
      
    // Which dimensions have been split into inner and outer sub-dimensions
    struct Split {
        string old_var, outer, inner;
        Expr factor;
    };
    vector<Split> splits;
        
    // The list of dimensions after splits, and how to traverse each dimension
    struct Dim {
        string var;
        For::ForType for_type;
    };
    vector<Dim> dims;

    // Any (optional) explicit bounds on dimensions
    struct Bound {
        string var;
        Expr min, extent;
    };
    vector<Bound> bounds;
};

}
}

#endif
