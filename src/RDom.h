#ifndef HALIDE_RDOM_H
#define HALIDE_RDOM_H

/** \file
 * Defines the front-end syntax for reduction domains and reduction
 * variables.
 */

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Expr.h"
#include "Reduction.h"
#include "Util.h"

namespace Halide {

template<typename T, int Dims>
class Buffer;
class OutputImageParam;

/** A reduction variable represents a single dimension of a reduction
 * domain (RDom). Don't construct them directly, instead construct an
 * RDom, and use RDom::operator[] to get at the variables. For
 * single-dimensional reduction domains, you can just cast a
 * single-dimensional RDom to an RVar. */
class RVar {
    std::string _name;
    Internal::ReductionDomain _domain;
    int _index = -1;

    const Internal::ReductionVariable &_var() const {
        const auto &d = _domain.domain();
        internal_assert(_index >= 0 && _index < (int)d.size());
        return d.at(_index);
    }

public:
    /** An empty reduction variable. */
    RVar()
        : _name(Internal::make_entity_name(this, "Halide:.*:RVar", 'r')) {
    }

    /** Construct an RVar with the given name */
    explicit RVar(const std::string &n)
        : _name(n) {
    }

    /** Construct a reduction variable with the given name and
     * bounds. Must be a member of the given reduction domain. */
    RVar(Internal::ReductionDomain domain, int index)
        : _domain(std::move(domain)), _index(index) {
    }

    /** The minimum value that this variable will take on */
    Expr min() const;

    /** The number that this variable will take on. The maximum value
     * of this variable will be min() + extent() - 1 */
    Expr extent() const;

    /** The reduction domain this is associated with. */
    Internal::ReductionDomain domain() const {
        return _domain;
    }

    /** The name of this reduction variable */
    const std::string &name() const;

    /** Reduction variables can be used as expressions. */
    operator Expr() const;
};

/** A multi-dimensional domain over which to iterate. Used when
 * defining functions with update definitions.
 *
 * An reduction is a function with a two-part definition. It has an
 * initial value, which looks much like a pure function, and an update
 * definition, which may refer to some RDom. Evaluating such a
 * function first initializes it over the required domain (which is
 * inferred based on usage), and then runs update rule for all points
 * in the RDom. For example:
 *
 \code
 Func f;
 Var x;
 RDom r(0, 10);
 f(x) = x; // the initial value
 f(r) = f(r) * 2;
 Buffer<int> result = f.realize({10});
 \endcode
 *
 * This function creates a single-dimensional buffer of size 10, in
 * which element x contains the value x*2. Internally, first the
 * initialization rule fills in x at every site, and then the update
 * definition doubles every site.
 *
 * One use of reductions is to build a function recursively (pure
 * functions in halide cannot be recursive). For example, this
 * function fills in an array with the first 20 fibonacci numbers:
 *
 \code
 Func f;
 Var x;
 RDom r(2, 18);
 f(x) = 1;
 f(r) = f(r-1) + f(r-2);
 \endcode
 *
 * Another use of reductions is to perform scattering operations, as
 * unlike a pure function declaration, the left-hand-side of an update
 * definition may contain general expressions:
 *
 \code
 ImageParam input(UInt(8), 2);
 Func histogram;
 Var x;
 RDom r(input); // Iterate over all pixels in the input
 histogram(x) = 0;
 histogram(input(r.x, r.y)) = histogram(input(r.x, r.y)) + 1;
 \endcode
 *
 * An update definition may also be multi-dimensional. This example
 * computes a summed-area table by first summing horizontally and then
 * vertically:
 *
 \code
 ImageParam input(Float(32), 2);
 Func sum_x, sum_y;
 Var x, y;
 RDom r(input);
 sum_x(x, y)     = input(x, y);
 sum_x(r.x, r.y) = sum_x(r.x, r.y) + sum_x(r.x-1, r.y);
 sum_y(x, y)     = sum_x(x, y);
 sum_y(r.x, r.y) = sum_y(r.x, r.y) + sum_y(r.x, r.y-1);
 \endcode
 *
 * You can also mix pure dimensions with reduction variables. In the
 * previous example, note that there's no need for the y coordinate in
 * sum_x to be traversed serially. The sum within each row is entirely
 * independent. The rows could be computed in parallel, or in a
 * different order, without changing the meaning. Therefore, we can
 * instead write this definition as follows:
 *
 \code
 ImageParam input(Float(32), 2);
 Func sum_x, sum_y;
 Var x, y;
 RDom r(input);
 sum_x(x, y)   = input(x, y);
 sum_x(r.x, y) = sum_x(r.x, y) + sum_x(r.x-1, y);
 sum_y(x, y)   = sum_x(x, y);
 sum_y(x, r.y) = sum_y(x, r.y) + sum_y(x, r.y-1);
 \endcode
 *
 * This lets us schedule it more flexibly. You can now parallelize the
 * update step of sum_x over y by calling:
 \code
 sum_x.update().parallel(y).
 \endcode
 *
 * Note that calling sum_x.parallel(y) only parallelizes the
 * initialization step, and not the update step! Scheduling the update
 * step of a reduction must be done using the handle returned by
 * \ref Func::update(). This code parallelizes both the initialization
 * step and the update step:
 *
 \code
 sum_x.parallel(y);
 sum_x.update().parallel(y);
 \endcode
 *
 * When you mix reduction variables and pure dimensions, the reduction
 * domain is traversed outermost. That is, for each point in the
 * reduction domain, the inferred pure domain is traversed in its
 * entirety. For the above example, this means that sum_x walks down
 * the columns, and sum_y walks along the rows. This may not be
 * cache-coherent. You may try reordering these dimensions using the
 * schedule, but Halide will return an error if it decides that this
 * risks changing the meaning of your function. The solution lies in
 * clever scheduling. If we say:
 *
 \code
 sum_x.compute_at(sum_y, y);
 \endcode
 *
 * Then the sum in x is computed only as necessary for each scanline
 * of the sum in y. This not only results in sum_x walking along the
 * rows, it also improves the locality of the entire pipeline.
 */
class RDom {
    Internal::ReductionDomain dom;

