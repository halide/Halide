#ifndef HALIDE_IMAGE_PARAM_H
#define HALIDE_IMAGE_PARAM_H

/** \file
 *
 * Classes for declaring image parameters to halide pipelines
 */

#include "Var.h"
#include "OutputImageParam.h"
#include "Func.h"

namespace Halide {

/** An Image parameter to a halide pipeline. E.g., the input image. */
class ImageParam : public OutputImageParam {

    /** Func representation of the ImageParam.
     * All call to ImageParam is equivalent to call to its intrinsic Func
     * representation. */
    Func func;

    /** Helper function to initialize the Func representation of this ImageParam. */
    EXPORT void init_func();

public:

    /** Construct a nullptr image parameter handle. */
    ImageParam() : OutputImageParam() {}

    /** Construct an image parameter of the given type and
     * dimensionality, with an auto-generated unique name. */
    EXPORT ImageParam(Type t, int d);

    /** Construct an image parameter of the given type and
     * dimensionality, with the given name */
    EXPORT ImageParam(Type t, int d, const std::string &n);

    /** Bind an Image to this ImageParam. Only relevant for jitting */
    // @{
    EXPORT void set(Buffer<> im);
    // @}

    /** Get a reference to the Buffer bound to this ImageParam. Only relevant for jitting. */
    // @{
    EXPORT Buffer<> get() const;
    // @}

    /** Unbind any bound Buffer */
    EXPORT void reset();

    /** Construct an expression which loads from this image
     * parameter. The location is extended with enough implicit
     * variables to match the dimensionality of the image
     * (see \ref Var::implicit)
     */
    // @{
    template <typename... Args>
    NO_INLINE Expr operator()(Args&&... args) const {
        return func(std::forward<Args>(args)...);
    }
    EXPORT Expr operator()(std::vector<Expr>) const;
    EXPORT Expr operator()(std::vector<Var>) const;
    // @}

    /** Return the intrinsic Func representation of this ImageParam. This allows
     * an ImageParam to be implicitly converted to a Func.
     *
     * Note that we use implicit vars to name the dimensions of Funcs associated
     * with the ImageParam: both its internal Func representation and wrappers
     * (See \ref ImageParam::in). For example, to unroll the first and second
     * dimensions of the associated Func by a factor of 2, we would do the following:
     \code
     func.unroll(_0, 2).unroll(_1, 2);
     \endcode
     * '_0' represents the first dimension of the Func, while _1 represents the
     * second dimension of the Func.
     */
    EXPORT operator Func() const;


    /** Creates and returns a new Func that wraps this ImageParam. During
     * compilation, Halide will replace calls to this ImageParam with calls
     * to the wrapper as appropriate. If this ImageParam is already wrapped
     * for use in some Func, it will return the existing wrapper.
     *
     * For example, img.in(g) would rewrite a pipeline like this:
     \code
     ImageParam img(Int(32), 2);
     Func g;
     g(x, y) = ... img(x, y) ...
     \endcode
     * into a pipeline like this:
     \code
     ImageParam img(Int(32), 2);
     Func img_wrap, g;
     img_wrap(x, y) = img(x, y);
     g(x, y) = ... img_wrap(x, y) ...
     \endcode
     *
     * This has a variety of uses. One use case is to stage loads from an
     * ImageParam via some intermediate buffer (e.g. on the stack or in shared
     * GPU memory).
     *
     * The following example illustrates how you would use the 'in()' directive
     * to stage loads from an ImageParam into the GPU shared memory:
     \code
     ImageParam img(Int(32), 2);
     output(x, y) = img(y, x);
     Var tx, ty;
     output.compute_root().gpu_tile(x, y, tx, ty, 8, 8);
     img.in().compute_at(output, x).unroll(_0, 2).unroll(_1, 2).gpu_threads(x, y);
     \endcode
     *
     * Note that we use implicit vars to name the dimensions of the wrapper Func
     * (See \ref ImageParam::in for more details). See \ref Func::in for more
     * possible use cases of the 'in()' directive.
     */
    // @{
    EXPORT Func in(const Func &f);
    EXPORT Func in(const std::vector<Func> &fs);
    EXPORT Func in();
    // @}
};

}

#endif
