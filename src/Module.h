#ifndef HALIDE_MODULE_H
#define HALIDE_MODULE_H

/** \file
 *
 * Defines Module, an IR container that fully describes a Halide program.
 */

#include "IR.h"
#include "Buffer.h"
#include "Outputs.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Definition of a lowered function. This object provides a concrete
 * mapping between parameters used in the function body and their
 * declarations in the argument list. */
struct LoweredFunc {
    std::string name;

    /** Arguments referred to in the body of this function. */
    std::vector<Argument> args;

    /** Body of this function. */
    Stmt body;

    /** Type of linkage a function can have. */
    enum LinkageType {
        External, ///< Visible externally.
        Internal, ///< Not visible externally, similar to 'static' linkage in C.
    };

    /** The linkage of this function. */
    LinkageType linkage;

    LoweredFunc(const std::string &name, const std::vector<Argument> &args, Stmt body, LinkageType linkage)
        : name(name), args(args), body(body), linkage(linkage) {}
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
    EXPORT const std::vector<Buffer> &buffers() const;
    EXPORT const std::vector<Internal::LoweredFunc> &functions() const;
    // @}

    /** Add a declaration to this module. */
    // @{
    EXPORT void append(const Buffer &buffer);
    EXPORT void append(const Internal::LoweredFunc &function);
    // @}

    /** Compile a halide Module to variety of outputs, depending on 
     * the fields set in output_files. */
    EXPORT void compile(const Outputs &output_files) const;
};

/** Link a set of modules together into one module. */
EXPORT Module link_modules(const std::string &name, const std::vector<Module> &modules);

/** Create an object file containing the Halide runtime for a given
 * target. For use with Target::NoRuntime. */
EXPORT void compile_standalone_runtime(const std::string &object_filename, Target t);

}

#endif
