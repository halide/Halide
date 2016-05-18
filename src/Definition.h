#ifndef HALIDE_DEFINITION_H
#define HALIDE_DEFINITION_H

/** \file
 * Defines the internal representation of a halide function's definition and related classes
 */

#include "Expr.h"
#include "IntrusivePtr.h"
#include "Schedule.h"
#include "Reduction.h"

#include <map>

namespace Halide {

namespace Internal {
struct DefinitionContents;
struct FunctionContents;
}

namespace Internal {

class IRVisitor;
class IRMutator;
struct Specialization;

/** A Function definition which can either represent a pure or an update
 * definition. If it's a pure definition, then reduction domain is undefined.
 * A function may have different definitions due to specialization, which are
 * stored in 'specializations' (Not possible from the front-end, but some
 * scheduling directives may potentially cause this divergence to occur). */
class Definition {

    IntrusivePtr<DefinitionContents> contents;

public:
    /** Construct a Definition from an existing DefinitionContents pointer. Must be non-null */
    EXPORT explicit Definition(const IntrusivePtr<DefinitionContents> &);

    /** Construct a Definition with the supplied args, values, and reduction domain. */
    EXPORT Definition(const std::vector<Expr> &args, const std::vector<Expr> &values,
                      const ReductionDomain &rdom);

    EXPORT Definition();

    /** Return a deep copy of this Definition. It recursively deep copies all
     * called functions, schedules, and reduction domains. This method
     * takes a map of <old FunctionContents, deep-copied version> as input and
     * would use the deep-copied FunctionContents from the map if exists instead
     * of creating a new deep-copy to avoid creating deep-copies of the same
     * FunctionContents multiple times.
     */
    EXPORT Definition deep_copy(
        std::map<IntrusivePtr<FunctionContents>, IntrusivePtr<FunctionContents>> &copied_map) const;

    /** Equality of identity */
    bool same_as(const Definition &other) const {
        return contents.same_as(other.contents);
    }

    /** Is this function a pure definition */
    bool is_pure() const;

    /** Pass an IRVisitor through to all Exprs referenced in the
     * Definition. */
    void accept(IRVisitor *) const;

    /** Pass an IRMutator through to all Exprs referenced in the
     * Definition. */
    void mutate(IRMutator *);

    /** Get the arguments */
    // @{
    const std::vector<Expr> &args() const;
    std::vector<Expr> &args();
    // @}

    /** Get the right-hand-side of the definition */
    // @{
    const std::vector<Expr> &values() const;
    std::vector<Expr> &values();
    // @}

    /** Get the schedule associated with this Definition. */
    // @{
    Schedule &schedule();
    const Schedule &schedule() const;
    // @}

    /** Any reduction domain associated with this function's definition. */
    // @{
    const ReductionDomain &domain() const;
    void set_domain(const ReductionDomain &d);
    // @}

    /** You may create several specialized versions of a func with
     * different schedules. They trigger when the condition is
     * true. See \ref Func::specialize */
    // @{
    const std::vector<Specialization> &specializations() const;
    const Specialization &add_specialization(Expr condition);
    // @}

};

struct Specialization {
    Expr condition;
    Definition definition;
};

}}

#endif
