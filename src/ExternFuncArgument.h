#ifndef HALIDE_EXTERNFUNCARGUMENT_H
#define HALIDE_EXTERNFUNCARGUMENT_H

/** \file
 * Defines the internal representation of a halide ExternFuncArgument
 */

#include "Buffer.h"
#include "Expr.h"
#include "FunctionPtr.h"
#include "Parameter.h"

namespace Halide {

/** An argument to an extern-defined Func. May be a Function, Buffer,
 * ImageParam or Expr. */
struct ExternFuncArgument {
    enum ArgType { UndefinedArg = 0,
                   FuncArg,
                   BufferArg,
                   ExprArg,
                   ImageParamArg };
    ArgType arg_type = UndefinedArg;
    Internal::FunctionPtr func;
    Buffer<> buffer;
    Expr expr;
    Internal::Parameter image_param;

    explicit ExternFuncArgument(Internal::FunctionPtr f)
        : arg_type(FuncArg), func(std::move(f)) {
    }

    template<typename T, int Dims>
    explicit ExternFuncArgument(Buffer<T, Dims> b)
        : arg_type(BufferArg), buffer(b) {
    }
    explicit ExternFuncArgument(Expr e)
        : arg_type(ExprArg), expr(std::move(e)) {
    }
    explicit ExternFuncArgument(int e)
        : arg_type(ExprArg), expr(e) {
    }
    explicit ExternFuncArgument(float e)
        : arg_type(ExprArg), expr(e) {
    }

    explicit ExternFuncArgument(const Internal::Parameter &p)
        : arg_type(ImageParamArg), image_param(p) {
        // Scalar params come in via the Expr constructor.
        internal_assert(p.is_buffer());
    }
    ExternFuncArgument() = default;

    bool is_func() const {
        return arg_type == FuncArg;
    }
    bool is_expr() const {
        return arg_type == ExprArg;
    }
    bool is_buffer() const {
        return arg_type == BufferArg;
    }
    bool is_image_param() const {
        return arg_type == ImageParamArg;
    }
    bool defined() const {
        return arg_type != UndefinedArg;
    }
};

}  // namespace Halide

#endif  // HALIDE_EXTERNFUNCARGUMENT_H
