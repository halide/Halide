#ifndef HALIDE_FUNC_H
#define HALIDE_FUNC_H

/** \file
 *
 * Defines Func - the front-end handle on a halide function, and related classes.
 */

#include "Argument.h"
#include "Expr.h"
#include "JITModule.h"
#include "Module.h"
#include "Param.h"
#include "Pipeline.h"
#include "RDom.h"
#include "Target.h"
#include "Tuple.h"
#include "Var.h"

#include <map>
#include <utility>

namespace Halide {

class OutputImageParam;
class ParamMap;

/** A class that can represent Vars or RVars. Used for reorder calls
 * which can accept a mix of either. */
struct VarOrRVar {
    VarOrRVar(const std::string &n, bool r)
        : var(n), rvar(n), is_rvar(r) {
    }
    VarOrRVar(const Var &v)
        : var(v), is_rvar(false) {
    }
    VarOrRVar(const RVar &r)
        : rvar(r), is_rvar(true) {
    }
    VarOrRVar(const RDom &r)
        : rvar(RVar(r)), is_rvar(true) {
    }
    template<int N>
    VarOrRVar(const ImplicitVar<N> &u)
        : var(u), is_rvar(false) {
    }

    const std::string &name() const {
        if (is_rvar) {
            return rvar.name();
        } else {
            return var.name();
        }
    }

    Var var;
    RVar rvar;
    bool is_rvar;
};

class ImageParam;

namespace Internal {
class Function;
struct Split;
struct StorageDim;
}  // namespace Internal

/** A single definition of a Func. May be a pure or update definition. */
class Stage {
    /** Reference to the Function this stage (or definition) belongs to. */
    Internal::Function function;
    Internal::Definition definition;
    /** Indicate which stage the definition belongs to (0 for initial
     * definition, 1 for first update, etc.). */
    size_t stage_index;
    /** Pure Vars of the Function (from the init definition). */
    std::vector<Var> dim_vars;

    void set_dim_type(const VarOrRVar &var, Internal::ForType t);
    void set_dim_device_api(const VarOrRVar &var, DeviceAPI device_api);
    void split(const std::string &old, const std::string &outer, const std::string &inner,
               const Expr &factor, bool exact, TailStrategy tail);
    void remove(const std::string &var);
    Stage &purify(const VarOrRVar &old_name, const VarOrRVar &new_name);

    const std::vector<Internal::StorageDim> &storage_dims() const {
        return function.schedule().storage_dims();
    }

    Stage &compute_with(LoopLevel loop_level, const std::map<std::string, LoopAlignStrategy> &align);

public:
    Stage(Internal::Function f, Internal::Definition d, size_t stage_index)
        : function(std::move(f)), definition(std::move(d)), stage_index(stage_index) {
        internal_assert(definition.defined());

        dim_vars.reserve(function.args().size());
        for (const auto &arg : function.args()) {
            dim_vars.emplace_back(arg);
        }
        internal_assert(definition.args().size() == dim_vars.size());
    }

    /** Return the current StageSchedule associated with this Stage. For
     * introspection only: to modify schedule, use the Func interface. */
    const Internal::StageSchedule &get_schedule() const {
        return definition.schedule();
    }

    /** Return a string describing the current var list taking into
     * account all the splits, reorders, and tiles. */
    std::string dump_argument_list() const;

    /** Return the name of this stage, e.g. "f.update(2)" */
    std::string name() const;

    /** Calling rfactor() on an associative update definition a Func will split
     * the update into an intermediate which computes the partial results and
     * replaces the current update definition with a new definition which merges
     * the partial results. If called on a init/pure definition, this will
     * throw an error. rfactor() will automatically infer the associative reduction
     * operator and identity of the operator. If it can't prove the operation
     * is associative or if it cannot find an identity for that operator, this
     * will throw an error. In addition, commutativity of the operator is required
     * if rfactor() is called on the inner dimension but excluding the outer
     * dimensions.
     *
     * rfactor() takes as input 'preserved', which is a list of <RVar, Var> pairs.
     * The rvars not listed in 'preserved' are removed from the original Func and
     * are lifted to the intermediate Func. The remaining rvars (the ones in
     * 'preserved') are made pure in the intermediate Func. The intermediate Func's
     * update definition inherits all scheduling directives (e.g. split,fuse, etc.)
     * applied to the original Func's update definition. The loop order of the
     * intermediate Func's update definition is the same as the original, although
     * the RVars in 'preserved' are replaced by the new pure Vars. The loop order of the
     * intermediate Func's init definition from innermost to outermost is the args'
     * order of the original Func's init definition followed by the new pure Vars.
     *
     * The intermediate Func also inherits storage order from the original Func
     * with the new pure Vars added to the outermost.
     *
     * For example, f.update(0).rfactor({{r.y, u}}) would rewrite a pipeline like this:
     \code
     f(x, y) = 0;
     f(x, y) += g(r.x, r.y);
     \endcode
     * into a pipeline like this:
     \code
     f_intm(x, y, u) = 0;
     f_intm(x, y, u) += g(r.x, u);

     f(x, y) = 0;
     f(x, y) += f_intm(x, y, r.y);
     \endcode
     *
     * This has a variety of uses. You can use it to split computation of an associative reduction:
     \code
     f(x, y) = 10;
     RDom r(0, 96);
     f(x, y) = max(f(x, y), g(x, y, r.x));
     f.update(0).split(r.x, rxo, rxi, 8).reorder(y, x).parallel(x);
     f.update(0).rfactor({{rxo, u}}).compute_root().parallel(u).update(0).parallel(u);
     \endcode
     *
     *, which is equivalent to:
     \code
     parallel for u = 0 to 11:
       for y:
         for x:
           f_intm(x, y, u) = -inf
     parallel for x:
       for y:
         parallel for u = 0 to 11:
           for rxi = 0 to 7:
             f_intm(x, y, u) = max(f_intm(x, y, u), g(8*u + rxi))
     for y:
       for x:
         f(x, y) = 10
     parallel for x:
       for y:
         for rxo = 0 to 11:
           f(x, y) = max(f(x, y), f_intm(x, y, rxo))
     \endcode
     *
     */
    // @{
    Func rfactor(std::vector<std::pair<RVar, Var>> preserved);
    Func rfactor(const RVar &r, const Var &v);
    // @}

    /** Schedule the iteration over this stage to be fused with another
     * stage 's' from outermost loop to a given LoopLevel. 'this' stage will
     * be computed AFTER 's' in the innermost fused dimension. There should not
     * be any dependencies between those two fused stages. If either of the
     * stages being fused is a stage of an extern Func, this will throw an error.
     *
     * Note that the two stages that are fused together should have the same
     * exact schedule from the outermost to the innermost fused dimension, and
     * the stage we are calling compute_with on should not have specializations,
     * e.g. f2.compute_with(f1, x) is allowed only if f2 has no specializations.
     *
     * Also, if a producer is desired to be computed at the fused loop level,
     * the function passed to the compute_at() needs to be the "parent". Consider
     * the following code:
     \code
     input(x, y) = x + y;
     f(x, y) = input(x, y);
     f(x, y) += 5;
     g(x, y) = x - y;
     g(x, y) += 10;
     f.compute_with(g, y);
     f.update().compute_with(g.update(), y);
     \endcode
     *
     * To compute 'input' at the fused loop level at dimension y, we specify
     * input.compute_at(g, y) instead of input.compute_at(f, y) since 'g' is
     * the "parent" for this fused loop (i.e. 'g' is computed first before 'f'
     * is computed). On the other hand, to compute 'input' at the innermost
     * dimension of 'f', we specify input.compute_at(f, x) instead of
     * input.compute_at(g, x) since the x dimension of 'f' is not fused
     * (only the y dimension is).
     *
     * Given the constraints, this has a variety of uses. Consider the
     * following code:
     \code
     f(x, y) = x + y;
     g(x, y) = x - y;
     h(x, y) = f(x, y) + g(x, y);
     f.compute_root();
     g.compute_root();
     f.split(x, xo, xi, 8);
     g.split(x, xo, xi, 8);
     g.compute_with(f, xo);
     \endcode
     *
     * This is equivalent to:
     \code
     for y:
       for xo:
         for xi:
           f(8*xo + xi) = (8*xo + xi) + y
         for xi:
           g(8*xo + xi) = (8*xo + xi) - y
     for y:
       for x:
         h(x, y) = f(x, y) + g(x, y)
     \endcode
     *
     * The size of the dimensions of the stages computed_with do not have
     * to match. Consider the following code where 'g' is half the size of 'f':
     \code
     Image<int> f_im(size, size), g_im(size/2, size/2);
     input(x, y) = x + y;
     f(x, y) = input(x, y);
     g(x, y) = input(2*x, 2*y);
     g.compute_with(f, y);
     input.compute_at(f, y);
     Pipeline({f, g}).realize({f_im, g_im});
     \endcode
     *
     * This is equivalent to:
     \code
     for y = 0 to size-1:
       for x = 0 to size-1:
         input(x, y) = x + y;
       for x = 0 to size-1:
         f(x, y) = input(x, y)
       for x = 0 to size/2-1:
         if (y < size/2-1):
           g(x, y) = input(2*x, 2*y)
     \endcode
     *
     * 'align' specifies how the loop iteration of each dimension of the
     * two stages being fused should be aligned in the fused loop nests
     * (see LoopAlignStrategy for options). Consider the following loop nests:
     \code
     for z = f_min_z to f_max_z:
       for y = f_min_y to f_max_y:
         for x = f_min_x to f_max_x:
           f(x, y, z) = x + y + z
     for z = g_min_z to g_max_z:
       for y = g_min_y to g_max_y:
         for x = g_min_x to g_max_x:
           g(x, y, z) = x - y - z
     \endcode
     *
     * If no alignment strategy is specified, the following loop nest will be
     * generated:
     \code
     for z = min(f_min_z, g_min_z) to max(f_max_z, g_max_z):
       for y = min(f_min_y, g_min_y) to max(f_max_y, g_max_y):
         for x = f_min_x to f_max_x:
           if (f_min_z <= z <= f_max_z):
             if (f_min_y <= y <= f_max_y):
               f(x, y, z) = x + y + z
         for x = g_min_x to g_max_x:
           if (g_min_z <= z <= g_max_z):
             if (g_min_y <= y <= g_max_y):
               g(x, y, z) = x - y - z
     \endcode
     *
     * Instead, these alignment strategies:
     \code
     g.compute_with(f, y, {{z, LoopAlignStrategy::AlignStart}, {y, LoopAlignStrategy::AlignEnd}});
     \endcode
     * will produce the following loop nest:
     \code
     f_loop_min_z = f_min_z
     f_loop_max_z = max(f_max_z, (f_min_z - g_min_z) + g_max_z)
     for z = f_min_z to f_loop_max_z:
       f_loop_min_y = min(f_min_y, (f_max_y - g_max_y) + g_min_y)
       f_loop_max_y = f_max_y
       for y = f_loop_min_y to f_loop_max_y:
         for x = f_min_x to f_max_x:
           if (f_loop_min_z <= z <= f_loop_max_z):
             if (f_loop_min_y <= y <= f_loop_max_y):
               f(x, y, z) = x + y + z
         for x = g_min_x to g_max_x:
           g_shift_z = g_min_z - f_loop_min_z
           g_shift_y = g_max_y - f_loop_max_y
           if (g_min_z <= (z + g_shift_z) <= g_max_z):
             if (g_min_y <= (y + g_shift_y) <= g_max_y):
               g(x, y + g_shift_y, z + g_shift_z) = x - (y + g_shift_y) - (z + g_shift_z)
     \endcode
     *
     * LoopAlignStrategy::AlignStart on dimension z will shift the loop iteration
     * of 'g' at dimension z so that its starting value matches that of 'f'.
     * Likewise, LoopAlignStrategy::AlignEnd on dimension y will shift the loop
     * iteration of 'g' at dimension y so that its end value matches that of 'f'.
     */
    // @{
    Stage &compute_with(LoopLevel loop_level, const std::vector<std::pair<VarOrRVar, LoopAlignStrategy>> &align);
    Stage &compute_with(LoopLevel loop_level, LoopAlignStrategy align = LoopAlignStrategy::Auto);
    Stage &compute_with(const Stage &s, const VarOrRVar &var, const std::vector<std::pair<VarOrRVar, LoopAlignStrategy>> &align);
    Stage &compute_with(const Stage &s, const VarOrRVar &var, LoopAlignStrategy align = LoopAlignStrategy::Auto);
    // @}

