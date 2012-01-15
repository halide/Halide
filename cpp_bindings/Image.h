#ifndef FIMAGE_IMAGE_H
#define FIMAGE_IMAGE_H

#include <stdint.h>
#include "Expr.h"

struct buffer_t;

namespace FImage {
    
    class Type;
  
    // The base image type with no typed accessors
    class DynImage {
    public:

        DynImage(const Type &t, uint32_t a);
        DynImage(const Type &t, uint32_t a, uint32_t b);
        DynImage(const Type &t, uint32_t a, uint32_t b, uint32_t c);
        DynImage(const Type &t, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
        DynImage(const Type &t, std::vector<uint32_t> sizes);

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
        struct buffer_t* buffer() const;
        void setRuntimeHooks(void (*copyToHostFn)(buffer_t *), void (*freeFn)(buffer_t *)) const;
        void copyToHost() const;
        void copyToDev() const;
        void markHostDirty() const;
        void markDevDirty() const;
        bool hostDirty() const;
        bool devDirty() const;
        
        // Convenience functions for typical interpretations of dimensions
        uint32_t width() const {return size(0);}
        uint32_t height() const {return size(1);}
        uint32_t channels() const {
            if (dimensions() > 2)
                return size(2);
            else
                return 1;
        }

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

        Image(std::initializer_list<T> l) : im(TypeOf<T>(), l.size()) {
            int x = 0;
            for (auto &iter: l) {
                (*this)(x++) = iter;
            }
        }

        Image(std::initializer_list<std::initializer_list<T> > l) : im(TypeOf<T>(), l.begin()->size(), l.size()) {
            int y = 0;
            for (auto &row: l) {
                int x = 0;
                for (auto &elem: row) {
                    (*this)(x++, y) = elem;
                }
                y++;
            }
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
            im.copyToHost();
            im.markHostDirty();
            return ((T*)im.data())[a*im.stride(0)];
        }
        
        T &operator()(int a, int b) {
            im.copyToHost();
            im.markHostDirty();
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1)];
        }
        
        T &operator()(int a, int b, int c) {
            im.copyToHost();
            im.markHostDirty();
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1) + c*im.stride(2)];
        }
        
        T &operator()(int a, int b, int c, int d) {
            im.copyToHost();
            im.markHostDirty();
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1) + c*im.stride(2) + d*im.stride(3)];
        }

        const T &operator()(int a) const {
            im.copyToHost();
            return ((T*)im.data())[a*im.stride(0)];
        }
        
        const T &operator()(int a, int b) const {
            im.copyToHost();
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1)];
        }
        
        const T &operator()(int a, int b, int c) const {
            im.copyToHost();
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1) + c*im.stride(2)];
        }
        
        const T &operator()(int a, int b, int c, int d) const {
            im.copyToHost();
            return ((T*)im.data())[a*im.stride(0) + b*im.stride(1) + c*im.stride(2) + d*im.stride(3)];
        }

        // Convenience functions for typical interpretations of dimensions
        uint32_t width() const {return im.width();}
        uint32_t height() const {return im.height();}
        uint32_t channels() const {return im.channels();}
        uint32_t size(int i) const {return im.size(i);}
        int dimensions() const {return im.dimensions();}
        unsigned char *data() const {return im.data();}
    };

    
    class ImageRef {
    public:
        ImageRef(const DynImage &im, const Expr &idx) : image(im), idx(idx) {}
        const DynImage image;
        const Expr idx;
    };       
    
    class UniformImage {
    public:
        UniformImage(const Type &t, int dims);
        UniformImage(const Type &t, int dims, const std::string &name);

        void operator=(const DynImage &image);
        unsigned char *data() const;

        Expr operator()(const Expr &a) const;
        Expr operator()(const Expr &a, const Expr &b) const;
        Expr operator()(const Expr &a, const Expr &b, const Expr &c) const;
        Expr operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) const;
        Type type() const;
        const std::string &name() const;
        int dimensions() const;

        const DynImage &boundImage() const;

        const Expr &size(int i) const;
        const Expr &width() const {return size(0);}
        const Expr &height() const {return size(1);}
        const Expr &channels() const {return size(2);}

        // Compare for identity (not equality of contents)
        bool operator==(const UniformImage &other) const;
        
    private:
        struct Contents;
        std::shared_ptr<Contents> contents;
    };

    class UniformImageRef {
      public:
        UniformImageRef(const UniformImage &im, const Expr &idx) : image(im), idx(idx) {}
        const UniformImage image;
        const Expr idx;
    };

}

#endif
