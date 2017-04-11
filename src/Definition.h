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

/** A Function definition which can either represent a init or an update
 * definition. A function may have different definitions due to specialization,
 * which are stored in 'specializations' (Not possible from the front-end, but
 * some scheduling directives may potentially cause this divergence to occur).
 * Although init definition may have multiple values (RHS) per specialization, it
 * must have the same LHS (i.e. same pure dimension variables). The update
 * definition, on the other hand, may have different LHS/RHS per specialization.
 * Note that, while the Expr in LHS/RHS may be different across specializations,
 * they must have the same number of dimensions and the same pure dimensions.
 */
class Definition {

    IntrusivePtr<DefinitionContents> contents;

public:
    /** Construct a Definition from an existing DefinitionContents pointer. Must be non-null */
    EXPORT explicit Definition(const IntrusivePtr<DefinitionContents> &);

    /** Construct a Definition with the supplied args, values, and reduction domain. */
    EXPORT Definition(const std::vector<Expr> &args, const std::vector<Expr> &values,
                      const ReductionDomain &rdom, bool is_init);

    /** Construct an empty Definition. By default, it is a init definition. */
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

    /** Is this an init definition; otherwise it's an update definition */
    EXPORT bool is_init() const;

    /** Pass an IRVisitor through to all Exprs referenced in the
     * definition. */
    EXPORT void accept(IRVisitor *) const;

    /** Pass an IRMutator through to all Exprs referenced in the
     * definition. */
    EXPORT void mutate(IRMutator *);

    /** Get the default (no-specialization) arguments (left-hand-side) of the definition */
    // @{
    EXPORT const std::vector<Expr> &args() const;
    EXPORT std::vector<Expr> &args();
    // @}

    /** Get the default (no-specialization) right-hand-side of the definition */
    // @{
    EXPORT const std::vector<Expr> &values() const;
    EXPORT std::vector<Expr> &values();
    // @}

    /** Get the predicate on the definition */
    // @{
    EXPORT const Expr &predicate() const;
    EXPORT Expr &predicate();
    // @}

    /** Split predicate into vector of ANDs. If there is no predicate (i.e. this
     * definition is always valid), this returns an empty vector. */
    EXPORT std::vector<Expr> split_predicate() const;

    /** Get the default (no-specialization) schedule associated with this definition. */
    // @{
    EXPORT const Schedule &schedule() const;
    EXPORT Schedule &schedule();
    // @}

    /** You may create several specialized versions of a func with
     * different schedules. They trigger when the condition is
     * true. See \ref Func::specialize */
    // @{
    EXPORT const std::vector<Specialization> &specializations() const;
    EXPORT std::vector<Specialization> &specializations();
    EXPORT const Specialization &add_specialization(Expr condition);
    // @}

};

struct Specialization {
    Expr condition;
    Definition definition;
};

}}

#endif
