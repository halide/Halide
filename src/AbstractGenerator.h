#ifndef HALIDE_ABSTRACT_GENERATOR_H_
#define HALIDE_ABSTRACT_GENERATOR_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Expr.h"
#include "Func.h"
#include "Parameter.h"
#include "Pipeline.h"
#include "Schedule.h"
#include "Target.h"
#include "Type.h"

namespace Halide {

class GeneratorContext;

namespace Internal {

enum class ArgInfoKind { Scalar,
                         Function,
                         Buffer };

enum class ArgInfoDir { Input,
                        Output };

using ExternsMap = std::map<std::string, ExternalCode>;

/**
 * AbstractGenerator is an ABC that defines the API a Generator must provide
 * to work with the existing Generator infrastructure (GenGen, RunGen,
 * Generator Stubs). The existing Generator<>-based instances all implement
 * this API, but any other code that implements this (and uses RegisterGenerator
 * to register itself) should be indistinguishable from a user perspective.
 *
 * An AbstractGenerator is meant to be "single-use"; typically lifetimes will be
 * something like:
 * - create an instance (with a specific Target)
 * - optionally set GeneratorParam values
 * - optionally re-bind inputs (if using in JIT or Stub modes)
 * - call build_pipeline()
 * - optionally call get_funcs_for_output() to get the output(s) (if using in JIT or Stub modes)
 * - discard the instance
 *
 * AbstractGenerators should be fairly cheap to instantiate! Don't try to re-use
 * one by re-setting inputs and calling build_pipeline() multiple times.
 *
 * Note that an AbstractGenerator instance is (generally) stateful in terms of the order
 * that methods should be called; calling the methods out of order may cause
 * assert-fails or other undesirable behavior. Read the method notes carefully!
 */
class AbstractGenerator {
public:
    virtual ~AbstractGenerator() = default;

    /** ArgInfo is a struct to contain name-and-type information for the inputs and outputs to
     * the Pipeline that build_pipeline() will return.
     *
     * Note that this looks rather similar to Halide::Argument, but unfortunately
     * that is not a good fit here, as it cannot represent Func inputs (only
     * Buffer and Scalar), nor can it really handle Outputs.
     *
     * TODO: name is suboptimal. Is there a better one?
     */
    struct ArgInfo {
        std::string name;
        ArgInfoDir dir = ArgInfoDir::Input;
        ArgInfoKind kind = ArgInfoKind::Scalar;
        // Note that this can have multiple entries for Tuple-valued Inputs or Outputs
        std::vector<Type> types;
        int dimensions = 0;
    };

    /** Return the name of this Generator. (This should always be the name
     * used to register it.) */
    virtual std::string get_name() = 0;

    /** Return the Target, autoscheduler flag, and MachineParams that this Generator
     * was created with. Always legal to call on any AbstractGenerator instance,
     * regardless of what other methods have been called. (All AbstractGenerator instances
     * are expected to be created with immutable values for these, which can't be
     * changed for a given instance.)
     *
     * CALL-AFTER: any
     * CALL-BEFORE: any
     */
    virtual GeneratorContext get_context() const = 0;

    /** Return a list of all the ArgInfos for this generator. The list will be in the order
     * that the input and outputs are declared (possibly interleaved).
     * Any inputs or outputs added by a configure() method will be in the list,
     * at the end, in the order added.
     * All input and output names will be unique within a given Generator instance.
     *
     * CALL-AFTER: configure()
     * CALL-BEFORE: any
     */
    virtual std::vector<ArgInfo> get_arginfos() = 0;

    /** Set the value for a specific GeneratorParam for an AbstractGenerator instance.
     *
     * Names that aren't known generator names should assert-fail.
     *
     * Values that can't be parsed for the specific GeneratorParam (e.g. passing "foo" where
     * an integer is expected) should assert-fail at some point (either immediately, or when
     * build_pipeline() is called)
     *
     * This can be called multiple times, but only prior to build_pipeline().
     *
     * CALL-AFTER: nona
     * CALL-BEFORE: build_pipeline
     */
    // @{
    virtual void set_generatorparam_value(const std::string &name, const std::string &value) = 0;
    virtual void set_generatorparam_value(const std::string &name, const LoopLevel &loop_level) = 0;
    // @}

    /** Build and return the Pipeline for this AbstractGenerator. This method should be called
     * only once per instance.
     *
     * CALL-AFTER: set_generatorparam_value, bind_input
     * CALL-BEFORE: get_parameters_for_input, get_funcs_for_output, get_external_code_map
     */
    virtual Pipeline build_pipeline() = 0;

    /** Given the name of an input, return the Parameter(s) for that input.
     * (Most inputs will have exactly one, but inputs that are declared as arrays
     * will have multiple.)
     *
     * CALL-AFTER: build_pipeline
     * CALL-BEFORE: none
     */
    virtual std::vector<Parameter> get_parameters_for_input(const std::string &name) = 0;

    /** Given the name of an output, return the Func(s) for that output.
     * (Most outputs will have exactly one, but outputs that are declared as arrays, or that return Tuples,
     * will have multiple.)
     *
     * Must be called after build_pipeline(), since the output Funcs will be undefined prior to that.
     *
     * CALL-AFTER: build_pipeline()
     * CALL-BEFORE: none
     */
    virtual std::vector<Func> get_funcs_for_output(const std::string &name) = 0;

    /** Return the ExternsMap for the Generator, if any.
     *
     * CALL-AFTER: build_pipeline()
     * CALL-BEFORE: n/a
     */
    virtual ExternsMap get_external_code_map() = 0;

    /** Rebind all the inputs in a single call. The vector of StubInputs is assumed to have
     * the same size and ordering as the list of inputs. Basic type-checking is done to ensure
     * that inputs are still sane (e.g. types, dimensions, etc must match expectations).
     *
     * This call is infrequently used, and is only useful in JIT or Stub situations;
     * this allows you to bind the inputs to be Funcs, Buffers, etc from another Halide code
     * fragment, allowing inlining of multiple fragments together.
     *
     * CALL-AFTER: n/a
     * CALL-BEFORE: build_pipeline
     */
    // @{
    virtual void bind_input(const std::string &name, const std::vector<Parameter> &v) = 0;
    virtual void bind_input(const std::string &name, const std::vector<Func> &v) = 0;
    virtual void bind_input(const std::string &name, const std::vector<Expr> &v) = 0;
    // @}

    /** Emit a Generator Stub (.stub.h) file to the given path. Not all Generators support this.
     *
     * If you call this method, you should not call any other AbstractGenerator methods
     * on this instance, before or after this call.
     *
     * If the Generator is capable of emitting a Stub, do so and return true. (Errors
     * during stub emission should assert-fail rather than returning false.)
     *
     * If the Generator is not capable of emitting a Stub, do nothing and return false.
     *
     * CALL-AFTER: none
     * CALL-BEFORE: none
     */
    virtual bool emit_cpp_stub(const std::string &stub_file_path) = 0;
};

using AbstractGeneratorPtr = std::unique_ptr<AbstractGenerator>;

}  // namespace Internal
}  // namespace Halide

#endif
