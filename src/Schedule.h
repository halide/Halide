#ifndef HALIDE_SCHEDULE_H
#define HALIDE_SCHEDULE_H

/** \file
 * Defines the internal representation of the schedule for a function
 */

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "DeviceAPI.h"
#include "Expr.h"
#include "FunctionPtr.h"
#include "Parameter.h"
#include "PrefetchDirective.h"

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

    /** Guard the loads and stores in the loop with an if statement
     * that prevents evaluation beyond the original extent. Always
     * legal. The if statement is treated like a boundary condition,
     * and factored out into a loop epilogue if possible.
     * Pros: no redundant re-evaluation; does not constrain input or
     * output sizes. Cons: increases code size due to separate
     * tail-case handling. */
    Predicate,

    /** Guard the loads in the loop with an if statement that
     * prevents evaluation beyond the original extent. Only legal
     * for innermost splits. Not legal for RVars, as it would change
     * the meaning of the algorithm. The if statement is treated like
     * a boundary condition, and factored out into a loop epilogue if
     * possible.
     * Pros: does not constrain input sizes, output size constraints
     * are simpler than full predication. Cons: increases code size
     * due to separate tail-case handling, constrains the output size
     * to be a multiple of the split factor. */
    PredicateLoads,

    /** Guard the stores in the loop with an if statement that
     * prevents evaluation beyond the original extent. Only legal
     * for innermost splits. Not legal for RVars, as it would change
     * the meaning of the algorithm. The if statement is treated like
     * a boundary condition, and factored out into a loop epilogue if
     * possible.
     * Pros: does not constrain output sizes, input size constraints
     * are simpler than full predication. Cons: increases code size
     * due to separate tail-case handling, constraints the input size
     * to be a multiple of the split factor.. */
    PredicateStores,

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
        : contents(std::move(c)) {
    }
    LoopLevel(const std::string &func_name, const std::string &var_name,
              bool is_rvar, int stage_index, bool locked = false);

public:
    /** Return the index of the function stage associated with this loop level.
     * Asserts if undefined */
    int stage_index() const;

    /** Identify the loop nest corresponding to some dimension of some function */
    // @{
    LoopLevel(const Internal::Function &f, const VarOrRVar &v, int stage_index = -1);
    LoopLevel(const Func &f, const VarOrRVar &v, int stage_index = -1);
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

/** Each Dim below has a dim_type, which tells you what
 * transformations are legal on it. When you combine two Dims of
 * distinct DimTypes (e.g. with Stage::fuse), the combined result has
 * the greater enum value of the two types. */
enum class DimType {
    /** This dim originated from a Var. You can evaluate a Func at
     * distinct values of this Var in any order over an interval
     * that's at least as large as the interval required. In pure
     * definitions you can even redundantly re-evaluate points. */
    PureVar = 0,

    /** The dim originated from an RVar. You can evaluate a Func at
     * distinct values of this RVar in any order (including in
     * parallel) over exactly the interval specified in the
     * RDom. PureRVars can also be reordered arbitrarily in the dims
     * list, as there are no data hazards between the evaluation of
     * the Func at distinct values of the RVar.
     *
     * The most common case where an RVar is considered pure is RVars
     * that are used in a way which obeys all the syntactic
     * constraints that a Var does, e.g:
     *
     \code
     RDom r(0, 100);
     f(r.x) = f(r.x) + 5;
     \endcode
     *
     * Other cases where RVars are pure are where the sites being
     * written to by the Func evaluated at one value of the RVar
     * couldn't possibly collide with the sites being written or read
     * by the Func at a distinct value of the RVar. For example, r.x
     * is pure in the following three definitions:
     *
     \code

     // This definition writes to even coordinates and reads from the
     // same site (which no other value of r.x is writing to) and odd
     // sites (which no other value of r.x is writing to):
     f(2*r.x) = max(f(2*r.x), f(2*r.x + 7));

     // This definition writes to scanline zero and reads from the the
     // same site and scanline one:
     f(r.x, 0) += f(r.x, 1);

     // This definition reads and writes over non-overlapping ranges:
     f(r.x + 100) += f(r.x);
     \endcode
     *
     * To give two counterexamples, r.x is not pure in the following
     * definitions:
     *
     \code
     // The same site is written by distinct values of the RVar
     // (write-after-write hazard):
     f(r.x / 2) += f(r.x);

     // One value of r.x reads from a site that another value of r.x
     // is writing to (read-after-write hazard):
     f(r.x) += f(r.x + 1);
     \endcode
     */
    PureRVar,

