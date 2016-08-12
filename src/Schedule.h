#ifndef HALIDE_SCHEDULE_H
#define HALIDE_SCHEDULE_H

/** \file
 * Defines the internal representation of the schedule for a function
 */

#include "Expr.h"

#include <map>

namespace Halide {

class Func;
struct VarOrRVar;

namespace Internal {
class Function;
struct FunctionContents;
}  // namespace Internal

/** Different ways to handle a tail case in a split when the
 * factor does not provably divide the extent. */
enum class TailStrategy {
    /** Round up the extent to be a multiple of the split
     * factor. Not legal for RVars, as it would change the meaning
     * of the algorithm. Pros: generates the simplest, fastest
     * code. Cons: if used on a stage that reads from the input or
     * writes to the output, constrains the input or output size
     * to be a multiple of the split factor. */
    RoundUp,

    /** Guard the inner loop with an if statement that prevents
     * evaluation beyond the original extent. Always legal. The if
     * statement is treated like a boundary condition, and
     * factored out into a loop epilogue if possible. Pros: no
     * redundant re-evaluation; does not constrain input our
     * output sizes. Cons: increases code size due to separate
     * tail-case handling; vectorization will scalarize in the tail
     * case to handle the if statement. */
    GuardWithIf,

    /** Prevent evaluation beyond the original extent by shifting
     * the tail case inwards, re-evaluating some points near the
     * end. Only legal for pure variables in pure definitions. If
     * the inner loop is very simple, the tail case is treated
     * like a boundary condition and factored out into an
     * epilogue.
     *
     * This is a good trade-off between several factors. Like
     * RoundUp, it supports vectorization well, because the inner
     * loop is always a fixed size with no data-dependent
     * branching. It increases code size slightly for inner loops
     * due to the epilogue handling, but not for outer loops
     * (e.g. loops over tiles). If used on a stage that reads from
     * an input or writes to an output, this stategy only requires
     * that the input/output extent be at least the split factor,
     * instead of a multiple of the split factor as with RoundUp. */
    ShiftInwards,

    /** For pure definitions use ShiftInwards. For pure vars in
     * update definitions use RoundUp. For RVars in update
     * definitions use GuardWithIf. */
    Auto
};

/** A reference to a site in a Halide statement at the top of the
 * body of a particular for loop. Evaluating a region of a halide
 * function is done by generating a loop nest that spans its
 * dimensions. We schedule the inputs to that function by
 * recursively injecting realizations for them at particular sites
 * in this loop nest. A LoopLevel identifies such a site. */
class LoopLevel {
    // Note: func_ is nullptr for inline or root.
    Internal::IntrusivePtr<Internal::FunctionContents> function_contents;
    // TODO: these two fields should really be VarOrRVar, 
    // but cyclical include dependencies make this challenging.
    std::string var_name;
    bool is_rvar;

    EXPORT LoopLevel(Internal::IntrusivePtr<Internal::FunctionContents> f, const std::string &var_name, bool is_rvar);
    EXPORT std::string func_name() const;

public:
    /** Identify the loop nest corresponding to some dimension of some function */
    // @{
    EXPORT LoopLevel(Internal::Function f, VarOrRVar v);
    EXPORT LoopLevel(Func f, VarOrRVar v);
    // @}

    /** Construct an empty LoopLevel, which is interpreted as
     * 'inline'. This is a special LoopLevel value that implies
     * that a function should be inlined away */
    LoopLevel() : function_contents(nullptr), var_name(""), is_rvar(false) {}

    /** Return the Func. Asserts if the LoopLevel is_root() or is_inline(). */
    EXPORT Func func() const;

    /** Return the VarOrRVar. Asserts if the LoopLevel is_root() or is_inline(). */
    EXPORT VarOrRVar var() const;

    /** Test if a loop level corresponds to inlining the function */
    EXPORT bool is_inline() const;

    /** root is a special LoopLevel value which represents the
     * location outside of all for loops */
    EXPORT static LoopLevel root();

    /** Test if a loop level is 'root', which describes the site
     * outside of all for loops */
    EXPORT bool is_root() const;

    /** Return a string of the form func.var -- note that this is safe
     * to call for root or inline LoopLevels. */
    EXPORT std::string to_string() const;

    /** Compare this loop level against the variable name of a for
     * loop, to see if this loop level refers to the site
     * immediately inside this loop. */
    EXPORT bool match(const std::string &loop) const;

    EXPORT bool match(const LoopLevel &other) const;

    /** Check if two loop levels are exactly the same. */
    EXPORT bool operator==(const LoopLevel &other) const;
};

namespace Internal {

class IRMutator;
struct ReductionVariable;

struct Split {
    std::string old_var, outer, inner;
    Expr factor;
    bool exact; // Is it required that the factor divides the extent
                // of the old var. True for splits of RVars. Forces
                // tail strategy to be GuardWithIf.
    TailStrategy tail;

    enum SplitType {SplitVar = 0, RenameVar, FuseVars, PurifyRVar};

    // If split_type is Rename, then this is just a renaming of the
    // old_var to the outer and not a split. The inner var should
    // be ignored, and factor should be one. Renames are kept in
    // the same list as splits so that ordering between them is
    // respected.

    // If split type is Purify, this replaces the old_var RVar to
    // the outer Var. The inner var should be ignored, and factor
    // should be one.

