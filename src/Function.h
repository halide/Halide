#ifndef HALIDE_FUNCTION_H
#define HALIDE_FUNCTION_H

/** \file
 * Defines the internal representation of a halide function and related classes
 */
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Definition.h"
#include "Expr.h"
#include "FunctionPtr.h"
#include "Schedule.h"

namespace Halide {

struct ExternFuncArgument;
class Tuple;

class Var;

/** An enum to specify calling convention for extern stages. */
enum class NameMangling {
    Default,    ///< Match whatever is specified in the Target
    C,          ///< No name mangling
    CPlusPlus,  ///< C++ name mangling
};

namespace Internal {

struct Call;
class Parameter;

/** A reference-counted handle to Halide's internal representation of
 * a function. Similar to a front-end Func object, but with no
 * syntactic sugar to help with definitions. */
class Function {
    FunctionPtr contents;

public:
    /** This lets you use a Function as a key in a map of the form
     * map<Function, Foo, Function::Compare> */
    struct Compare {
        bool operator()(const Function &a, const Function &b) const {
            internal_assert(a.contents.defined() && b.contents.defined());
            return a.contents < b.contents;
        }
    };

    /** Construct a new function with no definitions and no name. This
     * constructor only exists so that you can make vectors of
     * functions, etc.
     */
    Function() = default;

    /** Construct a new function with the given name */
    explicit Function(const std::string &n);

    /** Construct a new function with the given name,
     * with a requirement that it can only represent Expr(s) of the given type(s),
     * and must have exactly the give nnumber of dimensions.
     * required_types.empty() means there are no constraints on the type(s).
     * required_dims == AnyDims means there are no constraints on the dimensions. */
    explicit Function(const std::vector<Type> &required_types, int required_dims, const std::string &n);

    /** Construct a Function from an existing FunctionContents pointer. Must be non-null */
    explicit Function(const FunctionPtr &);

    /** Update a function with deserialized data */
    void update_with_deserialization(const std::string &name,
                                     const std::string &origin_name,
                                     const std::vector<Halide::Type> &output_types,
                                     const std::vector<Halide::Type> &required_types,
                                     int required_dims,
                                     const std::vector<std::string> &args,
                                     const FuncSchedule &func_schedule,
                                     const Definition &init_def,
                                     const std::vector<Definition> &updates,
                                     const std::string &debug_file,
                                     const std::vector<Parameter> &output_buffers,
                                     const std::vector<ExternFuncArgument> &extern_arguments,
                                     const std::string &extern_function_name,
                                     NameMangling name_mangling,
                                     DeviceAPI device_api,
                                     const Expr &extern_proxy_expr,
                                     bool trace_loads,
                                     bool trace_stores,
                                     bool trace_realizations,
                                     const std::vector<std::string> &trace_tags,
                                     bool frozen);

    /** Get a handle on the halide function contents that this Function
     * represents. */
    FunctionPtr get_contents() const {
        return contents;
    }

    /** Deep copy this Function into 'copy'. It recursively deep copies all called
     * functions, schedules, update definitions, extern func arguments, specializations,
     * and reduction domains. This method does not deep-copy the Parameter objects.
     * This method also takes a map of <old Function, deep-copied version> as input
     * and would use the deep-copied Function from the map if exists instead of
     * creating a new deep-copy to avoid creating deep-copies of the same Function
     * multiple times. If 'name' is specified, copy's name will be set to that.
     */
    // @{
    void deep_copy(const FunctionPtr &copy, std::map<FunctionPtr, FunctionPtr> &copied_map) const;
    void deep_copy(std::string name, const FunctionPtr &copy,
                   std::map<FunctionPtr, FunctionPtr> &copied_map) const;
    // @}

    /** Add a pure definition to this function. It may not already
     * have a definition. All the free variables in 'value' must
     * appear in the args list. 'value' must not depend on any
     * reduction domain */
    void define(const std::vector<std::string> &args, std::vector<Expr> values);