    void init_vars(const std::string &name);

    void initialize_from_region(const Region &region, std::string name = "");

    template<typename... Args>
    HALIDE_NO_USER_CODE_INLINE void initialize_from_region(Region &region, const Expr &min, const Expr &extent, Args &&...args) {
        region.push_back({min, extent});
        initialize_from_region(region, std::forward<Args>(args)...);
    }

public:
    /** Construct an undefined reduction domain. */
    RDom() = default;

    /** Construct a multi-dimensional reduction domain with the given name. If the name
     * is left blank, a unique one is auto-generated. */
    // @{
    HALIDE_NO_USER_CODE_INLINE RDom(const Region &region, std::string name = "") {
        initialize_from_region(region, std::move(name));
    }

    template<typename... Args>
    HALIDE_NO_USER_CODE_INLINE RDom(Expr min, Expr extent, Args &&...args) {
        // This should really just be a delegating constructor, but I couldn't make
        // that work with variadic template unpacking in visual studio 2013
        Region region;
        initialize_from_region(region, min, extent, std::forward<Args>(args)...);
    }
    // @}

    /** Construct a reduction domain that iterates over all points in
     * a given Buffer or ImageParam. Has the same dimensionality as
     * the argument. */
    // @{
    RDom(const Buffer<void, -1> &);
    RDom(const OutputImageParam &);
    template<typename T, int Dims>
    HALIDE_NO_USER_CODE_INLINE RDom(const Buffer<T, Dims> &im)
        : RDom(Buffer<void, -1>(im)) {
    }
    // @}

    /** Construct a reduction domain that wraps an Internal ReductionDomain object. */
    RDom(const Internal::ReductionDomain &d);

    /** Get at the internal reduction domain object that this wraps. */
    Internal::ReductionDomain domain() const {
        return dom;
    }

    /** Check if this reduction domain is non-null */
    bool defined() const {
        return dom.defined();
    }

    /** Compare two reduction domains for equality of reference */
    bool same_as(const RDom &other) const {
        return dom.same_as(other.dom);
    }

    /** Get the dimensionality of a reduction domain */
    int dimensions() const;

    /** Get at one of the dimensions of the reduction domain */
    RVar operator[](int) const;

    /** Single-dimensional reduction domains can be used as RVars directly. */
    operator RVar() const;

    /** Single-dimensional reduction domains can be also be used as Exprs directly. */
    operator Expr() const;

    /** Add a predicate to the RDom. An RDom may have multiple
     * predicates associated with it. An update definition that uses
     * an RDom only iterates over the subset points in the domain for
     * which all of its predicates are true. The predicate expression
     * obeys the same rules as the expressions used on the
     * right-hand-side of the corresponding update definition. It may
     * refer to the RDom's variables and free variables in the Func's
     * update definition. It may include calls to other Funcs, or make
     * recursive calls to the same Func. This permits iteration over
     * non-rectangular domains, or domains with sizes that vary with
     * some free variable, or domains with shapes determined by some
     * other Func.
     *
     * Note that once RDom is used in the update definition of some
     * Func, no new predicates can be added to the RDom.
     *
     * Consider a simple example:
     \code
     RDom r(0, 20, 0, 20);
     r.where(r.x < r.y);
     r.where(r.x == 10);
     r.where(r.y > 13);
     f(r.x, r.y) += 1;
     \endcode
     * This is equivalent to:
     \code
     for (int r.y = 0; r.y < 20; r.y++) {
       if (r.y > 13) {
         for (int r.x = 0; r.x < 20; r.x++) {
           if (r.x == 10) {
             if (r.x < r.y) {
               f[r.x, r.y] += 1;
             }
           }
         }
       }
     }
     \endcode
     *
     * Where possible Halide restricts the range of the containing for
     * loops to avoid the cases where the predicate is false so that
     * the if statement can be removed entirely. The case above would
     * be further simplified into:
     *
     \code
     for (int r.y = 14; r.y < 20; r.y++) {
       f[10, r.y] += 1;
     }
     \endcode
     *
     * In general, the predicates that we can simplify away by
     * restricting loop ranges are inequalities that compare an inner
     * Var or RVar to some expression in outer Vars or RVars.
     *
     * You can also pack multiple conditions into one predicate like so:
     *
     \code
     RDom r(0, 20, 0, 20);
     r.where((r.x < r.y) && (r.x == 10) && (r.y > 13));
     f(r.x, r.y) += 1;
     \endcode
     *
     */
    void where(Expr predicate);

    /** Direct access to the first four dimensions of the reduction
     * domain. Some of these variables may be undefined if the
     * reduction domain has fewer than four dimensions. */
    // @{
    RVar x, y, z, w;
    // @}
};

/** Emit an RVar in a human-readable form */
std::ostream &operator<<(std::ostream &stream, const RVar &);

/** Emit an RDom in a human-readable form. */
std::ostream &operator<<(std::ostream &stream, const RDom &);
}  // namespace Halide

#endif
