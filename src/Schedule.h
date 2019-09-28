#ifndef HALIDE_SCHEDULE_H
#define HALIDE_SCHEDULE_H

/** \file
 * Defines the internal representation of the schedule for a function
 */

#include "Expr.h"
#include "FunctionPtr.h"
#include "Parameter.h"

#include <map>

namespace Halide {

class Func;
struct VarOrRVar;

namespace Internal {
class Function;
struct FunctionContents;
struct LoopLevelContents;
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

/** Different ways to handle the case when the start/end of the loops of stages
 * computed with (fused) are not aligned. */
enum class LoopAlignStrategy {
    /** Shift the start of the fused loops to align. */
    AlignStart,

    /** Shift the end of the fused loops to align. */
    AlignEnd,

    /** compute_with will make no attempt to align the start/end of the
     * fused loops. */
    NoAlign,

    /** By default, LoopAlignStrategy is set to NoAlign. */
    Auto
};

/** Different ways to handle accesses outside the original extents in a prefetch. */
enum class PrefetchBoundStrategy {
    /** Clamp the prefetched exprs by intersecting the prefetched region with
     * the original extents. This may make the exprs of the prefetched region
     * more complicated. */
    Clamp,

    /** Guard the prefetch with if-guards that ignores the prefetch if
     * any of the prefetched region ever goes beyond the original extents
     * (i.e. all or nothing). */
    GuardWithIf,

    /** Leave the prefetched exprs as are (no if-guards around the prefetch
     * and no intersecting with the original extents). This makes the prefetch
     * exprs simpler but this may cause prefetching of region outside the original
     * extents. This is good if prefetch won't fault when accessing region
     * outside the original extents. */
    NonFaulting
};

/** A reference to a site in a Halide statement at the top of the
 * body of a particular for loop. Evaluating a region of a halide
 * function is done by generating a loop nest that spans its
 * dimensions. We schedule the inputs to that function by
 * recursively injecting realizations for them at particular sites
 * in this loop nest. A LoopLevel identifies such a site. The site
 * can either be a loop nest within all stages of a function
 * or it can refer to a loop nest within a particular function's
 * stage (initial definition or updates).
 *
 * Note that a LoopLevel is essentially a pointer to an underlying value;
 * all copies of a LoopLevel refer to the same site, so mutating one copy
 * (via the set() method) will effectively mutate all copies:
 \code
 Func f;
 Var x;
 LoopLevel a(f, x);
 // Both a and b refer to LoopLevel(f, x)
 LoopLevel b = a;
 // Now both a and b refer to LoopLevel::root()
 a.set(LoopLevel::root());
 \endcode
 * This is quite useful when splitting Halide code into utility libraries, as it allows
 * a library to schedule code according to a caller's specifications, even if the caller
 * hasn't fully defined its pipeline yet:
 \code
 Func demosaic(Func input,
              LoopLevel intermed_compute_at,
              LoopLevel intermed_store_at,
              LoopLevel output_compute_at) {
    Func intermed = ...;
    Func output = ...;
    intermed.compute_at(intermed_compute_at).store_at(intermed_store_at);
    output.compute_at(output_compute_at);
    return output;
 }

 void process() {
     // Note that these LoopLevels are all undefined when we pass them to demosaic()
     LoopLevel intermed_compute_at, intermed_store_at, output_compute_at;
     Func input = ...;
     Func demosaiced = demosaic(input, intermed_compute_at, intermed_store_at, output_compute_at);
     Func output = ...;

     // We need to ensure all LoopLevels have a well-defined value prior to lowering:
     intermed_compute_at.set(LoopLevel(output, y));
     intermed_store_at.set(LoopLevel(output, y));
     output_compute_at.set(LoopLevel(output, x));
 }
 \endcode
 */
class LoopLevel {
    Internal::IntrusivePtr<Internal::LoopLevelContents> contents;

    explicit LoopLevel(Internal::IntrusivePtr<Internal::LoopLevelContents> c)
        : contents(c) {
    }
    LoopLevel(const std::string &func_name, const std::string &var_name,
              bool is_rvar, int stage_index, bool locked = false);

public:
    /** Return the index of the function stage associated with this loop level.
     * Asserts if undefined */
    int stage_index() const;