    // If split_type is Fuse, then this does the opposite of a
    // split, it joins the outer and inner into the old_var.
    SplitType split_type;

    bool is_rename() const {return split_type == RenameVar;}
    bool is_split() const {return split_type == SplitVar;}
    bool is_fuse() const {return split_type == FuseVars;}
    bool is_purify() const {return split_type == PurifyRVar;}
};

struct Dim {
    std::string var;
    ForType for_type;
    DeviceAPI device_api;

    enum Type {PureVar = 0, PureRVar, ImpureRVar};
    Type dim_type;

    bool is_pure() const {return (dim_type == PureVar) || (dim_type == PureRVar);}
    bool is_rvar() const {return (dim_type == PureRVar) || (dim_type == ImpureRVar);}
};

struct Bound {
    std::string var;
    Expr min, extent, modulus, remainder;
};

struct ScheduleContents;

struct StorageDim {
    std::string var;
    Expr alignment;
    Expr fold_factor;
    bool fold_forward;
};

class ReductionDomain;

struct FunctionContents;

/** A schedule for a single stage of a Halide pipeline. Right now this
 * interface is basically a struct, offering mutable access to its
 * innards. In the future it may become more encapsulated. */
class Schedule {
    IntrusivePtr<ScheduleContents> contents;

public:

    Schedule(IntrusivePtr<ScheduleContents> c) : contents(c) {}
    Schedule(const Schedule &other) : contents(other.contents) {}
    EXPORT Schedule();

    /** Return a deep copy of this Schedule. It recursively deep copies all called
     * functions, schedules, specializations, and reduction domains. This method
     * takes a map of <old FunctionContents, deep-copied version> as input and
     * would use the deep-copied FunctionContents from the map if exists instead
     * of creating a new deep-copy to avoid creating deep-copies of the same
     * FunctionContents multiple times.
     */
    EXPORT Schedule deep_copy(
        std::map<IntrusivePtr<FunctionContents>, IntrusivePtr<FunctionContents>> &copied_map) const;

    /** This flag is set to true if the schedule is memoized. */
    // @{
    bool &memoized();
    bool memoized() const;
    // @}

    /** This flag is set to true if the dims list has been manipulated
     * by the user (or if a ScheduleHandle was created that could have
     * been used to manipulate it). It controls the warning that
     * occurs if you schedule the vars of the pure step but not the
     * update steps. */
    // @{
    bool &touched();
    bool touched() const;
    // @}

    /** The traversal of the domain of a function can have some of its
     * dimensions split into sub-dimensions. See ScheduleHandle::split */
    // @{
    const std::vector<Split> &splits() const;
    std::vector<Split> &splits();
    // @}

    /** The list and ordering of dimensions used to evaluate this
     * function, after all splits have taken place. The first
     * dimension in the vector corresponds to the innermost for loop,
     * and the last is the outermost. Also specifies what type of for
     * loop to use for each dimension. Does not specify the bounds on
     * each dimension. These get inferred from how the function is
     * used, what the splits are, and any optional bounds in the list below. */
    // @{
    const std::vector<Dim> &dims() const;
    std::vector<Dim> &dims();
    // @}

    /** RVars of reduction domain associated with this schedule if there is any. */
    // @{
    const std::vector<ReductionVariable> &rvars() const;
    std::vector<ReductionVariable> &rvars();
    // @}

    /** The list and order of dimensions used to store this
     * function. The first dimension in the vector corresponds to the
     * innermost dimension for storage (i.e. which dimension is
     * tightly packed in memory) */
    // @{
    const std::vector<StorageDim> &storage_dims() const;
    std::vector<StorageDim> &storage_dims();
    // @}

    /** You may explicitly bound some of the dimensions of a function,
     * or constrain them to lie on multiples of a given factor. See
     * \ref Func::bound and \ref Func::align_bounds */
    // @{
    const std::vector<Bound> &bounds() const;
    std::vector<Bound> &bounds();
    // @}

    /** Mark calls of a function by 'f' to be replaced with its wrapper
     * during the lowering stage. If the string 'f' is empty, it means replace
     * all calls to the function by all other functions (excluding itself) in
     * the pipeline with the wrapper. See \ref Func::in for more details. */
    // @{
    const std::map<std::string, IntrusivePtr<Internal::FunctionContents>> &wrappers() const;
    std::map<std::string, IntrusivePtr<Internal::FunctionContents>> &wrappers();
    EXPORT void add_wrapper(const std::string &f,
                            const IntrusivePtr<Internal::FunctionContents> &wrapper);
    // @}

    /** At what sites should we inject the allocation and the
     * computation of this function? The store_level must be outside
     * of or equal to the compute_level. If the compute_level is
     * inline, the store_level is meaningless. See \ref Func::store_at
     * and \ref Func::compute_at */
    // @{
    const LoopLevel &store_level() const;
    const LoopLevel &compute_level() const;
    LoopLevel &store_level();
    LoopLevel &compute_level();
    // @}

    /** Are race conditions permitted? */
    // @{
    bool allow_race_conditions() const;
    bool &allow_race_conditions();
    // @}

    /** Pass an IRVisitor through to all Exprs referenced in the
     * Schedule. */
    void accept(IRVisitor *) const;

    /** Pass an IRMutator through to all Exprs referenced in the
     * Schedule. */
    void mutate(IRMutator *);
};

}
}

#endif
