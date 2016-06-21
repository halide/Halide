#ifndef HALIDE_IMAGE_H
#define HALIDE_IMAGE_H

/** \file
 * Defines Halide's Image data type
 */

#include "Var.h"
#include "Tuple.h"
#include "Target.h"

namespace Halide {

/** A base class for Images, which are typed accessors on
 * Buffers. This exists to make the implementations of certain methods
 * of Image private, so that they can safely throw errors without the
 * risk of being inlined (which in turns messes up reporting of line
 * numbers). */
class ImageBase {
protected:
    /** The underlying memory object */
    Buffer buffer;

    /** The address of the zero coordinate. The halide_buffer_t stores the
     * address of the min coordinate, but it's easier to index off the
     * zero coordinate. */
    void *origin;

    /** The first four strides. These fields are also stored in the
     * buffer, but they're cached here in the handle to make
     * operator() fast for low-dimensional images. This is safe to do
     * because the buffer is never modified.
     */
    int stride[4];

    /** The dimensionality. */
    int dims;

    /** The size of each element. */
    int elem_size;

    /** Prepare the buffer to be used as an image. Makes sure that the
     * cached strides are correct, and that the image data is on the
     * host. */
    void prepare_for_direct_pixel_access();

    bool add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                          Expr last_arg,
                                          int total_args,
                                          bool placeholder_seen) const;
public:
    /** Construct an undefined image handle */
    ImageBase() : origin(nullptr), stride{0, 0, 0, 0}, dims(0) {}

    /** Allocate an image with the given dimensions. */
    EXPORT ImageBase(Type t, const std::vector<int> &size, const std::string &name = "");

    /** Wrap a buffer in an Image object, so that we can directly
     * access its pixels in a type-safe way. */
    EXPORT ImageBase(Type t, const Buffer &buf);

    /** Wrap a single-element realization in an Image object. */
    EXPORT ImageBase(Type t, const Realization &r);

    /** Wrap a halide_buffer_t in an Image object, so that we can access its
     * pixels. */
    EXPORT ImageBase(Type t, const halide_buffer_t *b, const std::string &name = "");

    /** Get the name of this image. */
    EXPORT const std::string &name() const;

    /** Manually copy-back data to the host, if it's on a device. This
     * is done for you if you construct an image from a buffer, but
     * you might need to call this if you realize a gpu kernel into an
     * existing image */
    EXPORT void copy_to_host();

    /** Mark the buffer as dirty-on-host.  is done for you if you
     * construct an image from a buffer, but you might need to call
     * this if you realize a gpu kernel into an existing image, or
     * modify the data via some other back-door. */
    EXPORT void set_host_dirty(bool dirty = true);

    /** Check if this image handle points to actual data */
    EXPORT bool defined() const;

    /** Get the dimensionality of the data. Typically two for grayscale images, and three for color images. */
    EXPORT int dimensions() const;

    /** Get information about a single dimension of this image. */
    EXPORT Buffer::Dimension dim(int i) const;

    /** Set the coordinates of the top left of the image. */
    EXPORT void set_min(const std::vector<int> &m);

    template<typename ...Args>
    void set_min(int first, Args... rest) {
        std::vector<int> min = {first, rest...};
        set_min(min);
    }

    /** Get the extent of dimension 0, which by convention we use as
     * the width of the image. Unlike extent(0), returns one if the
     * buffer is zero-dimensional. */
    EXPORT int width() const;

    /** Get the extent of dimension 1, which by convention we use as
     * the height of the image. Unlike extent(1), returns one if the
     * buffer has fewer than two dimensions. */
    EXPORT int height() const;

    /** Get the extent of dimension 2, which by convention we use as
     * the number of color channels (often 3). Unlike extent(2),
     * returns one if the buffer has fewer than three dimensions. */
    EXPORT int channels() const;

    /** Get the minimum coordinate in dimension 0, which by convention
     * is the coordinate of the left edge of the image. Returns zero
     * for zero-dimensional images. */
    EXPORT int left() const;

    /** Get the maximum coordinate in dimension 0, which by convention
     * is the coordinate of the right edge of the image. Returns zero
     * for zero-dimensional images. */
    EXPORT int right() const;

    /** Get the minimum coordinate in dimension 1, which by convention
     * is the top of the image. Returns zero for zero- or
     * one-dimensional images. */
    EXPORT int top() const;

    /** Get the maximum coordinate in dimension 1, which by convention
     * is the bottom of the image. Returns zero for zero- or
     * one-dimensional images. */
    EXPORT int bottom() const;

    /** Construct an expression which loads from this image. If the
     * Var _ is used, the location is extended with enough implicit
     * variables to match the dimensionality of the image (see \ref
     * Var::implicit) */
    // @{
    EXPORT Expr operator()(std::vector<Expr>) const;
    EXPORT Expr operator()(std::vector<Var>) const;

    template <typename ...Args>
    NO_INLINE typename std::enable_if<Internal::all_are_convertible<Expr, Args...>::value, Expr>::type
    operator()(Args... args) const {
        std::vector<Expr> exprs = {Expr(args)...};
        return (*this)(exprs);
    };
    // @}

    /** Get a pointer to the raw halide_buffer_t that this image holds */
    EXPORT halide_buffer_t *raw_buffer() const;

