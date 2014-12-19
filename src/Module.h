#ifndef HALIDE_MODULE_H
#define HALIDE_MODULE_H

/** \file
 *
 * Defines Func - the front-end handle on a halide function, and related classes.
 */

#include "IR.h"
#include "Target.h"

namespace Halide {

/** A halide module. This represents IR containing declarations of
 * functions and buffers. */
class Module {
    std::string name_;
    Target target_;
    Internal::Stmt body_;

public:
    EXPORT Module(const std::string &name, const Target &target) : name_(name), target_(target) {}

    /** Get the target this module has been lowered for. */
    EXPORT const Target &target() const { return target_; }

    /** The name of this module. This is used as the default filename
     * for output operations. */
    EXPORT const std::string &name() const { return name_; }

    /** The definitions contained in this module. */
    EXPORT Internal::Stmt body() const { return body_; }

    /** Add some IR to the module. */
    EXPORT void append(Internal::Stmt stmt);
};

/** Link a set of modules together into one module. */
EXPORT Module link_modules(const std::string &name, const std::vector<Module> &modules);

}

#endif