    /** Add an update definition to this function. It must already
     * have a pure definition but not an update definition, and the
     * length of args must match the length of args used in the pure
     * definition. 'value' must depend on some reduction domain, and
     * may contain variables from that domain as well as pure
     * variables. Any pure variables must also appear as Variables in
     * the args array, and they must have the same name as the pure
     * definition's argument in the same index. */
    void define_update(const std::vector<Expr> &args, std::vector<Expr> values);

    /** Accept a visitor to visit all of the definitions and arguments
     * of this function. */
    void accept(IRVisitor *visitor) const;

    /** Accept a mutator to mutator all of the definitions and
     * arguments of this function. */
    void mutate(IRMutator *mutator);

    /** Get the name of the function. */
    const std::string &name() const;

    /** If this is a wrapper of another func, created by a chain of in
     * or clone_in calls, returns the name of the original
     * Func. Otherwise returns the name. */
    const std::string &origin_name() const;

    /** Get a mutable handle to the init definition. */
    Definition &definition();

    /** Get the init definition. */
    const Definition &definition() const;

    /** Get the pure arguments. */
    const std::vector<std::string> &args() const;

    /** Get the dimensionality. */
    int dimensions() const;

    /** Get the number of outputs. */
    int outputs() const;

    /** Get the types of the outputs. */
    const std::vector<Type> &output_types() const;

    /** Get the type constaints on the outputs (if any). */
    const std::vector<Type> &required_types() const;

    /** Get the dimensionality constaints on the outputs (if any). */
    int required_dimensions() const;

    /** Get the right-hand-side of the pure definition. Returns an
     * empty vector if there is no pure definition.
     *
     * Warning: Any Vars in the Exprs are not qualified with the Func name, so
     * the Exprs may contain names which collide with names provided by
     * unique_name.
     */
    const std::vector<Expr> &values() const;

    /** Does this function have a pure definition? */
    bool has_pure_definition() const;

    /** Does this function *only* have a pure definition? */
    bool is_pure() const {
        return (has_pure_definition() &&
                !has_update_definition() &&
                !has_extern_definition());
    }

    /** Is it legal to inline this function? */
    bool can_be_inlined() const;

    /** Get a handle to the function-specific schedule for the purpose
     * of modifying it. */
    FuncSchedule &schedule();

    /** Get a const handle to the function-specific schedule for inspecting it. */
    const FuncSchedule &schedule() const;

    /** Get a handle on the output buffer used for setting constraints
     * on it. */
    const std::vector<Parameter> &output_buffers() const;

    /** Get a mutable handle to the stage-specfic schedule for the update
     * stage. */
    StageSchedule &update_schedule(int idx = 0);

    /** Get a mutable handle to this function's update definition at
     * index 'idx'. */
    Definition &update(int idx = 0);

    /** Get a const reference to this function's update definition at
     * index 'idx'. */
    const Definition &update(int idx = 0) const;

    /** Get a const reference to this function's update definitions. */
    const std::vector<Definition> &updates() const;

    /** Does this function have an update definition? */
    bool has_update_definition() const;

    /** Check if the function has an extern definition. */
    bool has_extern_definition() const;

    /** Get the name mangling specified for the extern definition. */
    NameMangling extern_definition_name_mangling() const;

    /** Make a call node to the extern definition. An error if the
     * function has no extern definition. */
    Expr make_call_to_extern_definition(const std::vector<Expr> &args,
                                        const Target &t) const;

    /** Get the proxy Expr for the extern stage. This is an expression
     * known to have the same data access pattern as the extern
     * stage. It must touch at least all of the memory that the extern
     * stage does, though it is permissible for it to be conservative
     * and touch a superset. For most Functions, including those with
     * extern definitions, this will be an undefined Expr. */
    // @{
    Expr extern_definition_proxy_expr() const;
    Expr &extern_definition_proxy_expr();
    // @}

    /** Add an external definition of this Func. */
    void define_extern(const std::string &function_name,
                       const std::vector<ExternFuncArgument> &args,
                       const std::vector<Type> &types,
                       const std::vector<Var> &dims,
                       NameMangling mangling, DeviceAPI device_api);

