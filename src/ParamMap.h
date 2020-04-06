#ifndef HALIDE_PARAM_MAP_H
#define HALIDE_PARAM_MAP_H

/** \file
 * Defines a collection of parameters to be passed as formal arguments
 * to a JIT invocation.
 */
#include <map>
#include <memory>

#include "runtime/HalideRuntime.h"

namespace Halide {

template<typename T>
class Buffer;
template<typename T>
class Param;
class ImageParam;
struct Type;

namespace Internal {
class Parameter;
struct ParamMapContents;
}  // namespace Internal

class ParamMap {
public:
    struct ParamMapping {
        const Internal::Parameter *parameter = nullptr;
        const ImageParam *image_param = nullptr;
        halide_scalar_value_t value;  // inits to all-zero
        const Buffer<void> *buf_in_param = nullptr;
        Buffer<void> *buf_out_param = nullptr;

        template<typename T>
        ParamMapping(const Param<T> &p, const T &val)
            : parameter(&p.parameter()) {
            memcpy(&value.u, &val, sizeof(val));
        }

        ParamMapping(const ImageParam &p, const Buffer<void> &buf)
            : image_param(&p), buf_in_param(&buf) {
        }

        template<typename T>
        ParamMapping(const ImageParam &p, const Buffer<T> &buf)
            : image_param(&p), buf_in_param((const Buffer<void> *)&buf) {
        }

        ParamMapping(const ImageParam &p, Buffer<void> *buf_ptr)
            : image_param(&p), buf_out_param(buf_ptr) {
        }

        template<typename T>
        ParamMapping(const ImageParam &p, Buffer<T> *buf_ptr)
            : image_param(&p), buf_out_param((Buffer<void> *)buf_ptr) {
        }
    };

private:
    std::unique_ptr<Internal::ParamMapContents> contents;

    void set_input_buffer(const ImageParam &p, const Buffer<void> &buf_in_param);
    void set_output_buffer(const ImageParam &p, Buffer<void> *buf_out_param);
    void set_scalar(const Internal::Parameter &p, const Type &t, const halide_scalar_value_t &val);

public:
    ParamMap();
    ~ParamMap();
    ParamMap(const std::initializer_list<ParamMapping> &init);

    ParamMap(const ParamMap &) = delete;
    ParamMap &operator=(const ParamMap &) = delete;
    ParamMap(ParamMap &&) = delete;
    ParamMap &operator=(ParamMap &&) = delete;

    template<typename T>
    void set(const Param<T> &p, T val) {
        halide_scalar_value_t scalar;
        static_assert(sizeof(val) <= sizeof(scalar), "Bad scalar");
        memcpy(&scalar.u, &val, sizeof(val));
        set_scalar(p.parameter(), p.type(), scalar);
    };

    void set(const ImageParam &p, const Buffer<void> &buf);

    size_t size() const;

    /** If there is an entry in the ParamMap for this Parameter, return it.
     * Otherwise return the parameter itself. */
    // @{
    const Internal::Parameter &map(const Internal::Parameter &p, Buffer<void> *&buf_out_param) const;
    Internal::Parameter &map(Internal::Parameter &p, Buffer<void> *&buf_out_param) const;
    // @}

    /** A const ref to an empty ParamMap. Useful for default function
     * arguments, which would otherwise require a copy constructor
     * (with llvm in c++98 mode) */
    static const ParamMap &empty_map();
};

}  // namespace Halide

#endif
