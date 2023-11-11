#ifndef HALIDE_DEFINITION_H
#define HALIDE_DEFINITION_H

/** \file
 * Defines the internal representation of a halide function's definition and related classes
 */

#include "Expr.h"
#include "IntrusivePtr.h"
#include "Schedule.h"

#include <map>

namespace Halide {

namespace Internal {
struct DefinitionContents;
struct FunctionContents;
class ReductionDomain;
}  // namespace Internal

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
    explicit Definition(const IntrusivePtr<DefinitionContents> &);

    /** Construct a Definition with the supplied args, values, and reduction domain. */
    Definition(const std::vector<Expr> &args, const std::vector<Expr> &values,
               const ReductionDomain &rdom, bool is_init);

    /** Construct a Definition with deserialized data. */
    Definition(bool is_init, const Expr &predicate, const std::vector<Expr> &args, const std::vector<Expr> &values,
               const StageSchedule &schedule, const std::vector<Specialization> &specializations, const std::string &source_location);

    /** Construct an undefined Definition object. */
    Definition();

    /** Return a copy of this Definition. */
    Definition get_copy() const;

    /** Equality of identity */
    bool same_as(const Definition &other) const {
        return contents.same_as(other.contents);
    }

    /** Definition objects are nullable. Does this definition exist? */
    bool defined() const;

    /** Is this an init definition; otherwise it's an update definition */
    bool is_init() const;

    /** Pass an IRVisitor through to all Exprs referenced in the
     * definition. */
    void accept(IRVisitor *) const;

    /** Pass an IRMutator through to all Exprs referenced in the
     * definition. */
    void mutate(IRMutator *);

    /** Get the default (no-specialization) arguments (left-hand-side) of the definition.
     *
     * Warning: Any Vars in the Exprs are not qualified with the Func name, so
     * the Exprs may contain names which collide with names provided by
     * unique_name.
     */
    // @{
    const std::vector<Expr> &args() const;
    std::vector<Expr> &args();
    // @}

    /** Get the default (no-specialization) right-hand-side of the definition.
     *
     * Warning: Any Vars in the Exprs are not qualified with the Func name, so
     * the Exprs may contain names which collide with names provided by
     * unique_name.
     */
    // @{
    const std::vector<Expr> &values() const;
    std::vector<Expr> &values();
    // @}

    /** Get the predicate on the definition */
    // @{
    const Expr &predicate() const;
    Expr &predicate();
    // @}

    /** Split predicate into vector of ANDs. If there is no predicate (i.e. this
     * definition is always valid), this returns an empty vector. */
    std::vector<Expr> split_predicate() const;

    /** Get the default (no-specialization) stage-specific schedule associated
     * with this definition. */
    // @{
    const StageSchedule &schedule() const;
    StageSchedule &schedule();
    // @}

    /** You may create several specialized versions of a func with
     * different stage-specific schedules. They trigger when the condition is
     * true. See \ref Func::specialize */
    // @{
    const std::vector<Specialization> &specializations() const;
    std::vector<Specialization> &specializations();
    const Specialization &add_specialization(Expr condition);
    // @}

    /** Attempt to get the source file and line where this definition
     * was made using DWARF introspection. Returns an empty string if
     * no debug symbols were found or the debug symbols were not
     * understood. Works on OS X and Linux only. */
    std::string source_location() const;
};

struct Specialization {
    Expr condition;
    Definition definition;
    std::string failure_message;  // If non-empty, this specialization always assert-fails with this message.
};

}  // namespace Internal
}  // namespace Halide

#endif
