#ifndef HALIDE_MODULE_H
#define HALIDE_MODULE_H

/** \file
 *
 * Defines Func - the front-end handle on a halide function, and related classes.
 */

#include "IR.h"
#include "Target.h"

namespace Halide {

/** A halide module. This represents a collection of top level Decl
 * objects describing Func and Buffer objects. */
class Module {
    Target target;

public:
    Module(const Target &target) : target(target) {}

    const Target &get_target() const { return target; }

    Internal::Stmt body;

    void append(Internal::Stmt decl) {
        if (body.defined()) {
            body = Internal::Block::make(body, decl);
        } else {
            body = decl;
        }
    }
};

}

#endif