    /** Get the address of a particular pixel in up to four dimensions. */
    void *address_of(int x, int y = 0, int z = 0, int w = 0) const {
        uint8_t *ptr = (uint8_t *)origin;
        ptrdiff_t offset = ((ptrdiff_t(x))*stride[0] +
                            (ptrdiff_t(y))*stride[1] +
                            (ptrdiff_t(z))*stride[2] +
                            (ptrdiff_t(w))*stride[3]);
        return (void *)(ptr + offset * elem_size);
    }

    /** Get the address of a pixel in more than four dimensions. */
    template<typename ...Args>
    typename std::enable_if<Internal::all_are_convertible<int, Args...>::value, void *>::type
    address_of(int x, int y, int z, int w, Args... args) const {
        uint8_t *ptr = (uint8_t *)origin;
        ptrdiff_t offset = ((ptrdiff_t(x))*stride[0] +
                            (ptrdiff_t(y))*stride[1] +
                            (ptrdiff_t(z))*stride[2] +
                            (ptrdiff_t(w))*stride[3]);

        // We can't rely on the cached strides for the extra dimensions.
        const int extra[] = {args...};
        for (size_t i = 0; i < sizeof...(Args); i++) {
            offset += ptrdiff_t(extra[i]) * buffer.dim(i+4).stride();
        }
        return (void *)(ptr + offset * elem_size);
    }

    /** Get the address of a pixel using a vector of integers. */
    void *address_of(const std::vector<int> &vec) const {
        uint8_t *ptr = (uint8_t *)origin;
        ptrdiff_t offset = 0;
        for (size_t i = 0; i < vec.size(); i++) {
            offset += ptrdiff_t(vec[i]) * buffer.dim(i).stride();
        }
        return (void *)(ptr + offset * elem_size);
    }
};

/** A reference-counted handle on a dense multidimensional array
 * containing scalar values of type T. Can be directly accessed and
 * modified. May have up to four dimensions. Color images are
 * represented as three-dimensional, with the third dimension being
 * the color channel. In general we store color images in
 * color-planes, as opposed to packed RGB, because this tends to
 * vectorize more cleanly. */
template<typename T>
class Image : public ImageBase {
public:
    typedef T ElemType;

    /** Construct an undefined image handle */
    Image() : ImageBase() {}

    /** Allocate an image with the given dimensions. */
    // @{
    NO_INLINE Image(int x, int y, int z, int w, const std::string &name = "") :
        ImageBase(type_of<T>(), {x, y, z, w}, name) {}

    NO_INLINE Image(int x, int y, int z, const std::string &name = "") :
        ImageBase(type_of<T>(), {x, y, z}, name) {}

    NO_INLINE Image(int x, int y, const std::string &name = "") :
        ImageBase(type_of<T>(), {x, y}, name) {}

    NO_INLINE Image(int x, const std::string &name = "") :
        ImageBase(type_of<T>(), {x}, name) {}

    NO_INLINE Image(std::vector<int> size, const std::string &name = "") :
        ImageBase(type_of<T>(), size, name) {}
    // @}

    /** Wrap a buffer in an Image object, so that we can directly
     * access its pixels in a type-safe way. */
    NO_INLINE Image(const Buffer &buf) : ImageBase(type_of<T>(), buf) {}

    /** Wrap a single-element realization in an Image object. */
    NO_INLINE Image(const Realization &r) : ImageBase(type_of<T>(), r) {}

    /** Wrap a halide_buffer_t in an Image object, so that we can access its
     * pixels. */
    NO_INLINE Image(const halide_buffer_t *b, const std::string &name = "") :
        ImageBase(type_of<T>(), b, name) {}

    /** Get a pointer to the element at the min location. */
    NO_INLINE T *data() const {
        user_assert(defined()) << "data of undefined Image\n";
        return (T *)buffer.host_ptr();
    }

    using ImageBase::operator();

    /** Get the value of the element at the given position. */
    template<typename ...Args>
    typename std::enable_if<Internal::all_are_convertible<int, Args...>::value, const T &>::type
    operator()(int first, Args... rest) const {
        return *((T *)(address_of(first, rest...)));
    }

    /** Get a reference to the element at the given position. */
    template<typename ...Args>
    typename std::enable_if<Internal::all_are_convertible<int, Args...>::value, T &>::type
    operator()(int first, Args... rest) {
        return *((T *)(address_of(first, rest...)));
    }

    /** Get the value of the element at the given position. */
    const T &operator()(std::vector<int> pos) const {
        return *((T *)(address_of(pos)));
    }

    /** Get a reference to the element at the given position. */
    T &operator()(std::vector<int> pos) {
        return *((T *)(address_of(pos)));
    }

    /** Get a handle on the Buffer that this image holds */
    operator Buffer() const {
        return buffer;
    }

    /** Convert this image to an argument to a halide pipeline. */
    operator Argument() const {
        return Argument(buffer);
    }

    /** Convert this image to an argument to an extern stage. */
    operator ExternFuncArgument() const {
        return ExternFuncArgument(buffer);
    }

    /** Treating the image as an Expr is equivalent to call it with no
     * arguments. For example, you can say:
     *
     \code
     Image im(10, 10);
     Func f;
     f = im*2;
     \endcode
     *
     * This will define f as a two-dimensional function with value at
     * position (x, y) equal to twice the value of the image at the
     * same location.
     */
    operator Expr() const {
        return (*this)(_);
    }


};

}

#endif
