#ifndef HALIDE_FUNCTION_H
#define HALIDE_FUNCTION_H

/** \file
 * Defines the internal representation of a halide function and related classes
 */

#include "IntrusivePtr.h"
#include "Reduction.h"
#include "Schedule.h"
#include <string>
#include <vector>

namespace Halide { 
namespace Internal {

struct FunctionContents {
    mutable RefCount ref_count;
    std::string name;
    std::vector<std::string> args;
    Expr value;
    Schedule schedule;

    Expr reduction_value;
    std::vector<Expr> reduction_args;
    Schedule reduction_schedule;
    ReductionDomain reduction_domain;
};        

/** A reference-counted handle to Halide's internal representation of
 * a function. Similar to a front-end Func object, but with no
 * syntactic sugar to help with definitions. */
class Function {
private:
    IntrusivePtr<FunctionContents> contents;
public:
    /** Construct a new function with no definitions and no name. This
     * constructor only exists so that you can make vectors of
     * functions, etc.
     */
    Function() : contents(new FunctionContents) {}

    /** Add a pure definition to this function. It may not already
     * have a definition. All the free variables in 'value' must
     * appear in the args list. 'value' must not depend on any
     * reduction domain */
    void define(const std::vector<std::string> &args, Expr value);   

    /** Add a reduction definition to this function. It must already
     * have a pure definition but not a reduction definition, and the
     * length of args must match the length of args used in the pure
     * definition. 'value' must depend on some reduction domain, and
     * may contain variables from that domain as well as pure
     * variables. Any pure variables must also appear as Variables in
     * the args array, and they must have the same name as the pure
     * definition's argument in the same index. */
    void define_reduction(const std::vector<Expr> &args, Expr value);

    /** Construct a new function with the given name */
    Function(const std::string &n) : contents(new FunctionContents) {
        contents.ptr->name = n;
    }

    /** Get the name of the function */
    const std::string &name() const {
        return contents.ptr->name;
    }

    /** Get the pure arguments */
    const std::vector<std::string> &args() const {
        return contents.ptr->args;
    }

    /** Get the right-hand-side of the pure definition */
    Expr value() const {
        return contents.ptr->value;
    }

    /** Get a handle to the schedule for the purpose of modifying
     * it */
    Schedule &schedule() {
        return contents.ptr->schedule;
    }   

    /** Get a const handle to the schedule for inspecting it */
    const Schedule &schedule() const {
        return contents.ptr->schedule;
    }   

    /** Get a mutable handle to the schedule for the reduction
     * stage */
    Schedule &reduction_schedule() {
        return contents.ptr->reduction_schedule;
    }

    /** Get a const handle to the schedule for the reduction stage */
    const Schedule &reduction_schedule() const {
        return contents.ptr->reduction_schedule;
    }

    /** Get the right-hand-side of the reduction definition */
    Expr reduction_value() const {
        return contents.ptr->reduction_value;
    }

    /** Get the left-hand-side of the reduction definition */
    const std::vector<Expr> &reduction_args() const {
        return contents.ptr->reduction_args;        
    }

    /** Get the reduction domain for the reduction definition */
    ReductionDomain reduction_domain() const {
        return contents.ptr->reduction_domain;
    }

    /** Is this function a reduction? */
    bool is_reduction() const {
        return reduction_value().defined();
    }

    /** Equality of identity */
    bool same_as(const Function &other) const {
        return contents.same_as(other.contents);
    }
};

}}

#endif
