#ifndef HALIDE_MODULE_H
#define HALIDE_MODULE_H

/** \file
 *
 * Defines Module, an IR container that fully describes a Halide program.
 */

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "Argument.h"
#include "Expr.h"
#include "ExternalCode.h"
#include "Function.h"  // for NameMangling
#include "ModulusRemainder.h"

namespace Halide {

template<typename T, int Dims>
class Buffer;
struct Target;

/** Enums specifying various kinds of outputs that can be produced from a Halide Pipeline. */
enum class OutputFileType {
    assembly,
    bitcode,
    c_header,
    c_source,
    compiler_log,
    cpp_stub,
    featurization,
    llvm_assembly,
    object,
    python_extension,
    pytorch_wrapper,
    registration,
    schedule,
    static_library,
    stmt,
    stmt_html,
};

/** Type of linkage a function in a lowered Halide module can have.
    Also controls whether auxiliary functions and metadata are generated. */
enum class LinkageType {
    External,              ///< Visible externally.
    ExternalPlusMetadata,  ///< Visible externally. Argument metadata and an argv wrapper are also generated.
    ExternalPlusArgv,      ///< Visible externally. Argv wrapper is generated but *not* argument metadata.
    Internal,              ///< Not visible externally, similar to 'static' linkage in C.
};

namespace Internal {

struct OutputInfo {
    std::string name, extension;

    // `is_multi` indicates how these outputs are generated
    // when using the compile_to_multitarget_xxx() APIs (or via the
    // Generator command-line mode):
    //
    // - If `is_multi` is true, then a separate file of this Output type is
    //   generated for each target in the multitarget (e.g. object files,
    //   assembly files, etc). Each of the files will have a suffix appended
    //   that is based on the specific subtarget.
    //
    // - If `is_multi` is false, then only one file of this Output type
    //   regardless of how many targets are in the multitarget. No additional
    //   suffix will be appended to the filename.
    //
    bool is_multi{false};
};
std::map<OutputFileType, const OutputInfo> get_output_info(const Target &target);

/** Definition of an argument to a LoweredFunc. This is similar to
 * Argument, except it enables passing extra information useful to
 * some targets to LoweredFunc. */
struct LoweredArgument : public Argument {
    /** For scalar arguments, the modulus and remainder of this
     * argument. */
    ModulusRemainder alignment;

    LoweredArgument() = default;
    explicit LoweredArgument(const Argument &arg)
        : Argument(arg) {
    }
    LoweredArgument(const std::string &_name, Kind _kind, const Type &_type, uint8_t _dimensions, const ArgumentEstimates &argument_estimates)
        : Argument(_name, _kind, _type, _dimensions, argument_estimates) {
    }
};

/** Definition of a lowered function. This object provides a concrete
 * mapping between parameters used in the function body and their
 * declarations in the argument list. */
struct LoweredFunc {
    std::string name;

    /** Arguments referred to in the body of this function. */
    std::vector<LoweredArgument> args;

    /** Body of this function. */
    Stmt body;

    /** The linkage of this function. */
    LinkageType linkage;

    /** The name-mangling choice for the function. Defaults to using
     * the Target. */
    NameMangling name_mangling;

    LoweredFunc(const std::string &name,
                const std::vector<LoweredArgument> &args,
                Stmt body,
                LinkageType linkage,
                NameMangling mangling = NameMangling::Default);
    LoweredFunc(const std::string &name,
                const std::vector<Argument> &args,
                Stmt body,
                LinkageType linkage,
                NameMangling mangling = NameMangling::Default);
};

}  // namespace Internal

namespace Internal {
struct ModuleContents;
class CompilerLogger;
}  // namespace Internal

struct AutoSchedulerResults;

using MetadataNameMap = std::map<std::string, std::string>;

/** A halide module. This represents IR containing lowered function
 * definitions and buffers. */
class Module {
    Internal::IntrusivePtr<Internal::ModuleContents> contents;

public:
    Module(const std::string &name, const Target &target);

    /** Get the target this module has been lowered for. */
    const Target &target() const;

    /** The name of this module. This is used as the default filename
     * for output operations. */
    const std::string &name() const;

    /** If this Module had an auto-generated schedule, return a read-only pointer
     * to the AutoSchedulerResults. If not, return nullptr. */
    const AutoSchedulerResults *get_auto_scheduler_results() const;

    /** Return whether this module uses strict floating-point anywhere. */
    bool any_strict_float() const;

    /** The declarations contained in this module. */
    // @{
    const std::vector<Buffer<void>> &buffers() const;
    const std::vector<Internal::LoweredFunc> &functions() const;
    std::vector<Internal::LoweredFunc> &functions();
    const std::vector<Module> &submodules() const;
    const std::vector<ExternalCode> &external_code() const;
    // @}

    /** Return the function with the given name. If no such function
     * exists in this module, assert. */
    Internal::LoweredFunc get_function_by_name(const std::string &name) const;

    /** Add a declaration to this module. */
    // @{
    void append(const Buffer<void> &buffer);
    void append(const Internal::LoweredFunc &function);
    void append(const Module &module);
    void append(const ExternalCode &external_code);
    // @}

    /** Compile a halide Module to variety of outputs, depending on
     * the fields set in output_files. */
    void compile(const std::map<OutputFileType, std::string> &output_files) const;

    /** Compile a halide Module to in-memory object code. Currently
     * only supports LLVM based compilation, but should be extended to
     * handle source code backends. */
    Buffer<uint8_t> compile_to_buffer() const;

    /** Return a new module with all submodules compiled to buffers on
     * on the result Module. */
    Module resolve_submodules() const;

    /** When generating metadata from this module, remap any occurrences
     * of 'from' into 'to'. */
    void remap_metadata_name(const std::string &from, const std::string &to) const;

    /** Retrieve the metadata name map. */
    MetadataNameMap get_metadata_name_map() const;

    /** Set the AutoSchedulerResults for the Module. It is an error to call this
     * multiple times for a given Module. */
    void set_auto_scheduler_results(const AutoSchedulerResults &results);

    /** Set whether this module uses strict floating-point directives anywhere. */
    void set_any_strict_float(bool any_strict_float);
};

/** Link a set of modules together into one module. */
Module link_modules(const std::string &name, const std::vector<Module> &modules);

/** Create an object file containing the Halide runtime for a given target. For
 * use with Target::NoRuntime. Standalone runtimes are only compatible with
 * pipelines compiled by the same build of Halide used to call this function. */
void compile_standalone_runtime(const std::string &object_filename, const Target &t);

/** Create an object and/or static library file containing the Halide runtime
 * for a given target. For use with Target::NoRuntime. Standalone runtimes are
 * only compatible with pipelines compiled by the same build of Halide used to
 * call this function. Return a map with just the actual outputs filled in
 * (typically, OutputFileType::object and/or OutputFileType::static_library).
 */
std::map<OutputFileType, std::string> compile_standalone_runtime(const std::map<OutputFileType, std::string> &output_files, const Target &t);

using ModuleFactory = std::function<Module(const std::string &fn_name, const Target &target)>;
using CompilerLoggerFactory = std::function<std::unique_ptr<Internal::CompilerLogger>(const std::string &fn_name, const Target &target)>;

void compile_multitarget(const std::string &fn_name,
                         const std::map<OutputFileType, std::string> &output_files,
                         const std::vector<Target> &targets,
                         const std::vector<std::string> &suffixes,
                         const ModuleFactory &module_factory,
                         const CompilerLoggerFactory &compiler_logger_factory = nullptr);

}  // namespace Halide

#endif
