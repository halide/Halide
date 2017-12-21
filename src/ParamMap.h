#ifndef HALIDE_PARAM_MAP_H
#define HALIDE_PARAM_MAP_H

/** \file
 * Defines a collection of parameters to be passed as formal arugments
 * to a JIT invocation.
 */

#include "Param.h"
#include "Parameter.h"

namespace Halide {

class ImageParam;

class ParamMap {
    std::map<const Internal::Parameter, Internal::Parameter> mapping;

public:
    struct ParamMapping {
        const Internal::Parameter *parameter{nullptr};
        const ImageParam *image_param{nullptr};
        halide_scalar_value_t value;
        Buffer<> buf;

        template <typename T>
        ParamMapping(const Param<T> &p, const T &val) : parameter(&p.parameter()) {
            *((T *)&value) = val;
        }

        ParamMapping(const ImageParam &p, Buffer<> &buf) : image_param(&p), buf(buf) {
        }

        
        template <typename T>
        ParamMapping(const ImageParam &p, Buffer<T> &buf) : image_param(&p), buf(buf) {
        }
    };

    ParamMap() { }

    ParamMap(const std::initializer_list<ParamMapping> &init) {
        for (const auto &pm : init) {
            if (pm.parameter != nullptr) {
                Internal::Parameter v(pm.parameter->type(), false, 0, pm.parameter->name(), pm.parameter->is_explicit_name(), false);
                v.set_scalar(pm.parameter->type(), pm.value);
                mapping[*pm.parameter] = v;
            } else {
                // TODO: there has to be a way to do this without the const_cast.
                set(*pm.image_param, *const_cast<Buffer<> *>(&pm.buf));
            }
        }
    }

    template <typename T> void set(const Param<T> &p, T val) {
        Internal::Parameter v(p.type(), false, 0, p.name(), p.is_explicit_name(), false);
        v.set_scalar<T>(val);
        mapping[p.parameter()] = v;
    };

    EXPORT void set(const ImageParam &p, Buffer<> &buf);

    template <typename T>
    void set(const ImageParam &p, Buffer<T> &buf) {
        Buffer<> temp = buf;
        set(p, temp);
    }

    size_t size() const { return mapping.size(); }

    /** If there is an entry in the ParamMap for this Parameter, return it.
     * Otherwise return the parameter itself. */
    const Internal::Parameter &map(const Internal::Parameter &p) const {
        auto iter = mapping.find(p);
        return (iter != mapping.end()) ? iter->second : p;
    }
};

}

#endif
