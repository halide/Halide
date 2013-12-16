#ifndef HALIDE_IMAGE_H
#define HALIDE_IMAGE_H

/** \file
 * Defines Halide's Image data type
 */

#include "Buffer.h"
#include "Tuple.h"

namespace Halide {

/** A reference-counted handle on a dense multidimensional array
 * containing scalar values of type T. Can be directly accessed and
 * modified. May have up to four dimensions. Color images are
 * represented as three-dimensional, with the third dimension being
 * the color channel. In general we store color images in
 * color-planes, as opposed to packed RGB, because this tends to
 * vectorize more cleanly. */
template<typename T>
class Image {
private:
    /** The underlying memory object */
    Buffer buffer;

    /** These fields are also stored in the buffer, but they're cached
     * here in the handle to make operator() fast. This is safe to do
     * because the buffer is never modified
     */
    // @{
    T *origin;
    int stride_0, stride_1, stride_2, stride_3, dims;
    // @}

    /** Prepare the buffer to be used as an image. Makes sure that the
     * cached strides are correct, and that the image data is on the
     * host. */
    void prepare_for_direct_pixel_access() {
        // Make sure buffer has been copied to host. This is a no-op
        // if there's no device involved.
        buffer.copy_to_host();

        // We're probably about to modify the pixels, so to be
        // conservative we'd better set host dirty. If you're sure
        // you're not going to modify this memory via the Image
        // object, then you can call set_host_dirty(false) on the
        // underlying buffer.
        buffer.set_host_dirty(true);

        if (buffer.defined()) {
            origin = (T *)buffer.host_ptr();
            stride_0 = buffer.stride(0);
            stride_1 = buffer.stride(1);
            stride_2 = buffer.stride(2);
            stride_3 = buffer.stride(3);
            // The host pointer points to the mins vec, but we want to
            // point to the origin of the coordinate system.
            origin -= (buffer.min(0) * stride_0 +
                       buffer.min(1) * stride_1 +
                       buffer.min(2) * stride_2 +
                       buffer.min(3) * stride_3);
            dims = buffer.dimensions();
        } else {
            origin = NULL;
            stride_0 = stride_1 = stride_2 = stride_3 = 0;
            dims = 0;
        }
    }

    bool add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                          Expr last_arg,
                                          int total_args,
                                          bool placeholder_seen) const {
        const Internal::Variable *var = last_arg.as<Internal::Variable>();
        bool is_placeholder = var != NULL && Var::is_placeholder(var->name);
        if (is_placeholder) {
            assert(!placeholder_seen && "Only one placeholder ('_') allowed in argument list for Image.");
            placeholder_seen = true;

            // The + 1 in the conditional is because one provided argument is an placeholder
            for (int i = 0; i < (dims - total_args + 1); i++) {
                args.push_back(Var::implicit(i));
            }
        } else {
            args.push_back(last_arg);
        }

#if HALIDE_WARNINGS_FOR_OLD_IMPLICITS
        if (!is_placeholder && !placeholder_seen &&
            (int)args.size() == total_args &&
            (int)args.size() < dims) {
            std::cout << "Implicit arguments without placeholders ('_') are deprecated. "
                      << "Adding " << dims - args.size()
                      << " arguments to Image " << buffer.name() << '\n';
            int i = 0;
            while ((int)args.size() < dims) {
                args.push_back(Var::implicit(i++));

            }
        }
#endif

        return is_placeholder;
    }

public:
    /** Construct an undefined image handle */
    Image() : origin(NULL), stride_0(0), stride_1(0), stride_2(0), stride_3(0), dims(0) {}

    /** Allocate an image with the given dimensions. */
    // @{
    Image(int x, int y = 0, int z = 0, int w = 0, const std::string &name = "") :
        buffer(Buffer(type_of<T>(), x, y, z, w, NULL, name)) {
        prepare_for_direct_pixel_access();
    }

    Image(int x, int y, int z, const std::string &name) :
        buffer(Buffer(type_of<T>(), x, y, z, 0, NULL, name)) {
        prepare_for_direct_pixel_access();
    }

    Image(int x, int y, const std::string &name) :
        buffer(Buffer(type_of<T>(), x, y, 0, 0, NULL, name)) {
        prepare_for_direct_pixel_access();
    }

