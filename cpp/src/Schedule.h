#ifndef HALIDE_SCHEDULE_H
#define HALIDE_SCHEDULE_H

#include "IR.h"
#include <string>
#include <vector>

namespace Halide {
namespace Internal {

        
struct Schedule {
    struct LoopLevel {
        std::string func, var;

        // Some variable of some function
        LoopLevel(std::string f, std::string v) : func(f), var(v) {}

        // Represents inline
        LoopLevel() {} 
        bool is_inline() const {return var.empty();}

        // Represents outside the outermost loop
        static LoopLevel root() {
            return LoopLevel("", "<root>");
        }
        bool is_root() const {return var == "<root>";}

        // Does this loop level refer to a particular for loop variable?
        bool match(const std::string &loop) const {
            return starts_with(loop, func + ".") && ends_with(loop, "." + var);
        }

    };

    // At what granularity do we store and compute values of this function?
    LoopLevel store_level, compute_level;
      
    // Which dimensions have been split into inner and outer sub-dimensions
    struct Split {
        std::string old_var, outer, inner;
        Expr factor;
    };
    std::vector<Split> splits;
        
    // The list of dimensions after splits, and how to traverse each dimension
    struct Dim {
        std::string var;
        For::ForType for_type;
    };
    std::vector<Dim> dims;

    // Any (optional) explicit bounds on dimensions
    struct Bound {
        std::string var;
        Expr min, extent;
    };
    std::vector<Bound> bounds;
};

}
}

#endif
