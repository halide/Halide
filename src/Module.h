#ifndef HALIDE_MODULE_H
#define HALIDE_MODULE_H

/** \file
 *
 * Defines Module, an IR container that fully describes a Halide program.
 */

#include <functional>

#include "Argument.h"
#include "ExternalCode.h"
#include "IR.h"
#include "ModulusRemainder.h"
#include "Outputs.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Definition of an argument to a LoweredFunc. This is similar to
 * Argument, except it enables passing extra information useful to
 * some targets to LoweredFunc. */
struct LoweredArgument : public Argument {
    /** For scalar arguments, the modulus and remainder of this
     * argument. */
    ModulusRemainder alignment;

    LoweredArgument() {}
    LoweredArgument(const Argument &arg) : Argument(arg) {}
    LoweredArgument(const std::string &_name, Kind _kind, const Type &_type, uint8_t _dimensions,
                    Expr _def = Expr(),
                    Expr _min = Expr(),
                    Expr _max = Expr()) : Argument(_name, _kind, _type, _dimensions, _def, _min, _max) {}
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

    /** Type of linkage a function can have. */
    enum LinkageType {
        External, ///< Visible externally.
        ExternalPlusMetadata, ///< Visible externally. Argument metadata and an argv wrapper are also generated.
        Internal, ///< Not visible externally, similar to 'static' linkage in C.
    };

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

}

namespace Internal {
struct ModuleContents;
}

/** A halide module. This represents IR containing lowered function
 * definitions and buffers. */
class Module {
    Internal::IntrusivePtr<Internal::ModuleContents> contents;

public:
    EXPORT Module(const std::string &name, const Target &target);

    /** Get the target this module has been lowered for. */
    EXPORT const Target &target() const;

    /** The name of this module. This is used as the default filename
     * for output operations. */
    EXPORT const std::string &name() const;

    /** The declarations contained in this module. */
    // @{
    EXPORT const std::vector<Buffer<>> &buffers() const;
    EXPORT const std::vector<Internal::LoweredFunc> &functions() const;
    EXPORT std::vector<Internal::LoweredFunc> &functions();
    EXPORT const std::vector<Module> &submodules() const;
    EXPORT const std::vector<ExternalCode> &external_code() const;
    // @}

    /** Return the function with the given name. If no such function
    * exists in this module, assert. */
    EXPORT Internal::LoweredFunc get_function_by_name(const std::string &name) const;

    /** Add a declaration to this module. */
    // @{
    EXPORT void append(const Buffer<> &buffer);
    EXPORT void append(const Internal::LoweredFunc &function);
    EXPORT void append(const Module &module);
    EXPORT void append(const ExternalCode &external_code);
    // @}

    /** Compile a halide Module to variety of outputs, depending on
     * the fields set in output_files. */
    EXPORT void compile(const Outputs &output_files) const;

    /** Compile a halide Module to in-memory object code. Currently
     * only supports LLVM based compilation, but should be extended to
     * handle source code backends. */
    EXPORT Buffer<uint8_t> compile_to_buffer() const;

    /** Return a new module with all submodules compiled to buffers on
     * on the result Module. */
    EXPORT Module resolve_submodules() const;
};

/** Link a set of modules together into one module. */
EXPORT Module link_modules(const std::string &name, const std::vector<Module> &modules);

/** Create an object file containing the Halide runtime for a given
 * target. For use with Target::NoRuntime. */
EXPORT void compile_standalone_runtime(const std::string &object_filename, Target t);

/** Create an object and/or static library file containing the Halide runtime for a given
 * target. For use with Target::NoRuntime. Return an Outputs with just the actual
 * outputs filled in (typically, object_name and/or static_library_name).
 */
EXPORT Outputs compile_standalone_runtime(const Outputs &output_files, Target t);

typedef std::function<Module(const std::string &, const Target &)> ModuleProducer;

EXPORT void compile_multitarget(const std::string &fn_name,
                                const Outputs &output_files,
                                const std::vector<Target> &targets,
                                ModuleProducer module_producer,
                                const std::map<std::string, std::string> &suffixes = {});

}

#endif