    Image(int x, const std::string &name) :
        buffer(Buffer(type_of<T>(), x, 0, 0, 0, NULL, name)) {
        prepare_for_direct_pixel_access();
    }
    // @}

    /** Wrap a buffer in an Image object, so that we can directly
     * access its pixels in a type-safe way. */
    Image(const Buffer &buf) : buffer(buf) {
        if (type_of<T>() != buffer.type()) {
            std::cerr << "Can't construct Image of type " << type_of<T>()
                      << " from buffer of type " << buffer.type() << '\n';
            assert(false);
        }
        prepare_for_direct_pixel_access();
    }

    /** Wrap a single-element realization in an Image object. */
    Image(const Realization &r) : buffer(r) {
        if (type_of<T>() != buffer.type()) {
            std::cerr << "Can't construct Image of type " << type_of<T>()
                      << " from buffer of type " << buffer.type() << '\n';
            assert(false);
        }
        prepare_for_direct_pixel_access();
    }

    /** Wrap a buffer_t in an Image object, so that we can access its
     * pixels. */
    Image(const buffer_t *b, const std::string &name = "") : buffer(type_of<T>(), b, name) {
        prepare_for_direct_pixel_access();
    }

    /** Manually copy-back data to the host, if it's on a device. This
     * is done for you if you construct an image from a buffer, but
     * you might need to call this if you realize a gpu kernel into an
     * existing image */
    void copy_to_host() {
        buffer.copy_to_host();
    }

    /** Mark the buffer as dirty-on-host.  is done for you if you
     * construct an image from a buffer, but you might need to call
     * this if you realize a gpu kernel into an existing image, or
     * modify the data via some other back-door. */
    void set_host_dirty(bool dirty = true) {
        buffer.set_host_dirty(dirty);
    }

    /** Check if this image handle points to actual data */
    bool defined() const {
        return buffer.defined();
    }

    /** Get the dimensionality of the data. Typically two for grayscale images, and three for color images. */
    int dimensions() const {
        return dims;
    }