    /** The dim originated from an RVar. You must evaluate a Func at
     * distinct values of this RVar in increasing order over precisely
     * the interval specified in the RDom. ImpureRVars may not be
     * reordered with respect to other ImpureRVars.
     *
     * All RVars are impure by default. Those for which we can prove
     * no data hazards exist get promoted to PureRVar. There are two
     * instances in which ImpureRVars may be parallelized or reordered
     * even in the presence of hazards:
     *
     * 1) In the case of an update definition that has been proven to be
     * an associative and commutative reduction, reordering of
     * ImpureRVars is allowed, and parallelizing them is allowed if
     * the update has been made atomic.
     *
     * 2) ImpureRVars can also be reordered and parallelized if
     * Func::allow_race_conditions() has been set. This is the escape
     * hatch for when there are no hazards but the checks above failed
     * to prove that (RDom::where can encode arbitrary facts about
     * non-linear integer arithmetic, which is undecidable), or for
     * when you don't actually care about the non-determinism
     * introduced by data hazards (e.g. in the algorithm HOGWILD!).
     */
    ImpureRVar,
};

/** The Dim struct represents one loop in the schedule's
 * representation of a loop nest. */
struct Dim {
    /** Name of the loop variable */
    std::string var;

    /** How are the loop values traversed (e.g. unrolled, vectorized, parallel) */
    ForType for_type;

    /** On what device does the body of the loop execute (e.g. Host, GPU, Hexagon) */
    DeviceAPI device_api;

    /** The DimType tells us what transformations are legal on this
     * loop (see the DimType enum above). */
    DimType dim_type;

    /** Can this loop be evaluated in any order (including in
     * parallel)? Equivalently, are there no data hazards between
     * evaluations of the Func at distinct values of this var? */
    bool is_pure() const {
        return (dim_type == DimType::PureVar) || (dim_type == DimType::PureRVar);
    }

    /** Did this loop originate from an RVar (in which case the bounds
     * of the loops are algorithmically meaningful)? */
    bool is_rvar() const {
        return (dim_type == DimType::PureRVar) || (dim_type == DimType::ImpureRVar);
    }

    /** Could multiple iterations of this loop happen at the same
     * time, with reads and writes interleaved in arbitrary ways
     * according to the memory model of the underlying compiler and
     * machine? */
    bool is_unordered_parallel() const {
        return Halide::Internal::is_unordered_parallel(for_type);
    }

    /** Could multiple iterations of this loop happen at the same
     * time? Vectorized and GPULanes loop types are parallel but not
     * unordered, because the loop iterations proceed together in
     * lockstep with some well-defined outcome if there are hazards. */
    bool is_parallel() const {
        return Halide::Internal::is_parallel(for_type);
    }
};

/** A bound on a loop, typically from Func::bound */
struct Bound {
    /** The loop var being bounded */
    std::string var;

    /** Declared min and extent of the loop. min may be undefined if
     * Func::bound_extent was used. */
    Expr min, extent;

    /** If defined, the number of iterations will be a multiple of
     * "modulus", and the first iteration will be at a value congruent
     * to "remainder" modulo "modulus". Set by Func::align_bounds and
     * Func::align_extent. */
    Expr modulus, remainder;
};

/** Properties of one axis of the storage of a Func */
struct StorageDim {
    /** The var in the pure definition corresponding to this axis */
    std::string var;

    /** The bounds allocated (not computed) must be a multiple of
     * "alignment". Set by Func::align_storage. */
    Expr alignment;

    /** The bounds allocated (not computed). Set by Func::bound_storage. */
    Expr bound;

    /** If the Func is explicitly folded along this axis (with
     * Func::fold_storage) this gives the extent of the circular
     * buffer used, and whether it is used in increasing order
     * (fold_forward = true) or decreasing order (fold_forward =
     * false). */
    Expr fold_factor;
    bool fold_forward;
};

/** This represents two stages with fused loop nests from outermost to
 * a specific loop level. The loops to compute func_1(stage_1) are
 * fused with the loops to compute func_2(stage_2) from outermost to
 * loop level var_name and the computation from stage_1 of func_1
 * occurs first.
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
        : contents(std::move(c)) {
    }
    FuncSchedule(const FuncSchedule &other) = default;
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

    /** This flag is set to true if the schedule is memoized and has an attached
     *  eviction key. */
    // @{
    Expr &memoize_eviction_key();
    Expr memoize_eviction_key() const;
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
     * \ref Func::bound and \ref Func::align_bounds and \ref Func::align_extent. */
    // @{
    const std::vector<Bound> &bounds() const;
    std::vector<Bound> &bounds();
    // @}

    /** You may explicitly specify an estimate of some of the function
     * dimensions. See \ref Func::set_estimate */
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
        : contents(std::move(c)) {
    }
    StageSchedule(const StageSchedule &other) = default;
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
     * and \ref Func::compute_with */
    // @{
    const FuseLoopLevel &fuse_level() const;
    FuseLoopLevel &fuse_level();
    // @}

    /** List of function stages that are to be fused with this function stage
     * from the outermost loop to a certain loop level. Those function stages
     * are to be computed AFTER this function stage at the last fused loop level.
     * This list is populated when realization_order() is called. See
     * \ref Func::compute_with */
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

    /** Atomic updates are only allowed on associative reductions.
     *  We try to prove the associativity, but the user can override
     *  the associativity test and suppress compiler error if the prover
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