    /** Scheduling calls that control how the domain of this stage is
     * traversed. See the documentation for Func for the meanings. */
    // @{

    Stage &split(const VarOrRVar &old, const VarOrRVar &outer, const VarOrRVar &inner, const Expr &factor, TailStrategy tail = TailStrategy::Auto);
    Stage &fuse(const VarOrRVar &inner, const VarOrRVar &outer, const VarOrRVar &fused);
    Stage &serial(const VarOrRVar &var);
    Stage &parallel(const VarOrRVar &var);
    Stage &vectorize(const VarOrRVar &var);
    Stage &unroll(const VarOrRVar &var);
    Stage &parallel(const VarOrRVar &var, const Expr &task_size, TailStrategy tail = TailStrategy::Auto);
    Stage &vectorize(const VarOrRVar &var, const Expr &factor, TailStrategy tail = TailStrategy::Auto);
    Stage &unroll(const VarOrRVar &var, const Expr &factor, TailStrategy tail = TailStrategy::Auto);
    Stage &tile(const VarOrRVar &x, const VarOrRVar &y,
                const VarOrRVar &xo, const VarOrRVar &yo,
                const VarOrRVar &xi, const VarOrRVar &yi, const Expr &xfactor, const Expr &yfactor,
                TailStrategy tail = TailStrategy::Auto);
    Stage &tile(const VarOrRVar &x, const VarOrRVar &y,
                const VarOrRVar &xi, const VarOrRVar &yi,
                const Expr &xfactor, const Expr &yfactor,
                TailStrategy tail = TailStrategy::Auto);
    Stage &tile(const std::vector<VarOrRVar> &previous,
                const std::vector<VarOrRVar> &outers,
                const std::vector<VarOrRVar> &inners,
                const std::vector<Expr> &factors,
                const std::vector<TailStrategy> &tails);
    Stage &tile(const std::vector<VarOrRVar> &previous,
                const std::vector<VarOrRVar> &outers,
                const std::vector<VarOrRVar> &inners,
                const std::vector<Expr> &factors,
                TailStrategy tail = TailStrategy::Auto);
    Stage &tile(const std::vector<VarOrRVar> &previous,
                const std::vector<VarOrRVar> &inners,
                const std::vector<Expr> &factors,
                TailStrategy tail = TailStrategy::Auto);
    Stage &reorder(const std::vector<VarOrRVar> &vars);

    template<typename... Args>
    HALIDE_NO_USER_CODE_INLINE typename std::enable_if<Internal::all_are_convertible<VarOrRVar, Args...>::value, Stage &>::type
    reorder(const VarOrRVar &x, const VarOrRVar &y, Args &&...args) {
        std::vector<VarOrRVar> collected_args{x, y, std::forward<Args>(args)...};
        return reorder(collected_args);
    }

    Stage &rename(const VarOrRVar &old_name, const VarOrRVar &new_name);
    Stage specialize(const Expr &condition);
    void specialize_fail(const std::string &message);

