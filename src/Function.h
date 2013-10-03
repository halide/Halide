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
struct FunctionContents;
}

/** An argument to an extern-defined Func. May be a Function, Buffer,
 * ImageParam or Expr. */
struct ExternFuncArgument {
    enum ArgType {UndefinedArg = 0, FuncArg, BufferArg, ExprArg, ImageParamArg};
    ArgType arg_type;
    Internal::IntrusivePtr<Internal::FunctionContents> func;
    Buffer buffer;
    Expr expr;
    Internal::Parameter image_param;

    ExternFuncArgument(Internal::IntrusivePtr<Internal::FunctionContents> f): arg_type(FuncArg), func(f) {}

    ExternFuncArgument(Buffer b): arg_type(BufferArg), buffer(b) {}

    ExternFuncArgument(Expr e): arg_type(ExprArg), expr(e) {}

    ExternFuncArgument(Internal::Parameter p) : arg_type(ImageParamArg), image_param(p) {
        assert(p.is_buffer());
    }
    ExternFuncArgument() : arg_type(UndefinedArg) {}

    bool is_func() const {return arg_type == FuncArg;}
    bool is_expr() const {return arg_type == ExprArg;}
    bool is_buffer() const {return arg_type == BufferArg;}
    bool is_image_param() const {return arg_type == ImageParamArg;}
    bool defined() const {return arg_type != UndefinedArg;}
};

namespace Internal {


struct FunctionContents {
    mutable RefCount ref_count;
    std::string name;
    std::vector<std::string> args;
    std::vector<Expr> values;
    std::vector<Type> output_types;
    Schedule schedule;

    std::vector<Expr> reduction_values;
    std::vector<Expr> reduction_args;
    Schedule reduction_schedule;
    ReductionDomain reduction_domain;

    std::string debug_file;

    std::vector<Parameter> output_buffers;

    std::vector<ExternFuncArgument> extern_arguments;
    std::string extern_function_name;

    bool trace_loads, trace_stores, trace_realizations;

    FunctionContents() : trace_loads(false), trace_stores(false), trace_realizations(false) {}
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

    /** Reconstruct a Function from a FunctionContents pointer. */
    Function(const IntrusivePtr<FunctionContents> &c) : contents(c) {}

    /** Add a pure definition to this function. It may not already
     * have a definition. All the free variables in 'value' must
     * appear in the args list. 'value' must not depend on any
     * reduction domain */
    void define(const std::vector<std::string> &args, std::vector<Expr> values);

    /** Add a reduction definition to this function. It must already
     * have a pure definition but not a reduction definition, and the
     * length of args must match the length of args used in the pure
     * definition. 'value' must depend on some reduction domain, and
     * may contain variables from that domain as well as pure
     * variables. Any pure variables must also appear as Variables in
     * the args array, and they must have the same name as the pure
     * definition's argument in the same index. */
    void define_reduction(const std::vector<Expr> &args, std::vector<Expr> values);

    /** Construct a new function with the given name */
    Function(const std::string &n) : contents(new FunctionContents) {
        for (size_t i = 0; i < n.size(); i++) {
            assert(n[i] != '.' && "Func names may not contain the character '.', as it is used internally by Halide as a separator");
        }
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

    /** Get the dimensionality */
    int dimensions() const {
        return args().size();
    }

    /** Get the number of outputs */
    int outputs() const {
        return output_types().size();
    }

    /** Get the types of the outputs */
    const std::vector<Type> &output_types() const {
        return contents.ptr->output_types;
    }

    /** Get the right-hand-side of the pure definition */
    const std::vector<Expr> &values() const {
        return contents.ptr->values;
    }

    /** Does this function have a pure definition */
    bool has_pure_definition() const {
        return !contents.ptr->values.empty();
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

    /** Get a handle on the output buffer used for setting constraints
     * on it. */
    const std::vector<Parameter> &output_buffers() const {
        return contents.ptr->output_buffers;
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
    const std::vector<Expr> &reduction_values() const {
        return contents.ptr->reduction_values;
    }

    /** Does this function have a reduction definition */
    bool has_reduction_definition() const {
        return !contents.ptr->reduction_values.empty();
    }

    /** Get the left-hand-side of the reduction definition */
    const std::vector<Expr> &reduction_args() const {
        return contents.ptr->reduction_args;
    }

    /** Get the reduction domain for the reduction definition */
    ReductionDomain reduction_domain() const {
        return contents.ptr->reduction_domain;
    }

    /** Check if the function has an extern definition */
    bool has_extern_definition() const {
        return !contents.ptr->extern_function_name.empty();
    }

    /** Add an external definition of this Func */
    void define_extern(const std::string &function_name,
                       const std::vector<ExternFuncArgument> &args,
                       const std::vector<Type> &types,
                       int dimensionality);

    /** Retrive the arguments of the extern definition */
    const std::vector<ExternFuncArgument> &extern_arguments() const {
        return contents.ptr->extern_arguments;
    }

    /** Get the name of the extern function called for an extern
     * definition. */
    const std::string &extern_function_name() const {
        return contents.ptr->extern_function_name;
    }

    /** Equality of identity */
    bool same_as(const Function &other) const {
        return contents.same_as(other.contents);
    }

    /** Get a const handle to the debug filename */
    const std::string &debug_file() const {
        return contents.ptr->debug_file;
    }

    /** Get a handle to the debug filename */
    std::string &debug_file() {
        return contents.ptr->debug_file;
    }

    /** Use an an extern argument to another function. */
    operator ExternFuncArgument() const {
        return ExternFuncArgument(contents);
    }

    /** Tracing calls and accessors, passed down from the Func
     * equivalents. */
    // @{
    void trace_loads() {
        contents.ptr->trace_loads = true;
    }
    void trace_stores() {
        contents.ptr->trace_stores = true;
    }
    void trace_realizations() {
        contents.ptr->trace_realizations = true;
    }
    bool is_tracing_loads() {
        return contents.ptr->trace_loads;
    }
    bool is_tracing_stores() {
        return contents.ptr->trace_stores;
    }
    bool is_tracing_realizations() {
        return contents.ptr->trace_realizations;
    }
    // @}

    /** What's the smallest amount of this Function that can be
     * produced? This is a function of the splits being done. Ignores
     * writes due to scattering done by reductions. */
    Expr min_extent_produced(const std::string &dim) const;

    /** Reductions also have a minimum granularity with which they can
     * be computed in the pure vars (because they can't hit the same
     * pure variable twice). This function retrieves that. */
    Expr min_extent_updated(const std::string &dim) const;
};

}}

#endif