    /** Identify the loop nest corresponding to some dimension of some function */
    // @{
    LoopLevel(const Internal::Function &f, VarOrRVar v, int stage_index = -1);
    LoopLevel(const Func &f, VarOrRVar v, int stage_index = -1);
    // @}

    /** Construct an undefined LoopLevel. Calling any method on an undefined
     * LoopLevel (other than set()) will assert. */
    LoopLevel();

    /** Construct a special LoopLevel value that implies
     * that a function should be inlined away. */
    static LoopLevel inlined();

    /** Construct a special LoopLevel value which represents the
     * location outside of all for loops. */
    static LoopLevel root();

    /** Mutate our contents to match the contents of 'other'. */
    void set(const LoopLevel &other);

    // All the public methods below this point are meant only for internal
    // use by Halide, rather than user code; hence, they are deliberately
    // documented with plain comments (rather than Doxygen) to avoid being
    // present in user documentation.

    // Lock this LoopLevel.
    LoopLevel &lock();

    // Return the Func name. Asserts if the LoopLevel is_root() or is_inlined() or !defined().
    std::string func() const;

    // Return the VarOrRVar. Asserts if the LoopLevel is_root() or is_inlined() or !defined().
    VarOrRVar var() const;

    // Return true iff the LoopLevel is defined. (Only LoopLevels created
    // with the default ctor are undefined.)
    bool defined() const;

    // Test if a loop level corresponds to inlining the function.
    bool is_inlined() const;

    // Test if a loop level is 'root', which describes the site
    // outside of all for loops.
    bool is_root() const;

    // Return a string of the form func.var -- note that this is safe
    // to call for root or inline LoopLevels, but asserts if !defined().
    std::string to_string() const;

    // Compare this loop level against the variable name of a for
    // loop, to see if this loop level refers to the site
    // immediately inside this loop. Asserts if !defined().
    bool match(const std::string &loop) const;

    bool match(const LoopLevel &other) const;

    // Check if two loop levels are exactly the same.
    bool operator==(const LoopLevel &other) const;

    bool operator!=(const LoopLevel &other) const {
        return !(*this == other);
    }

private:
    void check_defined() const;
    void check_locked() const;
    void check_defined_and_locked() const;
};

struct FuseLoopLevel {
    LoopLevel level;
    /** Contains alignment strategies for the fused dimensions (indexed by the
     * dimension name). If not in the map, use the default alignment strategy
     * to align the fused dimension (see \ref LoopAlignStrategy::Auto).
     */
    std::map<std::string, LoopAlignStrategy> align;

    FuseLoopLevel()
        : level(LoopLevel::inlined().lock()) {
    }
    FuseLoopLevel(const LoopLevel &level, const std::map<std::string, LoopAlignStrategy> &align)
        : level(level), align(align) {
    }
};

namespace Internal {

class IRMutator;
struct ReductionVariable;

struct Split {
    std::string old_var, outer, inner;
    Expr factor;
    bool exact;  // Is it required that the factor divides the extent
        // of the old var. True for splits of RVars. Forces
        // tail strategy to be GuardWithIf.
    TailStrategy tail;

    enum SplitType { SplitVar = 0,
                     RenameVar,
                     FuseVars,
                     PurifyRVar };

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

    bool is_rename() const {
        return split_type == RenameVar;
    }
    bool is_split() const {
        return split_type == SplitVar;
    }
    bool is_fuse() const {
        return split_type == FuseVars;
    }
    bool is_purify() const {
        return split_type == PurifyRVar;
    }
};

struct Dim {
    std::string var;
    ForType for_type;
    DeviceAPI device_api;

    enum Type { PureVar = 0,
                PureRVar,
                ImpureRVar };
    Type dim_type;

