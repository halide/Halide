#ifndef HALIDE_SCHEDULE_H
#define HALIDE_SCHEDULE_H

/** \file
 * Defines the internal representation of the schedule for a function
 */

#include "IR.h"
#include <string>
#include <vector>

namespace Halide {
namespace Internal {

/** A schedule for a halide function, which defines where, when, and
 * how it should be evaluated. */
struct Schedule {
    /** A reference to a site in a Halide statement at the top of the
     * body of a particular for loop. Evaluating a region of a halide
     * function is done by generating a loop nest that spans its
     * dimensions. We schedule the inputs to that function by
     * recursively injecting realizations for them at particular sites
     * in this loop nest. A LoopLevel identifies such a site. */
    struct LoopLevel {
        std::string func, var;

        /** Identify the loop nest corresponding to some dimension of some function */
        LoopLevel(std::string f, std::string v) : func(f), var(v) {}

        /** Construct an empty LoopLevel, which is interpreted as
         * 'inline'. This is a special LoopLevel value that implies
         * that a function should be inlined away */
        LoopLevel() {}

        /** Test if a loop level corresponds to inlining the function */
        bool is_inline() const {return var.empty();}

        /** root is a special LoopLevel value which represents the
         * location outside of all for loops */
        static LoopLevel root() {
            return LoopLevel("", "<root>");
        }
        /** Test if a loop level is 'root', which describes the site
         * outside of all for loops */
        bool is_root() const {return var == "<root>";}

        /** Compare this loop level against the variable name of a for
         * loop, to see if this loop level refers to the site
         * immediately inside this loop. */
        bool match(const std::string &loop) const {
            return starts_with(loop, func + ".") && ends_with(loop, "." + var);
        }

        bool match(const LoopLevel &other) const {
            return (func == other.func &&
                    (var == other.var ||
                     ends_with(var, "." + other.var) ||
                     ends_with(other.var, "." + var)));
        }

    };

    /** At what sites should we inject the allocation and the
     * computation of this function? The store_level must be outside
     * of or equal to the compute_level. If the compute_level is
     * inline, the store_level is meaningless. See \ref Func::store_at
     * and \ref Func::compute_at */
    // @{
    LoopLevel store_level, compute_level, lazy_level;
    // @}

    struct Split {
        std::string old_var, outer, inner;
        Expr factor;

        enum SplitType {SplitVar = 0, RenameVar, FuseVars};

        // If split_type is Rename, then this is just a renaming of the
        // old_var to the outer and not a split. The inner var should
        // be ignored, and factor should be one. Renames are kept in
        // the same list as splits so that ordering between them is
        // respected.

        // If split_type is Fuse, then this does the opposite of a
        // split, it joins the outer and inner into the old_var.
        SplitType split_type;

        bool is_rename() const {return split_type == RenameVar;}
        bool is_split() const {return split_type == SplitVar;}
        bool is_fuse() const {return split_type == FuseVars;}
    };
    /** The traversal of the domain of a function can have some of its
     * dimensions split into sub-dimensions. See
     * \ref ScheduleHandle::split */
    std::vector<Split> splits;

    struct Dim {
        std::string var;
        For::ForType for_type;
    };
    /** The list and ordering of dimensions used to evaluate this
     * function, after all splits have taken place. The first
     * dimension in the vector corresponds to the innermost for loop,
     * and the last is the outermost. Also specifies what type of for
     * loop to use for each dimension. Does not specify the bounds on
     * each dimension. These get inferred from how the function is
     * used, what the splits are, and any optional bounds in the list below. */
    std::vector<Dim> dims;

    /** The list and order of dimensions used to store this
     * function. The first dimension in the vector corresponds to the
     * innermost dimension for storage (i.e. which dimension is
     * tightly packed in memory) */
    std::vector<std::string> storage_dims;

    struct Bound {
        std::string var;
        Expr min, extent;
    };
    /** You may explicitly bound some of the dimensions of a
     * function. See \ref ScheduleHandle::bound */
    std::vector<Bound> bounds;
};

}
}

#endif
