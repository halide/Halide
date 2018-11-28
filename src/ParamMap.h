#ifndef HALIDE_PARAM_MAP_H
#define HALIDE_PARAM_MAP_H

/** \file
 * Defines a collection of parameters to be passed as formal arguments
 * to a JIT invocation.
 */

#include "Param.h"
#include "Parameter.h"

namespace Halide {

class ImageParam;

class ParamMap {
public:
    struct ParamMapping {
        const Internal::Parameter *parameter{nullptr};
        const ImageParam *image_param{nullptr};
        halide_scalar_value_t value;
        Buffer<> buf;
        Buffer<> *buf_out_param;

        template <typename T>
        ParamMapping(const Param<T> &p, const T &val) : parameter(&p.parameter()) {
            *((T *)&value) = val;
        }

        ParamMapping(const ImageParam &p, Buffer<> &buf) : image_param(&p), buf(buf), buf_out_param(nullptr) {
        }

        template <typename T>
        ParamMapping(const ImageParam &p, Buffer<T> &buf) : image_param(&p), buf(buf), buf_out_param(nullptr) {
        }

        ParamMapping(const ImageParam &p, Buffer<> *buf_ptr) : image_param(&p), buf_out_param(buf_ptr) {
        }

        template <typename T>
        ParamMapping(const ImageParam &p, Buffer<T> *buf_ptr) : image_param(&p), buf_out_param((Buffer<> *)buf_ptr) {
        }
    };

private:
    struct ParamArg {
        Internal::Parameter mapped_param;
        Buffer<> *buf_out_param;

        ParamArg() : buf_out_param(nullptr) { }
        ParamArg(const ParamMapping &pm)
            : mapped_param(pm.parameter->type(), false, 0, pm.parameter->name()),
               buf_out_param(nullptr) {
              mapped_param.set_scalar(pm.parameter->type(), pm.value);
        }
        ParamArg(Buffer<> *buf_ptr) : buf_out_param(buf_ptr) { }
        ParamArg(const ParamArg &) = default;
    };
    mutable std::map<const Internal::Parameter, ParamArg> mapping;

    void set(const ImageParam &p, Buffer<> &buf, Buffer<> *buf_out_param);

public:
    ParamMap() { }

    ParamMap(const std::initializer_list<ParamMapping> &init);

    template <typename T> void set(const Param<T> &p, T val) {
        Internal::Parameter v(p.type(), false, 0, p.name());
        v.set_scalar<T>(val);
        ParamArg pa;
        pa.mapped_param = v;
        pa.buf_out_param = nullptr;
        mapping[p.parameter()] = pa;
    };

    void set(const ImageParam &p, Buffer<> &buf) {
        set(p, buf, nullptr);
    }

    template <typename T>
    void set(const ImageParam &p, Buffer<T> &buf) {
        Buffer<> temp = buf;
        set(p, temp, nullptr);
    }

    size_t size() const { return mapping.size(); }

    /** If there is an entry in the ParamMap for this Parameter, return it.
     * Otherwise return the parameter itself. */
    // @{
    const Internal::Parameter &map(const Internal::Parameter &p, Buffer<> *&buf_out_param) const;

    Internal::Parameter &map(Internal::Parameter &p, Buffer<> *&buf_out_param) const;
    // @}

    /** A const ref to an empty ParamMap. Useful for default function
     * arguments, which would otherwise require a copy constructor
     * (with llvm in c++98 mode) */
    static const ParamMap &empty_map() {
        static ParamMap empty_param_map;
        return empty_param_map;
    }
};

}  // namespace Halide

#endif