    bool is_pure() const {
        return (dim_type == PureVar) || (dim_type == PureRVar);
    }
    bool is_rvar() const {
        return (dim_type == PureRVar) || (dim_type == ImpureRVar);
    }
    bool is_unordered_parallel() const {
        return Halide::Internal::is_unordered_parallel(for_type);
    }
    bool is_parallel() const {
        return Halide::Internal::is_parallel(for_type);
    }
};

struct Bound {
    std::string var;
    Expr min, extent, modulus, remainder;
};

struct StorageDim {
    std::string var;
    Expr alignment;
    Expr fold_factor;
    bool fold_forward;
};

/** This represents two stages with fused loop nests from outermost to a specific
 * loop level. The loops to compute func_1(stage_1) are fused with the loops to
 * compute func_2(stage_2) from outermost to loop level var_name and the
 * computation from stage_1 of func_1 occurs first.
 */
struct FusedPair {
    std::string func_1;
    std::string func_2;
    size_t stage_1;
    size_t stage_2;
    std::string var_name;

    FusedPair() = default;
    FusedPair(const std::string &f1, size_t s1, const std::string &f2,
              size_t s2, const std::string &var)
        : func_1(f1), func_2(f2), stage_1(s1), stage_2(s2), var_name(var) {
    }

    bool operator==(const FusedPair &other) const {
        return (func_1 == other.func_1) && (func_2 == other.func_2) &&
               (stage_1 == other.stage_1) && (stage_2 == other.stage_2) &&
               (var_name == other.var_name);
    }
    bool operator<(const FusedPair &other) const {
        if (func_1 != other.func_1) {
            return func_1 < other.func_1;
        }
        if (func_2 != other.func_2) {
            return func_2 < other.func_2;
        }
        if (var_name != other.var_name) {
            return var_name < other.var_name;
        }
        if (stage_1 != other.stage_1) {
            return stage_1 < other.stage_1;
        }
        return stage_2 < other.stage_2;
    }
};

struct PrefetchDirective {
    std::string name;
    std::string var;
    Expr offset;
    PrefetchBoundStrategy strategy;
    // If it's a prefetch load from an image parameter, this points to that.
    Parameter param;
};

struct FuncScheduleContents;
struct StageScheduleContents;
struct FunctionContents;

/** A schedule for a Function of a Halide pipeline. This schedule is
 * applied to all stages of the Function. Right now this interface is
 * basically a struct, offering mutable access to its innards.
 * In the future it may become more encapsulated. */
class FuncSchedule {
    IntrusivePtr<FuncScheduleContents> contents;

public:
    FuncSchedule(IntrusivePtr<FuncScheduleContents> c)
        : contents(c) {
    }
    FuncSchedule(const FuncSchedule &other)
        : contents(other.contents) {
    }
    FuncSchedule();

    /** Return a deep copy of this FuncSchedule. It recursively deep copies all
     * called functions, schedules, specializations, and reduction domains. This
     * method takes a map of <old FunctionContents, deep-copied version> as input
     * and would use the deep-copied FunctionContents from the map if exists
     * instead of creating a new deep-copy to avoid creating deep-copies of the
     * same FunctionContents multiple times.
     */
    FuncSchedule deep_copy(
        std::map<FunctionPtr, FunctionPtr> &copied_map) const;

    /** This flag is set to true if the schedule is memoized. */
    // @{
    bool &memoized();
    bool memoized() const;
    // @}

    /** Is the production of this Function done asynchronously */
    bool &async();
    bool async() const;

    /** The list and order of dimensions used to store this
     * function. The first dimension in the vector corresponds to the
     * innermost dimension for storage (i.e. which dimension is
     * tightly packed in memory) */
    // @{
    const std::vector<StorageDim> &storage_dims() const;
    std::vector<StorageDim> &storage_dims();
    // @}

    /** The memory type (heap/stack/shared/etc) used to back this Func. */
    // @{
    MemoryType memory_type() const;
    MemoryType &memory_type();
    // @}

    /** You may explicitly bound some of the dimensions of a function,
     * or constrain them to lie on multiples of a given factor. See
     * \ref Func::bound and \ref Func::align_bounds */
    // @{
    const std::vector<Bound> &bounds() const;
    std::vector<Bound> &bounds();
    // @}

