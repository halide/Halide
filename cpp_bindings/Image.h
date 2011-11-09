#ifndef FIMAGE_IMAGE_H
#define FIMAGE_IMAGE_H

#include <stdint.h>
#include "Expr.h"

namespace FImage {
    
    class Type;
  
    // The base image type with no typed accessors
    class DynImage {
    public:

        DynImage(const Type &t, uint32_t a);
        DynImage(const Type &t, uint32_t a, uint32_t b);
        DynImage(const Type &t, uint32_t a, uint32_t b, uint32_t c);
        DynImage(const Type &t, uint32_t a, uint32_t b, uint32_t c, uint32_t d);

        Expr operator()(const Expr &a) const;
        Expr operator()(const Expr &a, const Expr &b) const;
        Expr operator()(const Expr &a, const Expr &b, const Expr &c) const;
        Expr operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) const;

        const Type &type() const;
        uint32_t stride(int i) const;
        uint32_t size(int i) const;
        int dimensions() const;
        unsigned char *data() const;
        const std::string &name() const;
        
        // Compare for identity (not equality of contents)
        bool operator==(const DynImage &other) const {
            return other.contents == contents;
        }

    private:
        struct Contents;
        std::shared_ptr<Contents> contents;
    };


    // The (typed) image type
    template<typename T>
    class Image {
    private:
        DynImage im;
    public:
        Image(int a) : im(TypeOf<T>(), a) {}
        Image(int a, int b) : im(TypeOf<T>(), a, b) {}
        Image(int a, int b, int c) : im(TypeOf<T>(), a, b, c) {}
        Image(int a, int b, int c, int d) : im(TypeOf<T>(), a, b, c, d) {}
        Image(DynImage im) : im(im) {
            assert(TypeOf<T>() == im.type());
        }

        operator DynImage() const {
            return im;
        }

        // Construct a load expression
        Expr operator()(const Expr &a) const {
            return im(a);
        }

        Expr operator()(const Expr &a, const Expr &b) const {
            return im(a, b);
        }

        Expr operator()(const Expr &a, const Expr &b, const Expr &c) const {
            return im(a, b, c);
        }

        Expr operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) const {
            return im(a, b, c, d);
        }

        // Actually look something up in the image. Won't return anything
        // interesting if the image hasn't been evaluated yet.
        T &operator()(int a) {
            return ((T*)im.data())[a*im.stride(0)];
        }
        
        T &operator()(int a, int b) {
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1)];
        }
        
        T &operator()(int a, int b, int c) {
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1) + c*im.stride(2)];
        }
        
        T &operator()(int a, int b, int c, int d) {
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1) + c*im.stride(2) + d*im.stride(3)];
        }

    };

    class ImageRef {
    public:
        ImageRef(const DynImage &im, const Expr &idx) : image(im), idx(idx) {}
        const DynImage image;
        const Expr idx;
    };       

}

#endif