    Stage &gpu_threads(const VarOrRVar &thread_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Stage &gpu_threads(const VarOrRVar &thread_x, const VarOrRVar &thread_y, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Stage &gpu_threads(const VarOrRVar &thread_x, const VarOrRVar &thread_y, const VarOrRVar &thread_z, DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &gpu_lanes(const VarOrRVar &thread_x, DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &gpu_single_thread(DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &gpu_blocks(const VarOrRVar &block_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Stage &gpu_blocks(const VarOrRVar &block_x, const VarOrRVar &block_y, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Stage &gpu_blocks(const VarOrRVar &block_x, const VarOrRVar &block_y, const VarOrRVar &block_z, DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &gpu(const VarOrRVar &block_x, const VarOrRVar &thread_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Stage &gpu(const VarOrRVar &block_x, const VarOrRVar &block_y,
               const VarOrRVar &thread_x, const VarOrRVar &thread_y,
               DeviceAPI device_api = DeviceAPI::Default_GPU);
    Stage &gpu(const VarOrRVar &block_x, const VarOrRVar &block_y, const VarOrRVar &block_z,
               const VarOrRVar &thread_x, const VarOrRVar &thread_y, const VarOrRVar &thread_z,
               DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &gpu_tile(const VarOrRVar &x, const VarOrRVar &bx, const VarOrRVar &tx, const Expr &x_size,
                    TailStrategy tail = TailStrategy::Auto,
                    DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &gpu_tile(const VarOrRVar &x, const VarOrRVar &tx, const Expr &x_size,
                    TailStrategy tail = TailStrategy::Auto,
                    DeviceAPI device_api = DeviceAPI::Default_GPU);
    Stage &gpu_tile(const VarOrRVar &x, const VarOrRVar &y,
                    const VarOrRVar &bx, const VarOrRVar &by,
                    const VarOrRVar &tx, const VarOrRVar &ty,
                    const Expr &x_size, const Expr &y_size,
                    TailStrategy tail = TailStrategy::Auto,
                    DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &gpu_tile(const VarOrRVar &x, const VarOrRVar &y,
                    const VarOrRVar &tx, const VarOrRVar &ty,
                    const Expr &x_size, const Expr &y_size,
                    TailStrategy tail = TailStrategy::Auto,
                    DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &gpu_tile(const VarOrRVar &x, const VarOrRVar &y, const VarOrRVar &z,
                    const VarOrRVar &bx, const VarOrRVar &by, const VarOrRVar &bz,
                    const VarOrRVar &tx, const VarOrRVar &ty, const VarOrRVar &tz,
                    const Expr &x_size, const Expr &y_size, const Expr &z_size,
                    TailStrategy tail = TailStrategy::Auto,
                    DeviceAPI device_api = DeviceAPI::Default_GPU);
    Stage &gpu_tile(const VarOrRVar &x, const VarOrRVar &y, const VarOrRVar &z,
                    const VarOrRVar &tx, const VarOrRVar &ty, const VarOrRVar &tz,
                    const Expr &x_size, const Expr &y_size, const Expr &z_size,
                    TailStrategy tail = TailStrategy::Auto,
                    DeviceAPI device_api = DeviceAPI::Default_GPU);

    Stage &allow_race_conditions();
    Stage &atomic(bool override_associativity_test = false);

    Stage &hexagon(const VarOrRVar &x = Var::outermost());

    Stage &prefetch(const Func &f, const VarOrRVar &at, const VarOrRVar &from, Expr offset = 1,
                    PrefetchBoundStrategy strategy = PrefetchBoundStrategy::GuardWithIf);
    Stage &prefetch(const Internal::Parameter &param, const VarOrRVar &at, const VarOrRVar &from, Expr offset = 1,
                    PrefetchBoundStrategy strategy = PrefetchBoundStrategy::GuardWithIf);
    template<typename T>
    Stage &prefetch(const T &image, const VarOrRVar &at, const VarOrRVar &from, Expr offset = 1,
                    PrefetchBoundStrategy strategy = PrefetchBoundStrategy::GuardWithIf) {
        return prefetch(image.parameter(), at, from, std::move(offset), strategy);
    }
    // @}

    /** Attempt to get the source file and line where this stage was
     * defined by parsing the process's own debug symbols. Returns an
     * empty string if no debug symbols were found or the debug
     * symbols were not understood. Works on OS X and Linux only. */
    std::string source_location() const;

    /** Assert that this stage has intentionally been given no schedule, and
     * suppress the warning about unscheduled update definitions that would
     * otherwise fire. This counts as a schedule, so calling this twice on the
     * same Stage will fail the assertion. */
    void unscheduled();
};

// For backwards compatibility, keep the ScheduleHandle name.
typedef Stage ScheduleHandle;

class FuncTupleElementRef;

/** A fragment of front-end syntax of the form f(x, y, z), where x, y,
 * z are Vars or Exprs. If could be the left hand side of a definition or
 * an update definition, or it could be a call to a function. We don't know
 * until we see how this object gets used.
 */
class FuncRef {
    Internal::Function func;
    int implicit_placeholder_pos;
    int implicit_count;
    std::vector<Expr> args;
    std::vector<Expr> args_with_implicit_vars(const std::vector<Expr> &e) const;

    /** Helper for function update by Tuple. If the function does not
     * already have a pure definition, init_val will be used as RHS of
     * each tuple element in the initial function definition. */
    template<typename BinaryOp>
    Stage func_ref_update(const Tuple &e, int init_val);

    /** Helper for function update by Expr. If the function does not
     * already have a pure definition, init_val will be used as RHS in
     * the initial function definition. */
    template<typename BinaryOp>
    Stage func_ref_update(Expr e, int init_val);

public:
    FuncRef(const Internal::Function &, const std::vector<Expr> &,
            int placeholder_pos = -1, int count = 0);
    FuncRef(Internal::Function, const std::vector<Var> &,
            int placeholder_pos = -1, int count = 0);

    /** Use this as the left-hand-side of a definition or an update definition
     * (see \ref RDom).
     */
    Stage operator=(const Expr &);

    /** Use this as the left-hand-side of a definition or an update definition
     * for a Func with multiple outputs. */
    Stage operator=(const Tuple &);

    /** Define a stage that adds the given expression to this Func. If the
     * expression refers to some RDom, this performs a sum reduction of the
     * expression over the domain. If the function does not already have a
     * pure definition, this sets it to zero.
     */
    // @{
    Stage operator+=(Expr);
    Stage operator+=(const Tuple &);
    Stage operator+=(const FuncRef &);
    // @}

    /** Define a stage that adds the negative of the given expression to this
     * Func. If the expression refers to some RDom, this performs a sum reduction
     * of the negative of the expression over the domain. If the function does
     * not already have a pure definition, this sets it to zero.
     */
    // @{
    Stage operator-=(Expr);
    Stage operator-=(const Tuple &);
    Stage operator-=(const FuncRef &);
    // @}

    /** Define a stage that multiplies this Func by the given expression. If the
     * expression refers to some RDom, this performs a product reduction of the
     * expression over the domain. If the function does not already have a pure
     * definition, this sets it to 1.
     */
    // @{
    Stage operator*=(Expr);
    Stage operator*=(const Tuple &);
    Stage operator*=(const FuncRef &);
    // @}

    /** Define a stage that divides this Func by the given expression.
     * If the expression refers to some RDom, this performs a product
     * reduction of the inverse of the expression over the domain. If the
     * function does not already have a pure definition, this sets it to 1.
     */
    // @{
    Stage operator/=(Expr);
    Stage operator/=(const Tuple &);
    Stage operator/=(const FuncRef &);
    // @}

    /* Override the usual assignment operator, so that
     * f(x, y) = g(x, y) defines f.
     */
    Stage operator=(const FuncRef &);

    /** Use this as a call to the function, and not the left-hand-side
     * of a definition. Only works for single-output Funcs. */
    operator Expr() const;

    /** When a FuncRef refers to a function that provides multiple
     * outputs, you can access each output as an Expr using
     * operator[].
     */
    FuncTupleElementRef operator[](int) const;

    /** How many outputs does the function this refers to produce. */
    size_t size() const;

    /** What function is this calling? */
    Internal::Function function() const {
        return func;
    }
};

/** Explicit overloads of min and max for FuncRef. These exist to
 * disambiguate calls to min on FuncRefs when a user has pulled both
 * Halide::min and std::min into their namespace. */
// @{
inline Expr min(const FuncRef &a, const FuncRef &b) {
    return min(Expr(a), Expr(b));
}
inline Expr max(const FuncRef &a, const FuncRef &b) {
    return max(Expr(a), Expr(b));
}
// @}

/** A fragment of front-end syntax of the form f(x, y, z)[index], where x, y,
 * z are Vars or Exprs. If could be the left hand side of an update
 * definition, or it could be a call to a function. We don't know
 * until we see how this object gets used.
 */
class FuncTupleElementRef {
    FuncRef func_ref;
    std::vector<Expr> args;  // args to the function
    int idx;                 // Index to function outputs

    /** Helper function that generates a Tuple where element at 'idx' is set
     * to 'e' and the rests are undef. */
    Tuple values_with_undefs(const Expr &e) const;

public:
    FuncTupleElementRef(const FuncRef &ref, const std::vector<Expr> &args, int idx);

    /** Use this as the left-hand-side of an update definition of Tuple
     * component 'idx' of a Func (see \ref RDom). The function must
     * already have an initial definition.
     */
    Stage operator=(const Expr &e);

    /** Define a stage that adds the given expression to Tuple component 'idx'
     * of this Func. The other Tuple components are unchanged. If the expression
     * refers to some RDom, this performs a sum reduction of the expression over
     * the domain. The function must already have an initial definition.
     */
    Stage operator+=(const Expr &e);

    /** Define a stage that adds the negative of the given expression to Tuple
     * component 'idx' of this Func. The other Tuple components are unchanged.
     * If the expression refers to some RDom, this performs a sum reduction of
     * the negative of the expression over the domain. The function must already
     * have an initial definition.
     */
    Stage operator-=(const Expr &e);

    /** Define a stage that multiplies Tuple component 'idx' of this Func by
     * the given expression. The other Tuple components are unchanged. If the
     * expression refers to some RDom, this performs a product reduction of
     * the expression over the domain. The function must already have an
     * initial definition.
     */
    Stage operator*=(const Expr &e);

    /** Define a stage that divides Tuple component 'idx' of this Func by
     * the given expression. The other Tuple components are unchanged.
     * If the expression refers to some RDom, this performs a product
     * reduction of the inverse of the expression over the domain. The function
     * must already have an initial definition.
     */
    Stage operator/=(const Expr &e);

    /* Override the usual assignment operator, so that
     * f(x, y)[index] = g(x, y) defines f.
     */
    Stage operator=(const FuncRef &e);

    /** Use this as a call to Tuple component 'idx' of a Func, and not the
     * left-hand-side of a definition. */
    operator Expr() const;

    /** What function is this calling? */
    Internal::Function function() const {
        return func_ref.function();
    }

    /** Return index to the function outputs. */
    int index() const {
        return idx;
    }
};

namespace Internal {
class IRMutator;
}  // namespace Internal

/** Helper class for identifying purpose of an Expr passed to memoize.
 */
class EvictionKey {
protected:
    Expr key;
    friend class Func;

public:
    explicit EvictionKey(const Expr &expr = Expr())
        : key(expr) {
    }
};

/** A halide function. This class represents one stage in a Halide
 * pipeline, and is the unit by which we schedule things. By default
 * they are aggressively inlined, so you are encouraged to make lots
 * of little functions, rather than storing things in Exprs. */
class Func {

    /** A handle on the internal halide function that this
     * represents */
    Internal::Function func;

    /** When you make a reference to this function with fewer
     * arguments than it has dimensions, the argument list is bulked
     * up with 'implicit' vars with canonical names. This lets you
     * pass around partially applied Halide functions. */
    // @{
    std::pair<int, int> add_implicit_vars(std::vector<Var> &) const;
    std::pair<int, int> add_implicit_vars(std::vector<Expr> &) const;
    // @}

    /** The imaging pipeline that outputs this Func alone. */
    Pipeline pipeline_;

    /** Get the imaging pipeline that outputs this Func alone,
     * creating it (and freezing the Func) if necessary. */
    Pipeline pipeline();

    // Helper function for recursive reordering support
    Func &reorder_storage(const std::vector<Var> &dims, size_t start);

    void invalidate_cache();

public:
    /** Declare a new undefined function with the given name */
    explicit Func(const std::string &name);

    /** Declare a new undefined function with the given name.
     * The function will be constrained to represent Exprs of required_type.
     * If required_dims is not AnyDims, the function will be constrained to exactly
     * that many dimensions. */
    explicit Func(const Type &required_type, int required_dims, const std::string &name);

    /** Declare a new undefined function with the given name.
     * If required_types is not empty, the function will be constrained to represent
     * Tuples of the same arity and types. (If required_types is empty, there is no constraint.)
     * If required_dims is not AnyDims, the function will be constrained to exactly
     * that many dimensions. */
    explicit Func(const std::vector<Type> &required_types, int required_dims, const std::string &name);

    /** Declare a new undefined function with an
     * automatically-generated unique name */
    Func();

    /** Declare a new function with an automatically-generated unique
     * name, and define it to return the given expression (which may
     * not contain free variables). */
    explicit Func(const Expr &e);

    /** Construct a new Func to wrap an existing, already-define
     * Function object. */
    explicit Func(Internal::Function f);

    /** Construct a new Func to wrap a Buffer. */
    template<typename T, int Dims>
    HALIDE_NO_USER_CODE_INLINE explicit Func(Buffer<T, Dims> &im)
        : Func() {
        (*this)(_) = im(_);
    }

    /** Evaluate this function over some rectangular domain and return
     * the resulting buffer or buffers. Performs compilation if the
     * Func has not previously been realized and compile_jit has not
     * been called. If the final stage of the pipeline is on the GPU,
     * data is copied back to the host before being returned. The
     * returned Realization should probably be instantly converted to
     * a Buffer class of the appropriate type. That is, do this:
     *
     \code
     f(x) = sin(x);
     Buffer<float> im = f.realize(...);
     \endcode
     *
     * If your Func has multiple values, because you defined it using
     * a Tuple, then casting the result of a realize call to a buffer
     * or image will produce a run-time error. Instead you should do the
     * following:
     *
     \code
     f(x) = Tuple(x, sin(x));
     Realization r = f.realize(...);
     Buffer<int> im0 = r[0];
     Buffer<float> im1 = r[1];
     \endcode
     *
     * In Halide formal arguments of a computation are specified using
     * Param<T> and ImageParam objects in the expressions defining the
     * computation. The param_map argument to realize allows
     * specifying a set of per-call parameters to be used for a
     * specific computation. This method is thread-safe where the
     * globals used by Param<T> and ImageParam are not. Any parameters
     * that are not in the param_map are taken from the global values,
     * so those can continue to be used if they are not changing
     * per-thread.
     *
     * One can explicitly construct a ParamMap and
     * use its set method to insert Parameter to scalar or Buffer
     * value mappings:
     *
     \code
     Param<int32> p(42);
     ImageParam img(Int(32), 1);
     f(x) = img(x) + p;

     Buffer<int32_t) arg_img(10, 10);
     <fill in arg_img...>
     ParamMap params;
     params.set(p, 17);
     params.set(img, arg_img);

     Target t = get_jit_target_from_environment();
     Buffer<int32_t> result = f.realize({10, 10}, t, params);
     \endcode
     *
     * Alternatively, an initializer list can be used
     * directly in the realize call to pass this information:
     *
     \code
     Param<int32> p(42);
     ImageParam img(Int(32), 1);
     f(x) = img(x) + p;

     Buffer<int32_t) arg_img(10, 10);
     <fill in arg_img...>

     Target t = get_jit_target_from_environment();
     Buffer<int32_t> result = f.realize({10, 10}, t, { { p, 17 }, { img, arg_img } });
     \endcode
     *
     * If the Func cannot be realized into a buffer of the given size
     * due to scheduling constraints on scattering update definitions,
     * it will be realized into a larger buffer of the minimum size
     * possible, and a cropped view at the requested size will be
     * returned. It is thus not safe to assume the returned buffers
     * are contiguous in memory. This behavior can be disabled with
     * the NoBoundsQuery target flag, in which case an error about
     * writing out of bounds on the output buffer will trigger
     * instead.
     *
     */
    Realization realize(std::vector<int32_t> sizes = {}, const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());

    /** Same as above, but takes a custom user-provided context to be
     * passed to runtime functions. This can be used to pass state to
     * runtime overrides in a thread-safe manner. A nullptr context is
     * legal, and is equivalent to calling the variant of realize
     * that does not take a context. */
    Realization realize(JITUserContext *context,
                        std::vector<int32_t> sizes = {},
                        const Target &target = Target(),
                        const ParamMap &param_map = ParamMap::empty_map());

    /** Evaluate this function into an existing allocated buffer or
     * buffers. If the buffer is also one of the arguments to the
     * function, strange things may happen, as the pipeline isn't
     * necessarily safe to run in-place. If you pass multiple buffers,
     * they must have matching sizes. This form of realize does *not*
     * automatically copy data back from the GPU. */
    void realize(Pipeline::RealizationArg outputs, const Target &target = Target(),
                 const ParamMap &param_map = ParamMap::empty_map());

    /** Same as above, but takes a custom user-provided context to be
     * passed to runtime functions. This can be used to pass state to
     * runtime overrides in a thread-safe manner. A nullptr context is
     * legal, and is equivalent to calling the variant of realize
     * that does not take a context. */
    void realize(JITUserContext *context,
                 Pipeline::RealizationArg outputs,
                 const Target &target = Target(),
                 const ParamMap &param_map = ParamMap::empty_map());

    /** For a given size of output, or a given output buffer,
     * determine the bounds required of all unbound ImageParams
     * referenced. Communicates the result by allocating new buffers
     * of the appropriate size and binding them to the unbound
     * ImageParams.
     *
     * Set the documentation for Func::realize regarding the
     * ParamMap. There is one difference in that input Buffer<>
     * arguments that are being inferred are specified as a pointer to
     * the Buffer<> in the ParamMap. E.g.
     *
     \code
     Param<int32> p(42);
     ImageParam img(Int(32), 1);
     f(x) = img(x) + p;

     Target t = get_jit_target_from_environment();
     Buffer<> in;
     f.infer_input_bounds({10, 10}, t, { { img, &in } });
     \endcode
     * On return, in will be an allocated buffer of the correct size
     * to evaulate f over a 10x10 region.
     */
    // @{
    void infer_input_bounds(const std::vector<int32_t> &sizes,
                            const Target &target = get_jit_target_from_environment(),
                            const ParamMap &param_map = ParamMap::empty_map());
    void infer_input_bounds(Pipeline::RealizationArg outputs,
                            const Target &target = get_jit_target_from_environment(),
                            const ParamMap &param_map = ParamMap::empty_map());
    // @}

    /** Versions of infer_input_bounds that take a custom user context
     * to pass to runtime functions. */
    // @{
    void infer_input_bounds(JITUserContext *context,
                            const std::vector<int32_t> &sizes,
                            const Target &target = get_jit_target_from_environment(),
                            const ParamMap &param_map = ParamMap::empty_map());
    void infer_input_bounds(JITUserContext *context,
                            Pipeline::RealizationArg outputs,
                            const Target &target = get_jit_target_from_environment(),
                            const ParamMap &param_map = ParamMap::empty_map());
    // @}
    /** Statically compile this function to llvm bitcode, with the
     * given filename (which should probably end in .bc), type
     * signature, and C function name (which defaults to the same name
     * as this halide function */
    //@{
    void compile_to_bitcode(const std::string &filename, const std::vector<Argument> &, const std::string &fn_name,
                            const Target &target = get_target_from_environment());
    void compile_to_bitcode(const std::string &filename, const std::vector<Argument> &,
                            const Target &target = get_target_from_environment());
    // @}

    /** Statically compile this function to llvm assembly, with the
     * given filename (which should probably end in .ll), type
     * signature, and C function name (which defaults to the same name
     * as this halide function */
    //@{
    void compile_to_llvm_assembly(const std::string &filename, const std::vector<Argument> &, const std::string &fn_name,
                                  const Target &target = get_target_from_environment());
    void compile_to_llvm_assembly(const std::string &filename, const std::vector<Argument> &,
                                  const Target &target = get_target_from_environment());
    // @}

    /** Statically compile this function to an object file, with the
     * given filename (which should probably end in .o or .obj), type
     * signature, and C function name (which defaults to the same name
     * as this halide function. You probably don't want to use this
     * directly; call compile_to_static_library or compile_to_file instead. */
    //@{
    void compile_to_object(const std::string &filename, const std::vector<Argument> &, const std::string &fn_name,
                           const Target &target = get_target_from_environment());
    void compile_to_object(const std::string &filename, const std::vector<Argument> &,
                           const Target &target = get_target_from_environment());
    // @}

    /** Emit a header file with the given filename for this
     * function. The header will define a function with the type
     * signature given by the second argument, and a name given by the
     * third. The name defaults to the same name as this halide
     * function. You don't actually have to have defined this function
     * yet to call this. You probably don't want to use this directly;
     * call compile_to_static_library or compile_to_file instead. */
    void compile_to_header(const std::string &filename, const std::vector<Argument> &, const std::string &fn_name = "",
                           const Target &target = get_target_from_environment());

    /** Statically compile this function to text assembly equivalent
     * to the object file generated by compile_to_object. This is
     * useful for checking what Halide is producing without having to
     * disassemble anything, or if you need to feed the assembly into
     * some custom toolchain to produce an object file (e.g. iOS) */
    //@{
    void compile_to_assembly(const std::string &filename, const std::vector<Argument> &, const std::string &fn_name,
                             const Target &target = get_target_from_environment());
    void compile_to_assembly(const std::string &filename, const std::vector<Argument> &,
                             const Target &target = get_target_from_environment());
    // @}

    /** Statically compile this function to C source code. This is
     * useful for providing fallback code paths that will compile on
     * many platforms. Vectorization will fail, and parallelization
     * will produce serial code. */
    void compile_to_c(const std::string &filename,
                      const std::vector<Argument> &,
                      const std::string &fn_name = "",
                      const Target &target = get_target_from_environment());

    /** Write out an internal representation of lowered code. Useful
     * for analyzing and debugging scheduling. Can emit html or plain
     * text. */
    void compile_to_lowered_stmt(const std::string &filename,
                                 const std::vector<Argument> &args,
                                 StmtOutputFormat fmt = Text,
                                 const Target &target = get_target_from_environment());

    /** Write out the loop nests specified by the schedule for this
     * Function. Helpful for understanding what a schedule is
     * doing. */
    void print_loop_nest();

    /** Compile to object file and header pair, with the given
     * arguments. The name defaults to the same name as this halide
     * function.
     */
    void compile_to_file(const std::string &filename_prefix, const std::vector<Argument> &args,
                         const std::string &fn_name = "",
                         const Target &target = get_target_from_environment());

    /** Compile to static-library file and header pair, with the given
     * arguments. The name defaults to the same name as this halide
     * function.
     */
    void compile_to_static_library(const std::string &filename_prefix, const std::vector<Argument> &args,
                                   const std::string &fn_name = "",
                                   const Target &target = get_target_from_environment());

    /** Compile to static-library file and header pair once for each target;
     * each resulting function will be considered (in order) via halide_can_use_target_features()
     * at runtime, with the first appropriate match being selected for subsequent use.
     * This is typically useful for specializations that may vary unpredictably by machine
     * (e.g., SSE4.1/AVX/AVX2 on x86 desktop machines).
     * All targets must have identical arch-os-bits.
     */
    void compile_to_multitarget_static_library(const std::string &filename_prefix,
                                               const std::vector<Argument> &args,
                                               const std::vector<Target> &targets);

    /** Like compile_to_multitarget_static_library(), except that the object files
     * are all output as object files (rather than bundled into a static library).
     *
     * `suffixes` is an optional list of strings to use for as the suffix for each object
     * file. If nonempty, it must be the same length as `targets`. (If empty, Target::to_string()
     * will be used for each suffix.)
     *
     * Note that if `targets.size()` > 1, the wrapper code (to select the subtarget)
     * will be generated with the filename `${filename_prefix}_wrapper.o`
     *
     * Note that if `targets.size()` > 1 and `no_runtime` is not specified, the runtime
     * will be generated with the filename `${filename_prefix}_runtime.o`
     */
    void compile_to_multitarget_object_files(const std::string &filename_prefix,
                                             const std::vector<Argument> &args,
                                             const std::vector<Target> &targets,
                                             const std::vector<std::string> &suffixes);

    /** Store an internal representation of lowered code as a self
     * contained Module suitable for further compilation. */
    Module compile_to_module(const std::vector<Argument> &args, const std::string &fn_name = "",
                             const Target &target = get_target_from_environment());

    /** Compile and generate multiple target files with single call.
     * Deduces target files based on filenames specified in
     * output_files map.
     */
    void compile_to(const std::map<OutputFileType, std::string> &output_files,
                    const std::vector<Argument> &args,
                    const std::string &fn_name,
                    const Target &target = get_target_from_environment());

    /** Eagerly jit compile the function to machine code. This
     * normally happens on the first call to realize. If you're
     * running your halide pipeline inside time-sensitive code and
     * wish to avoid including the time taken to compile a pipeline,
     * then you can call this ahead of time. Default is to use the Target
     * returned from Halide::get_jit_target_from_environment()
     */
    void compile_jit(const Target &target = get_jit_target_from_environment());

    /** Get a struct containing the currently set custom functions
     * used by JIT. This can be mutated. Changes will take effect the
     * next time this Func is realized. */
    JITHandlers &jit_handlers();

    /** Eagerly jit compile the function to machine code and return a callable
     * struct that behaves like a function pointer. The calling convention
     * will exactly match that of an AOT-compiled version of this Func
     * with the same Argument list.
     */
    Callable compile_to_callable(const std::vector<Argument> &args,
                                 const Target &target = get_jit_target_from_environment());

    /** Add a custom pass to be used during lowering. It is run after
     * all other lowering passes. Can be used to verify properties of
     * the lowered Stmt, instrument it with extra code, or otherwise
     * modify it. The Func takes ownership of the pass, and will call
     * delete on it when the Func goes out of scope. So don't pass a
     * stack object, or share pass instances between multiple
     * Funcs. */
    template<typename T>
    void add_custom_lowering_pass(T *pass) {
        // Template instantiate a custom deleter for this type, then
        // wrap in a lambda. The custom deleter lives in user code, so
        // that deletion is on the same heap as construction (I hate Windows).
        add_custom_lowering_pass(pass, [pass]() { delete_lowering_pass<T>(pass); });
    }

    /** Add a custom pass to be used during lowering, with the
     * function that will be called to delete it also passed in. Set
     * it to nullptr if you wish to retain ownership of the object. */
    void add_custom_lowering_pass(Internal::IRMutator *pass, std::function<void()> deleter);

    /** Remove all previously-set custom lowering passes */
    void clear_custom_lowering_passes();

    /** Get the custom lowering passes. */
    const std::vector<CustomLoweringPass> &custom_lowering_passes();

    /** When this function is compiled, include code that dumps its
     * values to a file after it is realized, for the purpose of
     * debugging.
     *
     * If filename ends in ".tif" or ".tiff" (case insensitive) the file
     * is in TIFF format and can be read by standard tools. Oherwise, the
     * file format is as follows:
     *
     * All data is in the byte-order of the target platform.  First, a
     * 20 byte-header containing four 32-bit ints, giving the extents
     * of the first four dimensions.  Dimensions beyond four are
     * folded into the fourth.  Then, a fifth 32-bit int giving the
     * data type of the function. The typecodes are given by: float =
     * 0, double = 1, uint8_t = 2, int8_t = 3, uint16_t = 4, int16_t =
     * 5, uint32_t = 6, int32_t = 7, uint64_t = 8, int64_t = 9. The
     * data follows the header, as a densely packed array of the given
     * size and the given type. If given the extension .tmp, this file
     * format can be natively read by the program ImageStack. */
    void debug_to_file(const std::string &filename);

    /** The name of this function, either given during construction,
     * or automatically generated. */
    const std::string &name() const;

    /** Get the pure arguments. */
    std::vector<Var> args() const;

    /** The right-hand-side value of the pure definition of this
     * function. Causes an error if there's no pure definition, or if
     * the function is defined to return multiple values. */
    Expr value() const;

    /** The values returned by this function. An error if the function
     * has not been been defined. Returns a Tuple with one element for
     * functions defined to return a single value. */
    Tuple values() const;

    /** Does this function have at least a pure definition. */
    bool defined() const;

    /** Get the left-hand-side of the update definition. An empty
     * vector if there's no update definition. If there are
     * multiple update definitions for this function, use the
     * argument to select which one you want. */
    const std::vector<Expr> &update_args(int idx = 0) const;

    /** Get the right-hand-side of an update definition. An error if
     * there's no update definition. If there are multiple
     * update definitions for this function, use the argument to
     * select which one you want. */
    Expr update_value(int idx = 0) const;

    /** Get the right-hand-side of an update definition for
     * functions that returns multiple values. An error if there's no
     * update definition. Returns a Tuple with one element for
     * functions that return a single value. */
    Tuple update_values(int idx = 0) const;

    /** Get the RVars of the reduction domain for an update definition, if there is
     * one. */
    std::vector<RVar> rvars(int idx = 0) const;

    /** Does this function have at least one update definition? */
    bool has_update_definition() const;

    /** How many update definitions does this function have? */
    int num_update_definitions() const;

    /** Is this function an external stage? That is, was it defined
     * using define_extern? */
    bool is_extern() const;

    /** Add an extern definition for this Func. This lets you define a
     * Func that represents an external pipeline stage. You can, for
     * example, use it to wrap a call to an extern library such as
     * fftw. */
    // @{
    void define_extern(const std::string &function_name,
                       const std::vector<ExternFuncArgument> &params, Type t,
                       int dimensionality,
                       NameMangling mangling = NameMangling::Default,
                       DeviceAPI device_api = DeviceAPI::Host) {
        define_extern(function_name, params, t,
                      Internal::make_argument_list(dimensionality), mangling,
                      device_api);
    }

    void define_extern(const std::string &function_name,
                       const std::vector<ExternFuncArgument> &params,
                       const std::vector<Type> &types, int dimensionality,
                       NameMangling mangling) {
        define_extern(function_name, params, types,
                      Internal::make_argument_list(dimensionality), mangling);
    }

    void define_extern(const std::string &function_name,
                       const std::vector<ExternFuncArgument> &params,
                       const std::vector<Type> &types, int dimensionality,
                       NameMangling mangling = NameMangling::Default,
                       DeviceAPI device_api = DeviceAPI::Host) {
        define_extern(function_name, params, types,
                      Internal::make_argument_list(dimensionality), mangling,
                      device_api);
    }

    void define_extern(const std::string &function_name,
                       const std::vector<ExternFuncArgument> &params, Type t,
                       const std::vector<Var> &arguments,
                       NameMangling mangling = NameMangling::Default,
                       DeviceAPI device_api = DeviceAPI::Host) {
        define_extern(function_name, params, std::vector<Type>{t}, arguments,
                      mangling, device_api);
    }

    void define_extern(const std::string &function_name,
                       const std::vector<ExternFuncArgument> &params,
                       const std::vector<Type> &types,
                       const std::vector<Var> &arguments,
                       NameMangling mangling = NameMangling::Default,
                       DeviceAPI device_api = DeviceAPI::Host);
    // @}

    /** Get the type(s) of the outputs of this Func.
     *
     * It is not legal to call type() unless the Func has non-Tuple elements.
     *
     * If the Func isn't yet defined, and was not specified with required types,
     * a runtime error will occur.
     *
     * If the Func isn't yet defined, but *was* specified with required types,
     * the requirements will be returned. */
    // @{
    const Type &type() const;
    const std::vector<Type> &types() const;
    // @}

    /** Get the number of outputs of this Func. Corresponds to the
     * size of the Tuple this Func was defined to return.
     * If the Func isn't yet defined, but was specified with required types,
     * the number of outputs specified in the requirements will be returned. */
    int outputs() const;

    /** Get the name of the extern function called for an extern
     * definition. */
    const std::string &extern_function_name() const;

    /** The dimensionality (number of arguments) of this function.
     * If the Func isn't yet defined, but was specified with required dimensionality,
     * the dimensionality specified in the requirements will be returned. */
    int dimensions() const;

    /** Construct either the left-hand-side of a definition, or a call
     * to a functions that happens to only contain vars as
     * arguments. If the function has already been defined, and fewer
     * arguments are given than the function has dimensions, then
     * enough implicit vars are added to the end of the argument list
     * to make up the difference (see \ref Var::implicit) */
    // @{
    FuncRef operator()(std::vector<Var>) const;

    template<typename... Args>
    HALIDE_NO_USER_CODE_INLINE typename std::enable_if<Internal::all_are_convertible<Var, Args...>::value, FuncRef>::type
    operator()(Args &&...args) const {
        std::vector<Var> collected_args{std::forward<Args>(args)...};
        return this->operator()(collected_args);
    }
    // @}

    /** Either calls to the function, or the left-hand-side of
     * an update definition (see \ref RDom). If the function has
     * already been defined, and fewer arguments are given than the
     * function has dimensions, then enough implicit vars are added to
     * the end of the argument list to make up the difference. (see
     * \ref Var::implicit)*/
    // @{
    FuncRef operator()(std::vector<Expr>) const;

    template<typename... Args>
    HALIDE_NO_USER_CODE_INLINE typename std::enable_if<Internal::all_are_convertible<Expr, Args...>::value, FuncRef>::type
    operator()(const Expr &x, Args &&...args) const {
        std::vector<Expr> collected_args{x, std::forward<Args>(args)...};
        return (*this)(collected_args);
    }
    // @}

    /** Creates and returns a new identity Func that wraps this Func. During
     * compilation, Halide replaces all calls to this Func done by 'f'
     * with calls to the wrapper. If this Func is already wrapped for
     * use in 'f', will return the existing wrapper.
     *
     * For example, g.in(f) would rewrite a pipeline like this:
     \code
     g(x, y) = ...
     f(x, y) = ... g(x, y) ...
     \endcode
     * into a pipeline like this:
     \code
     g(x, y) = ...
     g_wrap(x, y) = g(x, y)
     f(x, y) = ... g_wrap(x, y)
     \endcode
     *
     * This has a variety of uses. You can use it to schedule this
     * Func differently in the different places it is used:
     \code
     g(x, y) = ...
     f1(x, y) = ... g(x, y) ...
     f2(x, y) = ... g(x, y) ...
     g.in(f1).compute_at(f1, y).vectorize(x, 8);
     g.in(f2).compute_at(f2, x).unroll(x);
     \endcode
     *
     * You can also use it to stage loads from this Func via some
     * intermediate buffer (perhaps on the stack as in
     * test/performance/block_transpose.cpp, or in shared GPU memory
     * as in test/performance/wrap.cpp). In this we compute the
     * wrapper at tiles of the consuming Funcs like so:
     \code
     g.compute_root()...
     g.in(f).compute_at(f, tiles)...
     \endcode
     *
     * Func::in() can also be used to compute pieces of a Func into a
     * smaller scratch buffer (perhaps on the GPU) and then copy them
     * into a larger output buffer one tile at a time. See
     * apps/interpolate/interpolate.cpp for an example of this. In
     * this case we compute the Func at tiles of its own wrapper:
     \code
     f.in(g).compute_root().gpu_tile(...)...
     f.compute_at(f.in(g), tiles)...
     \endcode
     *
     * A similar use of Func::in() wrapping Funcs with multiple update
     * stages in a pure wrapper. The following code:
     \code
     f(x, y) = x + y;
     f(x, y) += 5;
     g(x, y) = f(x, y);
     f.compute_root();
     \endcode
     *
     * Is equivalent to:
     \code
     for y:
       for x:
         f(x, y) = x + y;
     for y:
       for x:
         f(x, y) += 5
     for y:
       for x:
         g(x, y) = f(x, y)
     \endcode
     * using Func::in(), we can write:
     \code
     f(x, y) = x + y;
     f(x, y) += 5;
     g(x, y) = f(x, y);
     f.in(g).compute_root();
     \endcode
     * which instead produces:
     \code
     for y:
       for x:
         f(x, y) = x + y;
         f(x, y) += 5
         f_wrap(x, y) = f(x, y)
     for y:
       for x:
         g(x, y) = f_wrap(x, y)
     \endcode
     */
    Func in(const Func &f);

    /** Create and return an identity wrapper shared by all the Funcs in
     * 'fs'. If any of the Funcs in 'fs' already have a custom wrapper,
     * this will throw an error. */
    Func in(const std::vector<Func> &fs);

    /** Create and return a global identity wrapper, which wraps all calls to
     * this Func by any other Func. If a global wrapper already exists,
     * returns it. The global identity wrapper is only used by callers for
     * which no custom wrapper has been specified.
     */
    Func in();

    /** Similar to \ref Func::in; however, instead of replacing the call to
     * this Func with an identity Func that refers to it, this replaces the
     * call with a clone of this Func.
     *
     * For example, f.clone_in(g) would rewrite a pipeline like this:
     \code
     f(x, y) = x + y;
     g(x, y) = f(x, y) + 2;
     h(x, y) = f(x, y) - 3;
     \endcode
     * into a pipeline like this:
     \code
     f(x, y) = x + y;
     f_clone(x, y) = x + y;
     g(x, y) = f_clone(x, y) + 2;
     h(x, y) = f(x, y) - 3;
     \endcode
     *
     */
    //@{
    Func clone_in(const Func &f);
    Func clone_in(const std::vector<Func> &fs);
    //@}

    /** Declare that this function should be implemented by a call to
     * halide_buffer_copy with the given target device API. Asserts
     * that the Func has a pure definition which is a simple call to a
     * single input, and no update definitions. The wrapper Funcs
     * returned by in() are suitable candidates. Consumes all pure
     * variables, and rewrites the Func to have an extern definition
     * that calls halide_buffer_copy. */
    Func copy_to_device(DeviceAPI d = DeviceAPI::Default_GPU);

    /** Declare that this function should be implemented by a call to
     * halide_buffer_copy with a NULL target device API. Equivalent to
     * copy_to_device(DeviceAPI::Host). Asserts that the Func has a
     * pure definition which is a simple call to a single input, and
     * no update definitions. The wrapper Funcs returned by in() are
     * suitable candidates. Consumes all pure variables, and rewrites
     * the Func to have an extern definition that calls
     * halide_buffer_copy.
     *
     * Note that if the source Func is already valid in host memory,
     * this compiles to code that does the minimum number of calls to
     * memcpy.
     */
    Func copy_to_host();

    /** Split a dimension into inner and outer subdimensions with the
     * given names, where the inner dimension iterates from 0 to
     * factor-1. The inner and outer subdimensions can then be dealt
     * with using the other scheduling calls. It's ok to reuse the old
     * variable name as either the inner or outer variable. The final
     * argument specifies how the tail should be handled if the split
     * factor does not provably divide the extent. */
    Func &split(const VarOrRVar &old, const VarOrRVar &outer, const VarOrRVar &inner, const Expr &factor, TailStrategy tail = TailStrategy::Auto);

    /** Join two dimensions into a single fused dimension. The fused
     * dimension covers the product of the extents of the inner and
     * outer dimensions given. */
    Func &fuse(const VarOrRVar &inner, const VarOrRVar &outer, const VarOrRVar &fused);

    /** Mark a dimension to be traversed serially. This is the default. */
    Func &serial(const VarOrRVar &var);

    /** Mark a dimension to be traversed in parallel */
    Func &parallel(const VarOrRVar &var);

    /** Split a dimension by the given task_size, and the parallelize the
     * outer dimension. This creates parallel tasks that have size
     * task_size. After this call, var refers to the outer dimension of
     * the split. The inner dimension has a new anonymous name. If you
     * wish to mutate it, or schedule with respect to it, do the split
     * manually. */
    Func &parallel(const VarOrRVar &var, const Expr &task_size, TailStrategy tail = TailStrategy::Auto);

    /** Mark a dimension to be computed all-at-once as a single
     * vector. The dimension should have constant extent -
     * e.g. because it is the inner dimension following a split by a
     * constant factor. For most uses of vectorize you want the two
     * argument form. The variable to be vectorized should be the
     * innermost one. */
    Func &vectorize(const VarOrRVar &var);

    /** Mark a dimension to be completely unrolled. The dimension
     * should have constant extent - e.g. because it is the inner
     * dimension following a split by a constant factor. For most uses
     * of unroll you want the two-argument form. */
    Func &unroll(const VarOrRVar &var);

    /** Split a dimension by the given factor, then vectorize the
     * inner dimension. This is how you vectorize a loop of unknown
     * size. The variable to be vectorized should be the innermost
     * one. After this call, var refers to the outer dimension of the
     * split. 'factor' must be an integer. */
    Func &vectorize(const VarOrRVar &var, const Expr &factor, TailStrategy tail = TailStrategy::Auto);

    /** Split a dimension by the given factor, then unroll the inner
     * dimension. This is how you unroll a loop of unknown size by
     * some constant factor. After this call, var refers to the outer
     * dimension of the split. 'factor' must be an integer. */
    Func &unroll(const VarOrRVar &var, const Expr &factor, TailStrategy tail = TailStrategy::Auto);

    /** Statically declare that the range over which a function should
     * be evaluated is given by the second and third arguments. This
     * can let Halide perform some optimizations. E.g. if you know
     * there are going to be 4 color channels, you can completely
     * vectorize the color channel dimension without the overhead of
     * splitting it up. If bounds inference decides that it requires
     * more of this function than the bounds you have stated, a
     * runtime error will occur when you try to run your pipeline. */
    Func &bound(const Var &var, Expr min, Expr extent);

    /** Statically declare the range over which the function will be
     * evaluated in the general case. This provides a basis for the auto
     * scheduler to make trade-offs and scheduling decisions. The auto
     * generated schedules might break when the sizes of the dimensions are
     * very different from the estimates specified. These estimates are used
     * only by the auto scheduler if the function is a pipeline output. */
    Func &set_estimate(const Var &var, const Expr &min, const Expr &extent);

    /** Set (min, extent) estimates for all dimensions in the Func
     * at once; this is equivalent to calling `set_estimate(args()[n], min, extent)`
     * repeatedly, but slightly terser. The size of the estimates vector
     * must match the dimensionality of the Func. */
    Func &set_estimates(const Region &estimates);

    /** Expand the region computed so that the min coordinates is
     * congruent to 'remainder' modulo 'modulus', and the extent is a
     * multiple of 'modulus'. For example, f.align_bounds(x, 2) forces
     * the min and extent realized to be even, and calling
     * f.align_bounds(x, 2, 1) forces the min to be odd and the extent
     * to be even. The region computed always contains the region that
     * would have been computed without this directive, so no
     * assertions are injected.
     */
    Func &align_bounds(const Var &var, Expr modulus, Expr remainder = 0);

    /** Expand the region computed so that the extent is a
     * multiple of 'modulus'. For example, f.align_extent(x, 2) forces
     * the extent realized to be even. The region computed always contains the
     * region that would have been computed without this directive, so no
     * assertions are injected. (This is essentially equivalent to align_bounds(),
     * but always leaving the min untouched.)
     */
    Func &align_extent(const Var &var, Expr modulus);

    /** Bound the extent of a Func's realization, but not its
     * min. This means the dimension can be unrolled or vectorized
     * even when its min is not fixed (for example because it is
     * compute_at tiles of another Func). This can also be useful for
     * forcing a function's allocation to be a fixed size, which often
     * means it can go on the stack. */
    Func &bound_extent(const Var &var, Expr extent);

    /** Split two dimensions at once by the given factors, and then
     * reorder the resulting dimensions to be xi, yi, xo, yo from
     * innermost outwards. This gives a tiled traversal. */
    Func &tile(const VarOrRVar &x, const VarOrRVar &y,
               const VarOrRVar &xo, const VarOrRVar &yo,
               const VarOrRVar &xi, const VarOrRVar &yi,
               const Expr &xfactor, const Expr &yfactor,
               TailStrategy tail = TailStrategy::Auto);

    /** A shorter form of tile, which reuses the old variable names as
     * the new outer dimensions */
    Func &tile(const VarOrRVar &x, const VarOrRVar &y,
               const VarOrRVar &xi, const VarOrRVar &yi,
               const Expr &xfactor, const Expr &yfactor,
               TailStrategy tail = TailStrategy::Auto);

    /** A more general form of tile, which defines tiles of any dimensionality. */
    Func &tile(const std::vector<VarOrRVar> &previous,
               const std::vector<VarOrRVar> &outers,
               const std::vector<VarOrRVar> &inners,
               const std::vector<Expr> &factors,
               const std::vector<TailStrategy> &tails);

    /** The generalized tile, with a single tail strategy to apply to all vars. */
    Func &tile(const std::vector<VarOrRVar> &previous,
               const std::vector<VarOrRVar> &outers,
               const std::vector<VarOrRVar> &inners,
               const std::vector<Expr> &factors,
               TailStrategy tail = TailStrategy::Auto);

    /** Generalized tiling, reusing the previous names as the outer names. */
    Func &tile(const std::vector<VarOrRVar> &previous,
               const std::vector<VarOrRVar> &inners,
               const std::vector<Expr> &factors,
               TailStrategy tail = TailStrategy::Auto);

    /** Reorder variables to have the given nesting order, from
     * innermost out */
    Func &reorder(const std::vector<VarOrRVar> &vars);

    template<typename... Args>
    HALIDE_NO_USER_CODE_INLINE typename std::enable_if<Internal::all_are_convertible<VarOrRVar, Args...>::value, Func &>::type
    reorder(const VarOrRVar &x, const VarOrRVar &y, Args &&...args) {
        std::vector<VarOrRVar> collected_args{x, y, std::forward<Args>(args)...};
        return reorder(collected_args);
    }

    /** Rename a dimension. Equivalent to split with a inner size of one. */
    Func &rename(const VarOrRVar &old_name, const VarOrRVar &new_name);

    /** Specify that race conditions are permitted for this Func,
     * which enables parallelizing over RVars even when Halide cannot
     * prove that it is safe to do so. Use this with great caution,
     * and only if you can prove to yourself that this is safe, as it
     * may result in a non-deterministic routine that returns
     * different values at different times or on different machines. */
    Func &allow_race_conditions();

    /** Issue atomic updates for this Func. This allows parallelization
     * on associative RVars. The function throws a compile error when
     * Halide fails to prove associativity. Use override_associativity_test
     * to disable the associativity test if you believe the function is
     * associative or the order of reduction variable execution does not
     * matter.
     * Halide compiles this into hardware atomic operations whenever possible,
     * and falls back to a mutex lock per storage element if it is impossible
     * to atomically update.
     * There are three possible outcomes of the compiled code:
     * atomic add, compare-and-swap loop, and mutex lock.
     * For example:
     *
     * hist(x) = 0;
     * hist(im(r)) += 1;
     * hist.compute_root();
     * hist.update().atomic().parallel();
     *
     * will be compiled to atomic add operations.
     *
     * hist(x) = 0;
     * hist(im(r)) = min(hist(im(r)) + 1, 100);
     * hist.compute_root();
     * hist.update().atomic().parallel();
     *
     * will be compiled to compare-and-swap loops.
     *
     * arg_max() = {0, im(0)};
     * Expr old_index = arg_max()[0];
     * Expr old_max   = arg_max()[1];
     * Expr new_index = select(old_max < im(r), r, old_index);
     * Expr new_max   = max(im(r), old_max);
     * arg_max() = {new_index, new_max};
     * arg_max.compute_root();
     * arg_max.update().atomic().parallel();
     *
     * will be compiled to updates guarded by a mutex lock,
     * since it is impossible to atomically update two different locations.
     *
     * Currently the atomic operation is supported by x86, CUDA, and OpenCL backends.
     * Compiling to other backends results in a compile error.
     * If an operation is compiled into a mutex lock, and is vectorized or is
     * compiled to CUDA or OpenCL, it also results in a compile error,
     * since per-element mutex lock on vectorized operation leads to a
     * deadlock.
     * Vectorization of predicated RVars (through rdom.where()) on CPU
     * is also unsupported yet (see https://github.com/halide/Halide/issues/4298).
     * 8-bit and 16-bit atomics on GPU are also not supported. */
    Func &atomic(bool override_associativity_test = false);

    /** Specialize a Func. This creates a special-case version of the
     * Func where the given condition is true. The most effective
     * conditions are those of the form param == value, and boolean
     * Params. Consider a simple example:
     \code
     f(x) = x + select(cond, 0, 1);
     f.compute_root();
     \endcode
     * This is equivalent to:
     \code
     for (int x = 0; x < width; x++) {
       f[x] = x + (cond ? 0 : 1);
     }
     \endcode
     * Adding the scheduling directive:
     \code
     f.specialize(cond)
     \endcode
     * makes it equivalent to:
     \code
     if (cond) {
       for (int x = 0; x < width; x++) {
         f[x] = x;
       }
     } else {
       for (int x = 0; x < width; x++) {
         f[x] = x + 1;
       }
     }
     \endcode
     * Note that the inner loops have been simplified. In the first
     * path Halide knows that cond is true, and in the second path
     * Halide knows that it is false.
     *
     * The specialized version gets its own schedule, which inherits
     * every directive made about the parent Func's schedule so far
     * except for its specializations. This method returns a handle to
     * the new schedule. If you wish to retrieve the specialized
     * sub-schedule again later, you can call this method with the
     * same condition. Consider the following example of scheduling
     * the specialized version:
     *
     \code
     f(x) = x;
     f.compute_root();
     f.specialize(width > 1).unroll(x, 2);
     \endcode
     * Assuming for simplicity that width is even, this is equivalent to:
     \code
     if (width > 1) {
       for (int x = 0; x < width/2; x++) {
         f[2*x] = 2*x;
         f[2*x + 1] = 2*x + 1;
       }
     } else {
       for (int x = 0; x < width/2; x++) {
         f[x] = x;
       }
     }
     \endcode
     * For this case, it may be better to schedule the un-specialized
     * case instead:
     \code
     f(x) = x;
     f.compute_root();
     f.specialize(width == 1); // Creates a copy of the schedule so far.
     f.unroll(x, 2); // Only applies to the unspecialized case.
     \endcode
     * This is equivalent to:
     \code
     if (width == 1) {
       f[0] = 0;
     } else {
       for (int x = 0; x < width/2; x++) {
         f[2*x] = 2*x;
         f[2*x + 1] = 2*x + 1;
       }
     }
     \endcode
     * This can be a good way to write a pipeline that splits,
     * vectorizes, or tiles, but can still handle small inputs.
     *
     * If a Func has several specializations, the first matching one
     * will be used, so the order in which you define specializations
     * is significant. For example:
     *
     \code
     f(x) = x + select(cond1, a, b) - select(cond2, c, d);
     f.specialize(cond1);
     f.specialize(cond2);
     \endcode
     * is equivalent to:
     \code
     if (cond1) {
       for (int x = 0; x < width; x++) {
         f[x] = x + a - (cond2 ? c : d);
       }
     } else if (cond2) {
       for (int x = 0; x < width; x++) {
         f[x] = x + b - c;
       }
     } else {
       for (int x = 0; x < width; x++) {
         f[x] = x + b - d;
       }
     }
     \endcode
     *
     * Specializations may in turn be specialized, which creates a
     * nested if statement in the generated code.
     *
     \code
     f(x) = x + select(cond1, a, b) - select(cond2, c, d);
     f.specialize(cond1).specialize(cond2);
     \endcode
     * This is equivalent to:
     \code
     if (cond1) {
       if (cond2) {
         for (int x = 0; x < width; x++) {
           f[x] = x + a - c;
         }
       } else {
         for (int x = 0; x < width; x++) {
           f[x] = x + a - d;
         }
       }
     } else {
       for (int x = 0; x < width; x++) {
         f[x] = x + b - (cond2 ? c : d);
       }
     }
     \endcode
     * To create a 4-way if statement that simplifies away all of the
     * ternary operators above, you could say:
     \code
     f.specialize(cond1).specialize(cond2);
     f.specialize(cond2);
     \endcode
     * or
     \code
     f.specialize(cond1 && cond2);
     f.specialize(cond1);
     f.specialize(cond2);
     \endcode
     *
     * Any prior Func which is compute_at some variable of this Func
     * gets separately included in all paths of the generated if
     * statement. The Var in the compute_at call to must exist in all
     * paths, but it may have been generated via a different path of
     * splits, fuses, and renames. This can be used somewhat
     * creatively. Consider the following code:
     \code
     g(x, y) = 8*x;
     f(x, y) = g(x, y) + 1;
     f.compute_root().specialize(cond);
     Var g_loop;
     f.specialize(cond).rename(y, g_loop);
     f.rename(x, g_loop);
     g.compute_at(f, g_loop);
     \endcode
     * When cond is true, this is equivalent to g.compute_at(f,y).
     * When it is false, this is equivalent to g.compute_at(f,x).
     */
    Stage specialize(const Expr &condition);

    /** Add a specialization to a Func that always terminates execution
     * with a call to halide_error(). By itself, this is of limited use,
     * but can be useful to terminate chains of specialize() calls where
     * no "default" case is expected (thus avoiding unnecessary code generation).
     *
     * For instance, say we want to optimize a pipeline to process images
     * in planar and interleaved format; we might typically do something like:
     \code
     ImageParam im(UInt(8), 3);
     Func f = do_something_with(im);
     f.specialize(im.dim(0).stride() == 1).vectorize(x, 8);  // planar
     f.specialize(im.dim(2).stride() == 1).reorder(c, x, y).vectorize(c);  // interleaved
     \endcode
     * This code will vectorize along rows for the planar case, and across pixel
     * components for the interleaved case... but there is an implicit "else"
     * for the unhandled cases, which generates unoptimized code. If we never
     * anticipate passing any other sort of images to this, we code streamline
     * our code by adding specialize_fail():
     \code
     ImageParam im(UInt(8), 3);
     Func f = do_something(im);
     f.specialize(im.dim(0).stride() == 1).vectorize(x, 8);  // planar
     f.specialize(im.dim(2).stride() == 1).reorder(c, x, y).vectorize(c);  // interleaved
     f.specialize_fail("Unhandled image format");
     \endcode
     * Conceptually, this produces codes like:
     \code
     if (im.dim(0).stride() == 1) {
        do_something_planar();
     } else if (im.dim(2).stride() == 1) {
        do_something_interleaved();
     } else {
        halide_error("Unhandled image format");
     }
     \endcode
     *
     * Note that calling specialize_fail() terminates the specialization chain
     * for a given Func; you cannot create new specializations for the Func
     * afterwards (though you can retrieve handles to previous specializations).
     */
    void specialize_fail(const std::string &message);

    /** Tell Halide that the following dimensions correspond to GPU
     * thread indices. This is useful if you compute a producer
     * function within the block indices of a consumer function, and
     * want to control how that function's dimensions map to GPU
     * threads. If the selected target is not an appropriate GPU, this
     * just marks those dimensions as parallel. */
    // @{
    Func &gpu_threads(const VarOrRVar &thread_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Func &gpu_threads(const VarOrRVar &thread_x, const VarOrRVar &thread_y, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Func &gpu_threads(const VarOrRVar &thread_x, const VarOrRVar &thread_y, const VarOrRVar &thread_z, DeviceAPI device_api = DeviceAPI::Default_GPU);
    // @}

    /** The given dimension corresponds to the lanes in a GPU
     * warp. GPU warp lanes are distinguished from GPU threads by the
     * fact that all warp lanes run together in lockstep, which
     * permits lightweight communication of data from one lane to
     * another. */
    Func &gpu_lanes(const VarOrRVar &thread_x, DeviceAPI device_api = DeviceAPI::Default_GPU);

    /** Tell Halide to run this stage using a single gpu thread and
     * block. This is not an efficient use of your GPU, but it can be
     * useful to avoid copy-back for intermediate update stages that
     * touch a very small part of your Func. */
    Func &gpu_single_thread(DeviceAPI device_api = DeviceAPI::Default_GPU);

    /** Tell Halide that the following dimensions correspond to GPU
     * block indices. This is useful for scheduling stages that will
     * run serially within each GPU block. If the selected target is
     * not ptx, this just marks those dimensions as parallel. */
    // @{
    Func &gpu_blocks(const VarOrRVar &block_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Func &gpu_blocks(const VarOrRVar &block_x, const VarOrRVar &block_y, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Func &gpu_blocks(const VarOrRVar &block_x, const VarOrRVar &block_y, const VarOrRVar &block_z, DeviceAPI device_api = DeviceAPI::Default_GPU);
    // @}

    /** Tell Halide that the following dimensions correspond to GPU
     * block indices and thread indices. If the selected target is not
     * ptx, these just mark the given dimensions as parallel. The
     * dimensions are consumed by this call, so do all other
     * unrolling, reordering, etc first. */
    // @{
    Func &gpu(const VarOrRVar &block_x, const VarOrRVar &thread_x, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Func &gpu(const VarOrRVar &block_x, const VarOrRVar &block_y,
              const VarOrRVar &thread_x, const VarOrRVar &thread_y, DeviceAPI device_api = DeviceAPI::Default_GPU);
    Func &gpu(const VarOrRVar &block_x, const VarOrRVar &block_y, const VarOrRVar &block_z,
              const VarOrRVar &thread_x, const VarOrRVar &thread_y, const VarOrRVar &thread_z, DeviceAPI device_api = DeviceAPI::Default_GPU);
    // @}

    /** Short-hand for tiling a domain and mapping the tile indices
     * to GPU block indices and the coordinates within each tile to
     * GPU thread indices. Consumes the variables given, so do all
     * other scheduling first. */
    // @{
    Func &gpu_tile(const VarOrRVar &x, const VarOrRVar &bx, const VarOrRVar &tx, const Expr &x_size,
                   TailStrategy tail = TailStrategy::Auto,
                   DeviceAPI device_api = DeviceAPI::Default_GPU);

    Func &gpu_tile(const VarOrRVar &x, const VarOrRVar &tx, const Expr &x_size,
                   TailStrategy tail = TailStrategy::Auto,
                   DeviceAPI device_api = DeviceAPI::Default_GPU);
    Func &gpu_tile(const VarOrRVar &x, const VarOrRVar &y,
                   const VarOrRVar &bx, const VarOrRVar &by,
                   const VarOrRVar &tx, const VarOrRVar &ty,
                   const Expr &x_size, const Expr &y_size,
                   TailStrategy tail = TailStrategy::Auto,
                   DeviceAPI device_api = DeviceAPI::Default_GPU);

    Func &gpu_tile(const VarOrRVar &x, const VarOrRVar &y,
                   const VarOrRVar &tx, const VarOrRVar &ty,
                   const Expr &x_size, const Expr &y_size,
                   TailStrategy tail = TailStrategy::Auto,
                   DeviceAPI device_api = DeviceAPI::Default_GPU);

    Func &gpu_tile(const VarOrRVar &x, const VarOrRVar &y, const VarOrRVar &z,
                   const VarOrRVar &bx, const VarOrRVar &by, const VarOrRVar &bz,
                   const VarOrRVar &tx, const VarOrRVar &ty, const VarOrRVar &tz,
                   const Expr &x_size, const Expr &y_size, const Expr &z_size,
                   TailStrategy tail = TailStrategy::Auto,
                   DeviceAPI device_api = DeviceAPI::Default_GPU);
    Func &gpu_tile(const VarOrRVar &x, const VarOrRVar &y, const VarOrRVar &z,
                   const VarOrRVar &tx, const VarOrRVar &ty, const VarOrRVar &tz,
                   const Expr &x_size, const Expr &y_size, const Expr &z_size,
                   TailStrategy tail = TailStrategy::Auto,
                   DeviceAPI device_api = DeviceAPI::Default_GPU);
    // @}

    /** Schedule for execution on Hexagon. When a loop is marked with
     * Hexagon, that loop is executed on a Hexagon DSP. */
    Func &hexagon(const VarOrRVar &x = Var::outermost());

    /** Prefetch data written to or read from a Func or an ImageParam by a
     * subsequent loop iteration, at an optionally specified iteration offset. You may specify
     * specification of different vars for the location of the prefetch() instruction
     * vs. the location that is being prefetched:
     *
     * - the first var specified, 'at', indicates the loop in which the prefetch will be placed
     * - the second var specified, 'from', determines the var used to find the bounds to prefetch
     *   (in conjunction with 'offset')
     *
     * If 'at' and 'from' are distinct vars, then 'from' must be at a nesting level outside 'at.'
     * Note that the value for 'offset' applies only to 'from', not 'at'.
     *
     * The final argument specifies how prefetch of region outside bounds
     * should be handled.
     *
     * For example, consider this pipeline:
     \code
     Func f, g;
     Var x, y, z;
     f(x, y) = x + y;
     g(x, y) = 2 * f(x, y);
     h(x, y) = 3 * f(x, y);
     \endcode
     *
     * The following schedule:
     \code
     f.compute_root();
     g.prefetch(f, x, x, 2, PrefetchBoundStrategy::NonFaulting);
     h.prefetch(f, x, y, 2, PrefetchBoundStrategy::NonFaulting);
     \endcode
     *
     * will inject prefetch call at the innermost loop of 'g' and 'h' and generate
     * the following loop nest:
     \code
     for y = ...
       for x = ...
         f(x, y) = x + y
     for y = ..
       for x = ...
         prefetch(&f[x + 2, y], 1, 16);
         g(x, y) = 2 * f(x, y)
     for y = ..
       for x = ...
         prefetch(&f[x, y + 2], 1, 16);
         h(x, y) = 3 * f(x, y)
     \endcode
     *
     * Note that the 'from' nesting level need not be adjacent to 'at':
     \code
     Func f, g;
     Var x, y, z, w;
     f(x, y, z, w) = x + y + z + w;
     g(x, y, z, w) = 2 * f(x, y, z, w);
     \endcode
     *
     * The following schedule:
     \code
     f.compute_root();
     g.prefetch(f, y, w, 2, PrefetchBoundStrategy::NonFaulting);
     \endcode
     *
     * will produce code that prefetches a tile of data:
     \code
     for w = ...
       for z = ...
         for y = ...
           for x = ...
         f(x, y, z, w) = x + y + z + w
     for w = ...
       for z = ...
         for y = ...
           for x0 = ...
              prefetch(&f[x0, y, z, w + 2], 1, 16);
           for x = ...
             g(x, y, z, w) = 2 * f(x, y, z, w)
     \endcode
     *
     * Note that calling prefetch() with the same var for both 'at' and 'from'
     * is equivalent to calling prefetch() with that var.
     */
    // @{
    Func &prefetch(const Func &f, const VarOrRVar &at, const VarOrRVar &from, Expr offset = 1,
                   PrefetchBoundStrategy strategy = PrefetchBoundStrategy::GuardWithIf);
    Func &prefetch(const Internal::Parameter &param, const VarOrRVar &at, const VarOrRVar &from, Expr offset = 1,
                   PrefetchBoundStrategy strategy = PrefetchBoundStrategy::GuardWithIf);
    template<typename T>
    Func &prefetch(const T &image, const VarOrRVar &at, const VarOrRVar &from, Expr offset = 1,
                   PrefetchBoundStrategy strategy = PrefetchBoundStrategy::GuardWithIf) {
        return prefetch(image.parameter(), at, from, std::move(offset), strategy);
    }
    // @}

    /** Specify how the storage for the function is laid out. These
     * calls let you specify the nesting order of the dimensions. For
     * example, foo.reorder_storage(y, x) tells Halide to use
     * column-major storage for any realizations of foo, without
     * changing how you refer to foo in the code. You may want to do
     * this if you intend to vectorize across y. When representing
     * color images, foo.reorder_storage(c, x, y) specifies packed
     * storage (red, green, and blue values adjacent in memory), and
     * foo.reorder_storage(x, y, c) specifies planar storage (entire
     * red, green, and blue images one after the other in memory).
     *
     * If you leave out some dimensions, those remain in the same
     * positions in the nesting order while the specified variables
     * are reordered around them. */
    // @{
    Func &reorder_storage(const std::vector<Var> &dims);

    Func &reorder_storage(const Var &x, const Var &y);
    template<typename... Args>
    HALIDE_NO_USER_CODE_INLINE typename std::enable_if<Internal::all_are_convertible<Var, Args...>::value, Func &>::type
    reorder_storage(const Var &x, const Var &y, Args &&...args) {
        std::vector<Var> collected_args{x, y, std::forward<Args>(args)...};
        return reorder_storage(collected_args);
    }
    // @}

    /** Pad the storage extent of a particular dimension of
     * realizations of this function up to be a multiple of the
     * specified alignment. This guarantees that the strides for the
     * dimensions stored outside of dim will be multiples of the
     * specified alignment, where the strides and alignment are
     * measured in numbers of elements.
     *
     * For example, to guarantee that a function foo(x, y, c)
     * representing an image has scanlines starting on offsets
     * aligned to multiples of 16, use foo.align_storage(x, 16). */
    Func &align_storage(const Var &dim, const Expr &alignment);

    /** Store realizations of this function in a circular buffer of a
     * given extent. This is more efficient when the extent of the
     * circular buffer is a power of 2. If the fold factor is too
     * small, or the dimension is not accessed monotonically, the
     * pipeline will generate an error at runtime.
     *
     * The fold_forward option indicates that the new values of the
     * producer are accessed by the consumer in a monotonically
     * increasing order. Folding storage of producers is also
     * supported if the new values are accessed in a monotonically
     * decreasing order by setting fold_forward to false.
     *
     * For example, consider the pipeline:
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x, y+1);
     \endcode
     *
     * If we schedule f like so:
     *
     \code
     g.compute_at(f, y).store_root().fold_storage(y, 2);
     \endcode
     *
     * Then g will be computed at each row of f and stored in a buffer
     * with an extent in y of 2, alternately storing each computed row
     * of g in row y=0 or y=1.
     */
    Func &fold_storage(const Var &dim, const Expr &extent, bool fold_forward = true);

    /** Compute this function as needed for each unique value of the
     * given var for the given calling function f.
     *
     * For example, consider the simple pipeline:
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x, y+1) + g(x+1, y) + g(x+1, y+1);
     \endcode
     *
     * If we schedule f like so:
     *
     \code
     g.compute_at(f, x);
     \endcode
     *
     * Then the C code equivalent to this pipeline will look like this
     *
     \code

     int f[height][width];
     for (int y = 0; y < height; y++) {
         for (int x = 0; x < width; x++) {
             int g[2][2];
             g[0][0] = x*y;
             g[0][1] = (x+1)*y;
             g[1][0] = x*(y+1);
             g[1][1] = (x+1)*(y+1);
             f[y][x] = g[0][0] + g[1][0] + g[0][1] + g[1][1];
         }
     }

     \endcode
     *
     * The allocation and computation of g is within f's loop over x,
     * and enough of g is computed to satisfy all that f will need for
     * that iteration. This has excellent locality - values of g are
     * used as soon as they are computed, but it does redundant
     * work. Each value of g ends up getting computed four times. If
     * we instead schedule f like so:
     *
     \code
     g.compute_at(f, y);
     \endcode
     *
     * The equivalent C code is:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         int g[2][width+1];
         for (int x = 0; x < width; x++) {
             g[0][x] = x*y;
             g[1][x] = x*(y+1);
         }
         for (int x = 0; x < width; x++) {
             f[y][x] = g[0][x] + g[1][x] + g[0][x+1] + g[1][x+1];
         }
     }
     \endcode
     *
     * The allocation and computation of g is within f's loop over y,
     * and enough of g is computed to satisfy all that f will need for
     * that iteration. This does less redundant work (each point in g
     * ends up being evaluated twice), but the locality is not quite
     * as good, and we have to allocate more temporary memory to store
     * g.
     */
    Func &compute_at(const Func &f, const Var &var);

    /** Schedule a function to be computed within the iteration over
     * some dimension of an update domain. Produces equivalent code
     * to the version of compute_at that takes a Var. */
    Func &compute_at(const Func &f, const RVar &var);

    /** Schedule a function to be computed within the iteration over
     * a given LoopLevel. */
    Func &compute_at(LoopLevel loop_level);

    /** Schedule the iteration over the initial definition of this function
     *  to be fused with another stage 's' from outermost loop to a
     * given LoopLevel. */
    // @{
    Func &compute_with(const Stage &s, const VarOrRVar &var, const std::vector<std::pair<VarOrRVar, LoopAlignStrategy>> &align);
    Func &compute_with(const Stage &s, const VarOrRVar &var, LoopAlignStrategy align = LoopAlignStrategy::Auto);
    Func &compute_with(LoopLevel loop_level, const std::vector<std::pair<VarOrRVar, LoopAlignStrategy>> &align);
    Func &compute_with(LoopLevel loop_level, LoopAlignStrategy align = LoopAlignStrategy::Auto);

    /** Compute all of this function once ahead of time. Reusing
     * the example in \ref Func::compute_at :
     *
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x, y+1) + g(x+1, y) + g(x+1, y+1);

     g.compute_root();
     \endcode
     *
     * is equivalent to
     *
     \code
     int f[height][width];
     int g[height+1][width+1];
     for (int y = 0; y < height+1; y++) {
         for (int x = 0; x < width+1; x++) {
             g[y][x] = x*y;
         }
     }
     for (int y = 0; y < height; y++) {
         for (int x = 0; x < width; x++) {
             f[y][x] = g[y][x] + g[y+1][x] + g[y][x+1] + g[y+1][x+1];
         }
     }
     \endcode
     *
     * g is computed once ahead of time, and enough is computed to
     * satisfy all uses of it. This does no redundant work (each point
     * in g is evaluated once), but has poor locality (values of g are
     * probably not still in cache when they are used by f), and
     * allocates lots of temporary memory to store g.
     */
    Func &compute_root();

    /** Use the halide_memoization_cache_... interface to store a
     *  computed version of this function across invocations of the
     *  Func.
     *
     * If an eviction_key is provided, it must be constructed with
     * Expr of integer or handle type. The key Expr will be promoted
     * to a uint64_t and can be used with halide_memoization_cache_evict
     * to remove memoized entries using this eviction key from the
     * cache. Memoized computations that do not provide an eviction
     * key will never be evicted by this mechanism.
     */
    Func &memoize(const EvictionKey &eviction_key = EvictionKey());

    /** Produce this Func asynchronously in a separate
     * thread. Consumers will be run by the task system when the
     * production is complete. If this Func's store level is different
     * to its compute level, consumers will be run concurrently,
     * blocking as necessary to prevent reading ahead of what the
     * producer has computed. If storage is folded, then the producer
     * will additionally not be permitted to run too far ahead of the
     * consumer, to avoid clobbering data that has not yet been
     * used.
     *
     * Take special care when combining this with custom thread pool
     * implementations, as avoiding deadlock with producer-consumer
     * parallelism requires a much more sophisticated parallel runtime
     * than with data parallelism alone. It is strongly recommended
     * you just use Halide's default thread pool, which guarantees no
     * deadlock and a bound on the number of threads launched.
     */
    Func &async();

    /** Bound the extent of a Func's storage, but not extent of its
     * compute. This can be useful for forcing a function's allocation
     * to be a fixed size, which often means it can go on the stack.
     * If bounds inference decides that it requires more storage for
     * this function than the allocation size you have stated, a runtime
     * error will occur when you try to run the pipeline. */
    Func &bound_storage(const Var &dim, const Expr &bound);

    /** Allocate storage for this function within f's loop over
     * var. Scheduling storage is optional, and can be used to
     * separate the loop level at which storage occurs from the loop
     * level at which computation occurs to trade off between locality
     * and redundant work. This can open the door for two types of
     * optimization.
     *
     * Consider again the pipeline from \ref Func::compute_at :
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x+1, y) + g(x, y+1) + g(x+1, y+1);
     \endcode
     *
     * If we schedule it like so:
     *
     \code
     g.compute_at(f, x).store_at(f, y);
     \endcode
     *
     * Then the computation of g takes place within the loop over x,
     * but the storage takes place within the loop over y:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         int g[2][width+1];
         for (int x = 0; x < width; x++) {
             g[0][x] = x*y;
             g[0][x+1] = (x+1)*y;
             g[1][x] = x*(y+1);
             g[1][x+1] = (x+1)*(y+1);
             f[y][x] = g[0][x] + g[1][x] + g[0][x+1] + g[1][x+1];
         }
     }
     \endcode
     *
     * Provided the for loop over x is serial, halide then
     * automatically performs the following sliding window
     * optimization:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         int g[2][width+1];
         for (int x = 0; x < width; x++) {
             if (x == 0) {
                 g[0][x] = x*y;
                 g[1][x] = x*(y+1);
             }
             g[0][x+1] = (x+1)*y;
             g[1][x+1] = (x+1)*(y+1);
             f[y][x] = g[0][x] + g[1][x] + g[0][x+1] + g[1][x+1];
         }
     }
     \endcode
     *
     * Two of the assignments to g only need to be done when x is
     * zero. The rest of the time, those sites have already been
     * filled in by a previous iteration. This version has the
     * locality of compute_at(f, x), but allocates more memory and
     * does much less redundant work.
     *
     * Halide then further optimizes this pipeline like so:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         int g[2][2];
         for (int x = 0; x < width; x++) {
             if (x == 0) {
                 g[0][0] = x*y;
                 g[1][0] = x*(y+1);
             }
             g[0][(x+1)%2] = (x+1)*y;
             g[1][(x+1)%2] = (x+1)*(y+1);
             f[y][x] = g[0][x%2] + g[1][x%2] + g[0][(x+1)%2] + g[1][(x+1)%2];
         }
     }
     \endcode
     *
     * Halide has detected that it's possible to use a circular buffer
     * to represent g, and has reduced all accesses to g modulo 2 in
     * the x dimension. This optimization only triggers if the for
     * loop over x is serial, and if halide can statically determine
     * some power of two large enough to cover the range needed. For
     * powers of two, the modulo operator compiles to more efficient
     * bit-masking. This optimization reduces memory usage, and also
     * improves locality by reusing recently-accessed memory instead
     * of pulling new memory into cache.
     *
     */
    Func &store_at(const Func &f, const Var &var);

    /** Equivalent to the version of store_at that takes a Var, but
     * schedules storage within the loop over a dimension of a
     * reduction domain */
    Func &store_at(const Func &f, const RVar &var);

    /** Equivalent to the version of store_at that takes a Var, but
     * schedules storage at a given LoopLevel. */
    Func &store_at(LoopLevel loop_level);

    /** Equivalent to \ref Func::store_at, but schedules storage
     * outside the outermost loop. */
    Func &store_root();

    /** Aggressively inline all uses of this function. This is the
     * default schedule, so you're unlikely to need to call this. For
     * a Func with an update definition, that means it gets computed
     * as close to the innermost loop as possible.
     *
     * Consider once more the pipeline from \ref Func::compute_at :
     *
     \code
     Func f, g;
     Var x, y;
     g(x, y) = x*y;
     f(x, y) = g(x, y) + g(x+1, y) + g(x, y+1) + g(x+1, y+1);
     \endcode
     *
     * Leaving g as inline, this compiles to code equivalent to the following C:
     *
     \code
     int f[height][width];
     for (int y = 0; y < height; y++) {
         for (int x = 0; x < width; x++) {
             f[y][x] = x*y + x*(y+1) + (x+1)*y + (x+1)*(y+1);
         }
     }
     \endcode
     */
    Func &compute_inline();

    /** Get a handle on an update step for the purposes of scheduling
     * it. */
    Stage update(int idx = 0);

    /** Set the type of memory this Func should be stored in. Controls
     * whether allocations go on the stack or the heap on the CPU, and
     * in global vs shared vs local on the GPU. See the documentation
     * on MemoryType for more detail. */
    Func &store_in(MemoryType memory_type);

    /** Trace all loads from this Func by emitting calls to
     * halide_trace. If the Func is inlined, this has no
     * effect. */
    Func &trace_loads();

    /** Trace all stores to the buffer backing this Func by emitting
     * calls to halide_trace. If the Func is inlined, this call
     * has no effect. */
    Func &trace_stores();

    /** Trace all realizations of this Func by emitting calls to
     * halide_trace. */
    Func &trace_realizations();

    /** Add a string of arbitrary text that will be passed thru to trace
     * inspection code if the Func is realized in trace mode. (Funcs that are
     * inlined won't have their tags emitted.) Ignored entirely if
     * tracing is not enabled for the Func (or globally).
     */
    Func &add_trace_tag(const std::string &trace_tag);

    /** Get a handle on the internal halide function that this Func
     * represents. Useful if you want to do introspection on Halide
     * functions */
    Internal::Function function() const {
        return func;
    }

    /** You can cast a Func to its pure stage for the purposes of
     * scheduling it. */
    operator Stage() const;

    /** Get a handle on the output buffer for this Func. Only relevant
     * if this is the output Func in a pipeline. Useful for making
     * static promises about strides, mins, and extents. */
    // @{
    OutputImageParam output_buffer() const;
    std::vector<OutputImageParam> output_buffers() const;
    // @}

    /** Use a Func as an argument to an external stage. */
    operator ExternFuncArgument() const;

    /** Infer the arguments to the Func, sorted into a canonical order:
     * all buffers (sorted alphabetically by name), followed by all non-buffers
     * (sorted alphabetically by name).
     This lets you write things like:
     \code
     func.compile_to_assembly("/dev/stdout", func.infer_arguments());
     \endcode
     */
    std::vector<Argument> infer_arguments() const;

    /** Get the source location of the pure definition of this
     * Func. See Stage::source_location() */
    std::string source_location() const;

    /** Return the current StageSchedule associated with this initial
     * Stage of this Func. For introspection only: to modify schedule,
     * use the Func interface. */
    const Internal::StageSchedule &get_schedule() const {
        return Stage(*this).get_schedule();
    }
};

namespace Internal {

template<typename Last>
inline void check_types(const Tuple &t, int idx) {
    using T = typename std::remove_pointer<typename std::remove_reference<Last>::type>::type;
    user_assert(t[idx].type() == type_of<T>())
        << "Can't evaluate expression "
        << t[idx] << " of type " << t[idx].type()
        << " as a scalar of type " << type_of<T>() << "\n";
}

template<typename First, typename Second, typename... Rest>
inline void check_types(const Tuple &t, int idx) {
    check_types<First>(t, idx);
    check_types<Second, Rest...>(t, idx + 1);
}

template<typename Last>
inline void assign_results(Realization &r, int idx, Last last) {
    using T = typename std::remove_pointer<typename std::remove_reference<Last>::type>::type;
    *last = Buffer<T>(r[idx])();
}

template<typename First, typename Second, typename... Rest>
inline void assign_results(Realization &r, int idx, First first, Second second, Rest &&...rest) {
    assign_results<First>(r, idx, first);
    assign_results<Second, Rest...>(r, idx + 1, second, rest...);
}

}  // namespace Internal

/** JIT-Compile and run enough code to evaluate a Halide
 * expression. This can be thought of as a scalar version of
 * \ref Func::realize */
template<typename T>
HALIDE_NO_USER_CODE_INLINE T evaluate(JITUserContext *ctx, const Expr &e) {
    user_assert(e.type() == type_of<T>())
        << "Can't evaluate expression "
        << e << " of type " << e.type()
        << " as a scalar of type " << type_of<T>() << "\n";
    Func f;
    f() = e;
    Buffer<T, 0> im = f.realize(ctx);
    return im();
}

/** evaluate with a default user context */
template<typename T>
HALIDE_NO_USER_CODE_INLINE T evaluate(const Expr &e) {
    return evaluate<T>(nullptr, e);
}

/** JIT-compile and run enough code to evaluate a Halide Tuple. */
template<typename First, typename... Rest>
HALIDE_NO_USER_CODE_INLINE void evaluate(JITUserContext *ctx, Tuple t, First first, Rest &&...rest) {
    Internal::check_types<First, Rest...>(t, 0);

    Func f;
    f() = t;
    Realization r = f.realize(ctx);
    Internal::assign_results(r, 0, first, rest...);
}

/** JIT-compile and run enough code to evaluate a Halide Tuple. */
template<typename First, typename... Rest>
HALIDE_NO_USER_CODE_INLINE void evaluate(Tuple t, First first, Rest &&...rest) {
    evaluate<First, Rest...>(nullptr, std::move(t), std::forward<First>(first), std::forward<Rest...>(rest...));
}

namespace Internal {

inline void schedule_scalar(Func f) {
    Target t = get_jit_target_from_environment();
    if (t.has_gpu_feature()) {
        f.gpu_single_thread();
    }
    if (t.has_feature(Target::HVX)) {
        f.hexagon();
    }
}

}  // namespace Internal

/** JIT-Compile and run enough code to evaluate a Halide
 * expression. This can be thought of as a scalar version of
 * \ref Func::realize. Can use GPU if jit target from environment
 * specifies one.
 */
template<typename T>
HALIDE_NO_USER_CODE_INLINE T evaluate_may_gpu(const Expr &e) {
    user_assert(e.type() == type_of<T>())
        << "Can't evaluate expression "
        << e << " of type " << e.type()
        << " as a scalar of type " << type_of<T>() << "\n";
    Func f;
    f() = e;
    Internal::schedule_scalar(f);
    Buffer<T, 0> im = f.realize();
    return im();
}

/** JIT-compile and run enough code to evaluate a Halide Tuple. Can
 *  use GPU if jit target from environment specifies one. */
// @{
template<typename First, typename... Rest>
HALIDE_NO_USER_CODE_INLINE void evaluate_may_gpu(Tuple t, First first, Rest &&...rest) {
    Internal::check_types<First, Rest...>(t, 0);

    Func f;
    f() = t;
    Internal::schedule_scalar(f);
    Realization r = f.realize();
    Internal::assign_results(r, 0, first, rest...);
}
// @}

}  // namespace Halide

#endif