    /** Get the size of a dimension */
    int extent(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < dims && "dimension out of bounds in call to Image::extent");
        return buffer.extent(dim);
    }

    /** Set the size of the image. */
    void set_extent(int e0, int e1 = 0, int e2 = 0, int e3 = 0) {
        assert(defined());
        buffer.set_extent(e0, e1, e2, e3);
    }

    /** Get the min coordinate of a dimension. The top left of the
     * image represents this point in a function that was realized
     * into this image. */
    int min(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < dims && "dimension out of bounds in call to Image::min");
        return buffer.min(dim);
    }

    /** Set the min coordinates of a dimension. */
    void set_min(int m0, int m1 = 0, int m2 = 0, int m3 = 0) {
        assert(defined());
        buffer.set_min(m0, m1, m2, m3);
        // Move the origin
        prepare_for_direct_pixel_access();
    }

    /** Get the number of elements in the buffer between two adjacent
     * elements in the given dimension. For example, the stride in
     * dimension 0 is usually 1, and the stride in dimension 1 is
     * usually the extent of dimension 0. This is not necessarily true
     * though. */
    int stride(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < dims && "dimension out of bounds in call to Image::stride");
        return buffer.stride(dim);
    }

    /** Get the extent of dimension 0, which by convention we use as
     * the width of the image. Unlike extent(0), returns one if the
     * buffer is zero-dimensional. */
    int width() const {
        if (dimensions() < 1) return 1;
        return extent(0);
    }

    /** Get the extent of dimension 1, which by convention we use as
     * the height of the image. Unlike extent(1), returns one if the
     * buffer has fewer than two dimensions. */
    int height() const {
        if (dimensions() < 2) return 1;
        return extent(1);
    }

    /** Get the extent of dimension 2, which by convention we use as
     * the number of color channels (often 3). Unlike extent(2),
     * returns one if the buffer has fewer than three dimensions. */
    int channels() const {
        if (dimensions() < 3) return 1;
        return extent(2);
    }

    /** Get a pointer to the element at the min location. */
    T *data() const {
        assert(defined());
        T *ptr = origin;
        for (int i = 0; i < dims; i++) {
            ptr += min(i) * stride(i);
        }
        return ptr;
    }

    /** Assuming this image is one-dimensional, get the value of the
     * element at position x */
    T operator()(int x) const {
        return origin[x];
    }

    /** Assuming this image is two-dimensional, get the value of the
     * element at position (x, y) */
    T operator()(int x, int y) const {
        return origin[x*stride_0 + y*stride_1];
    }

    /** Assuming this image is three-dimensional, get the value of the
     * element at position (x, y, z) */
    T operator()(int x, int y, int z) const {
        return origin[x*stride_0 + y*stride_1 + z*stride_2];
    }

    /** Assuming this image is four-dimensional, get the value of the
     * element at position (x, y, z, w) */
    T operator()(int x, int y, int z, int w) const {
        return origin[x*stride_0 + y*stride_1 + z*stride_2 + w*stride_3];
    }

    /** Assuming this image is one-dimensional, get a reference to the
     * element at position x */
    T &operator()(int x) {
        return origin[x*stride_0];
    }

    /** Assuming this image is two-dimensional, get a reference to the
     * element at position (x, y) */
    T &operator()(int x, int y) {
        return origin[x*stride_0 + y*stride_1];
    }

    /** Assuming this image is three-dimensional, get a reference to the
     * element at position (x, y, z) */
    T &operator()(int x, int y, int z) {
        return origin[x*stride_0 + y*stride_1 + z*stride_2];
    }

    /** Assuming this image is four-dimensional, get a reference to the
     * element at position (x, y, z, w) */
    T &operator()(int x, int y, int z, int w) {
        return origin[x*stride_0 + y*stride_1 + z*stride_2 + w*stride_3];
    }

    /** Construct an expression which loads from this image. The
     * location is composed of enough implicit variables to match the
     * dimensionality of the image (see \ref Var::implicit) */
    Expr operator()() const {
        assert(dims == 0);
        std::vector<Expr> args;
        return Internal::Call::make(buffer, args);
    }

    /** Construct an expression which loads from this image. The
     * location is extended with enough implicit variables to match
     * the dimensionality of the image (see \ref Var::implicit) */
    // @{
    Expr operator()(Expr x) const {
        std::vector<Expr> args;
        bool placeholder_seen = false;
        placeholder_seen |= add_implicit_args_if_placeholder(args, x, 1, placeholder_seen);

        assert(args.size() == (size_t)dims);

        ImageParam::check_arg_types(buffer.name(), &args);

        return Internal::Call::make(buffer, args);
    }

    Expr operator()(Expr x, Expr y) const {
        std::vector<Expr> args;
        bool placeholder_seen = false;
        placeholder_seen |= add_implicit_args_if_placeholder(args, x, 2, placeholder_seen);
        placeholder_seen |= add_implicit_args_if_placeholder(args, y, 2, placeholder_seen);

        assert(args.size() == (size_t)dims);

        ImageParam::check_arg_types(buffer.name(), &args);

        return Internal::Call::make(buffer, args);
    }

    Expr operator()(Expr x, Expr y, Expr z) const {
        std::vector<Expr> args;
        bool placeholder_seen = false;
        placeholder_seen |= add_implicit_args_if_placeholder(args, x, 3, placeholder_seen);
        placeholder_seen |= add_implicit_args_if_placeholder(args, y, 3, placeholder_seen);
        placeholder_seen |= add_implicit_args_if_placeholder(args, z, 3, placeholder_seen);

        assert(args.size() == (size_t)dims);

        ImageParam::check_arg_types(buffer.name(), &args);

        return Internal::Call::make(buffer, args);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) const {
        std::vector<Expr> args;
        bool placeholder_seen = false;
        placeholder_seen |= add_implicit_args_if_placeholder(args, x, 4, placeholder_seen);
        placeholder_seen |= add_implicit_args_if_placeholder(args, y, 4, placeholder_seen);
        placeholder_seen |= add_implicit_args_if_placeholder(args, z, 4, placeholder_seen);
        placeholder_seen |= add_implicit_args_if_placeholder(args, w, 4, placeholder_seen);

        assert(args.size() == (size_t)dims);

        ImageParam::check_arg_types(buffer.name(), &args);

        return Internal::Call::make(buffer, args);
    }
    // @}

    /** Get a pointer to the raw buffer_t that this image holds */
    buffer_t *raw_buffer() const {return buffer.raw_buffer();}

    /** Get a handle on the Buffer that this image holds */
    operator Buffer() const {return buffer;}

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
    operator Expr() const {return (*this)(_);}


};

}

#endif
