#ifndef HALIDE_MODULE_H
#define HALIDE_MODULE_H

/** \file
 *
 * Defines Module, an IR container that fully describes a Halide program.
 */

#include "IR.h"
#include "Buffer.h"
#include "ModulusRemainder.h"
#include "Target.h"

namespace Halide {

namespace Internal {

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
        Internal, ///< Not visible externally, similar to 'static' linkage in C.
    };

    /** The linkage of this function. */
    LinkageType linkage;

    LoweredFunc(const std::string &name, const std::vector<LoweredArgument> &args, Stmt body, LinkageType linkage)
        : name(name), args(args), body(body), linkage(linkage) {}
    LoweredFunc(const std::string &name, const std::vector<Argument> &args, Stmt body, LinkageType linkage)
        : name(name), body(body), linkage(linkage) {
        for (const Argument &i : args) {
            this->args.push_back(i);
        }
    }
};

}

/** A halide module. This represents IR containing lowered function
 * definitions and buffers. */
class Module {
    std::string name_;
    Target target_;

public:
    EXPORT Module(const std::string &name, const Target &target) : name_(name), target_(target) {}

    /** Get the target this module has been lowered for. */
    EXPORT const Target &target() const { return target_; }

    /** The name of this module. This is used as the default filename
     * for output operations. */
    EXPORT const std::string &name() const { return name_; }

    /** The declarations contained in this module. */
    // @{
    std::vector<Buffer> buffers;
    std::vector<Internal::LoweredFunc> functions;
    // @}

    /** Add a declaration to this module. */
    // @{
    void append(const Buffer &buffer) {
        buffers.push_back(buffer);
    }
    void append(const Internal::LoweredFunc &function) {
        functions.push_back(function);
    }
    // @}
};

/** Link a set of modules together into one module. */
EXPORT Module link_modules(const std::string &name, const std::vector<Module> &modules);

}

#endif
