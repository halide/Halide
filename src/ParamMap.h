#ifndef HALIDE_PARAM_MAP_H
#define HALIDE_PARAM_MAP_H

/** \file
 * Defines the a collection of parameters to be passed to a JIT invocation.
 */

#include "Param.h"
#include "Parameter.h"

namespace Halide {

class ImageParam;

class ParamMap {
    std::map<const Internal::Parameter, Internal::Parameter> mapping;

public:
    template <typename T> void set(const Param<T> &p, T val) {
        Internal::Parameter v(p.type(), false, 0, p.name(), p.is_explicit_name(), false);
        v.set_scalar<T>(val);
        mapping[p.parameter()] = v;
    };

    EXPORT void set(const ImageParam &p, Buffer<> &buf);

    /** If there is an entry in the ParamMap for this Parameter, return it.
     * Otherwise return the parameter itself. */
    const Internal::Parameter &map(const Internal::Parameter &p) const {
        auto iter = mapping.find(p);
        return (iter != mapping.end()) ? iter->second : p;
    }
};

}

#endif