    /** Retrive the arguments of the extern definition. */
    // @{
    const std::vector<ExternFuncArgument> &extern_arguments() const;
    std::vector<ExternFuncArgument> &extern_arguments();
    // @}

    /** Get the name of the extern function called for an extern
     * definition. */
    const std::string &extern_function_name() const;

    /** Get the DeviceAPI declared for an extern function. */
    DeviceAPI extern_function_device_api() const;

    /** Test for equality of identity. */
    bool same_as(const Function &other) const {
        return contents.same_as(other.contents);
    }

    /** Get a const handle to the debug filename. */
    const std::string &debug_file() const;

    /** Get a handle to the debug filename. */
    std::string &debug_file();

    /** Use an an extern argument to another function. */
    operator ExternFuncArgument() const;

    /** Tracing calls and accessors, passed down from the Func
     * equivalents. */
    // @{
    void trace_loads();
    void trace_stores();
    void trace_realizations();
    void add_trace_tag(const std::string &trace_tag);
    bool is_tracing_loads() const;
    bool is_tracing_stores() const;
    bool is_tracing_realizations() const;
    const std::vector<std::string> &get_trace_tags() const;
    // @}

    /** Replace this Function's LoopLevels with locked copies that
     * cannot be mutated further. */
    void lock_loop_levels();

    /** Mark function as frozen, which means it cannot accept new
     * definitions. */
    void freeze();

    /** Check if a function has been frozen. If so, it is an error to
     * add new definitions. */
    bool frozen() const;

    /** Make a new Function with the same lifetime as this one, and
     * return a strong reference to it. Useful to create Functions which
     * have circular references to this one - e.g. the wrappers
     * produced by Func::in. */
    Function new_function_in_same_group(const std::string &);

    /** Mark calls of this function by 'f' to be replaced with its wrapper
     * during the lowering stage. If the string 'f' is empty, it means replace
     * all calls to this function by all other functions (excluding itself) in
     * the pipeline with the wrapper. This will also freeze 'wrapper' to prevent
     * user from updating the values of the Function it wraps via the wrapper.
     * See \ref Func::in for more details. */
    // @{
    void add_wrapper(const std::string &f, Function &wrapper);
    const std::map<std::string, FunctionPtr> &wrappers() const;
    // @}

    /** Check if a Function is a trivial wrapper around another
     * Function, Buffer, or Parameter. Returns the Call node if it
     * is. Otherwise returns null.
     */
    const Call *is_wrapper() const;

    /** Replace every call to Functions in 'substitutions' keys by all Exprs
     * referenced in this Function to call to their substitute Functions (i.e.
     * the corresponding values in 'substitutions' map). */
    // @{
    Function &substitute_calls(const std::map<FunctionPtr, FunctionPtr> &substitutions);
    Function &substitute_calls(const Function &orig, const Function &substitute);
    // @}

    /** Return true iff the name matches one of the Function's pure args. */
    bool is_pure_arg(const std::string &name) const;

    /** If the Function has type requirements, check that the given argument
     * is compatible with them. If not, assert-fail. (If there are no type requirements, do nothing.) */
    void check_types(const Expr &e) const;
    void check_types(const Tuple &t) const;
    void check_types(const Type &t) const;
    void check_types(const std::vector<Expr> &exprs) const;
    void check_types(const std::vector<Type> &types) const;

    /** If the Function has dimension requirements, check that the given argument
     * is compatible with them. If not, assert-fail. (If there are no dimension requirements, do nothing.) */
    void check_dims(int dims) const;

    /** Define the output buffers. If the Function has types specified, this can be called at
     * any time. If not, it can only be called for a Function with a pure definition. */
    void create_output_buffers(const std::vector<Type> &types, int dims) const;
};

/** Deep copy an entire Function DAG. */
std::pair<std::vector<Function>, std::map<std::string, Function>> deep_copy(
    const std::vector<Function> &outputs,
    const std::map<std::string, Function> &env);

extern std::atomic<int> random_variable_counter;

}  // namespace Internal
}  // namespace Halide

#endif