    /** You may explicitly specify an estimate of some of the function
     * dimensions. See \ref Func::estimate */
    // @{
    const std::vector<Bound> &estimates() const;
    std::vector<Bound> &estimates();
    // @}

    /** Mark calls of a function by 'f' to be replaced with its identity
     * wrapper or clone during the lowering stage. If the string 'f' is empty,
     * it means replace all calls to the function by all other functions
     * (excluding itself) in the pipeline with the global identity wrapper.
     * See \ref Func::in and \ref Func::clone_in for more details. */
    // @{
    const std::map<std::string, Internal::FunctionPtr> &wrappers() const;
    std::map<std::string, Internal::FunctionPtr> &wrappers();
    void add_wrapper(const std::string &f,
                     const Internal::FunctionPtr &wrapper);
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

    /** Pass an IRVisitor through to all Exprs referenced in the
     * Schedule. */
    void accept(IRVisitor *) const;

    /** Pass an IRMutator through to all Exprs referenced in the
     * Schedule. */
    void mutate(IRMutator *);
};

/** A schedule for a single stage of a Halide pipeline. Right now this
 * interface is basically a struct, offering mutable access to its
 * innards. In the future it may become more encapsulated. */
class StageSchedule {
    IntrusivePtr<StageScheduleContents> contents;

public:
    StageSchedule(IntrusivePtr<StageScheduleContents> c)
        : contents(c) {
    }
    StageSchedule(const StageSchedule &other)
        : contents(other.contents) {
    }
    StageSchedule();

    /** Return a copy of this StageSchedule. */
    StageSchedule get_copy() const;

    /** This flag is set to true if the dims list has been manipulated
     * by the user (or if a ScheduleHandle was created that could have
     * been used to manipulate it). It controls the warning that
     * occurs if you schedule the vars of the pure step but not the
     * update steps. */
    // @{
    bool &touched();
    bool touched() const;
    // @}

    /** RVars of reduction domain associated with this schedule if there is any. */
    // @{
    const std::vector<ReductionVariable> &rvars() const;
    std::vector<ReductionVariable> &rvars();
    // @}

    /** The traversal of the domain of a function can have some of its
     * dimensions split into sub-dimensions. See \ref Func::split */
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

    /** You may perform prefetching in some of the dimensions of a
     * function. See \ref Func::prefetch */
    // @{
    const std::vector<PrefetchDirective> &prefetches() const;
    std::vector<PrefetchDirective> &prefetches();
    // @}

    /** Innermost loop level of fused loop nest for this function stage.
     * Fusion runs from outermost to this loop level. The stages being fused
     * should not have producer/consumer relationship. See \ref Func::compute_with
     * and \ref Stage::compute_with */
    // @{
    const FuseLoopLevel &fuse_level() const;
    FuseLoopLevel &fuse_level();
    // @}

    /** List of function stages that are to be fused with this function stage
     * from the outermost loop to a certain loop level. Those function stages
     * are to be computed AFTER this function stage at the last fused loop level.
     * This list is populated when realization_order() is called. See
     * \ref Func::compute_with and \ref Stage::compute_with */
    // @{
    const std::vector<FusedPair> &fused_pairs() const;
    std::vector<FusedPair> &fused_pairs();

    /** Are race conditions permitted? */
    // @{
    bool allow_race_conditions() const;
    bool &allow_race_conditions();
    // @}

    /** Use atomic update? */
    // @{
    bool atomic() const;
    bool &atomic();
    // @}

    /** Atomic updates are only allowed on associative reduction.
     *  We try to prove the associativity, but the user can override
     *  the associativity test and supress compiler error if the prover
     *  fails to recognize the associativity or the user does not care. */
    // @{
    bool override_atomic_associativity_test() const;
    bool &override_atomic_associativity_test();
    // @}

    /** Pass an IRVisitor through to all Exprs referenced in the
     * Schedule. */
    void accept(IRVisitor *) const;

    /** Pass an IRMutator through to all Exprs referenced in the
     * Schedule. */
    void mutate(IRMutator *);
};

}  // namespace Internal
}  // namespace Halide

#endif
