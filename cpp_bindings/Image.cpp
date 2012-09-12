#include "Image.h"
#include "Type.h"
#include "Util.h"
#include "Uniform.h"
#include "Var.h"
#include <assert.h>
#include "../src/buffer.h"

namespace Halide {
    struct DynImage::Contents {
        Contents(const Type &t, int a);
        Contents(const Type &t, int a, int b);
        Contents(const Type &t, int a, int b, int c);
        Contents(const Type &t, int a, int b, int c, int d);
        Contents(const Type &t, std::vector<int> sizes);
        ~Contents();
        
        void allocate(size_t bytes);
        
        Type type;
        std::vector<int> size, stride;
        const std::string name;
        unsigned char *data;
        std::vector<unsigned char> host_buffer;
        buffer_t buf;
        mutable void (*copyToHost)(buffer_t*);
        mutable void (*freeBuffer)(buffer_t*);
    };

    DynImage::Contents::Contents(const Type &t, int a) : 
        type(t), size{a}, stride{1}, name(uniqueName('i')), copyToHost(NULL), freeBuffer(NULL) {
        assert(a > 0 && "Images must have positive sizes\n");
        allocate(a * (t.bits/8));
    }
    
    DynImage::Contents::Contents(const Type &t, int a, int b) : 
        type(t), size{a, b}, stride{1, a}, name(uniqueName('i')), copyToHost(NULL), freeBuffer(NULL) {
        assert(a > 0 && b > 0 && "Images must have positive sizes");
        allocate(a * b * (t.bits/8));
    }
    
    DynImage::Contents::Contents(const Type &t, int a, int b, int c) : 
        type(t), size{a, b, c}, stride{1, a, a*b}, name(uniqueName('i')), copyToHost(NULL), freeBuffer(NULL) {
        assert(a > 0 && b > 0 && c > 0 && "Images must have positive sizes");
        allocate(a * b * c * (t.bits/8));
    }

    DynImage::Contents::Contents(const Type &t, int a, int b, int c, int d) : 
        type(t), size{a, b, c, d}, stride{1, a, a*b, a*b*c}, name(uniqueName('i')), copyToHost(NULL), freeBuffer(NULL) {
        assert(a > 0 && b > 0 && c > 0 && d > 0 && "Images must have positive sizes");
        allocate(a * b * c * d * (t.bits/8));
    }

    DynImage::Contents::Contents(const Type &t, std::vector<int> sizes) :
        type(t), size(sizes), stride(sizes.size()), name(uniqueName('i')), copyToHost(NULL), freeBuffer(NULL) {
        
        size_t total = 1;
        for (size_t i = 0; i < sizes.size(); i++) {
            assert(sizes[i] > 0 && "Images must have positive sizes");
            stride[i] = total;
            total *= sizes[i];
        }

        allocate(total * (t.bits/8));
    }
    
    DynImage::Contents::~Contents() {
        if (freeBuffer) {
            fprintf(stderr, "freeBuffer %p\n", &buf);
            freeBuffer(&buf);
        }
    }

    void DynImage::Contents::allocate(size_t bytes) {
        host_buffer.resize(bytes+32);
        data = &(host_buffer[0]);
        unsigned char offset = ((size_t)data) & 0x1f;
        if (offset) {
            data += 32 - offset;
        }
        
        assert(size.size() <= 4);
        buf.host = data;
        buf.dev = 0;
        buf.host_dirty = false;
        buf.dev_dirty = false;
        buf.dims[0] = buf.dims[1] = buf.dims[2] = buf.dims[3] = 1;
        for (size_t i = 0; i < size.size(); i++) {
            buf.dims[i] = size[i];
        }
        buf.elem_size = type.bits/8;
    }

    DynImage::DynImage(const Type &t, int a) : contents(new Contents(t, a)) {}

    DynImage::DynImage(const Type &t, int a, int b) : contents(new Contents(t, a, b)) {}

    DynImage::DynImage(const Type &t, int a, int b, int c) : contents(new Contents(t, a, b, c)) {}

    DynImage::DynImage(const Type &t, int a, int b, int c, int d) : contents(new Contents(t, a, b, c, d)) {}
    DynImage::DynImage(const Type &t, std::vector<int> sizes) : contents(new Contents(t, sizes)) {}

    DynImage::DynImage(const DynImage &other) : contents(other.contents) {}

    const Type &DynImage::type() const {
        return contents->type;
    }

    int DynImage::stride(int i) const {
        if (i >= dimensions()) {
            fprintf(stderr,
                    "ERROR: accessing stride of dim %d of %d-dimensional image %s\n",
                    i, dimensions(), name().c_str());
            assert(i < dimensions());
        }
        return contents->stride[i];
    }

    int DynImage::size(int i) const {
        if (i >= dimensions()) {
            fprintf(stderr,
                    "ERROR: accessing size of dim %d of %d-dimensional image %s\n",
                    i, dimensions(), name().c_str());
            assert(i < dimensions());
        }
        return contents->size[i];
    }

    int DynImage::dimensions() const {
        return contents->size.size();
    }

    unsigned char *DynImage::data() const {
        return contents->data;
    }

    const std::string &DynImage::name() const {
        return contents->name;
    }
    
    buffer_t* DynImage::buffer() const {
        return &contents->buf;
    }
    
    void DynImage::setRuntimeHooks(void (*copyToHostFn)(buffer_t *), void (*freeFn)(buffer_t *)) const {
        contents->copyToHost = copyToHostFn;
        contents->freeBuffer = freeFn;
    }

    void DynImage::copyToHost() const {
        // printf("%p->copyToHost...", this);
        if (contents->buf.dev_dirty) {
            // printf("runs\n");
            assert(contents->copyToHost);
            contents->copyToHost(&contents->buf);
        } else {
            // printf("skipped - not dirty\n");
        }
    }

    void DynImage::copyToDev() const {
        if (contents->buf.host_dirty) {
            // TODO
            assert(false);
        }
    }

    void DynImage::markHostDirty() const {
        assert(!contents->buf.dev_dirty);
        contents->buf.host_dirty = true;
    }

    void DynImage::markDevDirty() const {
        assert(!contents->buf.host_dirty);
        contents->buf.dev_dirty = true;
    }
    
    bool DynImage::hostDirty() const {
        return contents->buf.host_dirty;
    }
    bool DynImage::devDirty() const {
        return contents->buf.dev_dirty;
    }

    Expr DynImage::operator()(const Expr &a) const {
        return ImageRef(*this, {a});
    }

    Expr DynImage::operator()(const Expr &a, const Expr &b) const {
        return ImageRef(*this, {a, b});
    }
    
    Expr DynImage::operator()(const Expr &a, const Expr &b, const Expr &c) const {
        return ImageRef(*this, {a, b, c});
    }
    
    Expr DynImage::operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) const {
        return ImageRef(*this, {a, b, c, d});
    }

    struct UniformImage::Contents {
        Contents(const Type &t, int dims) :
            t(t), name(uniqueName('m')) {
            sizes.resize(dims);
            for (int i = 0; i < dims; i++) {
                sizes[i] = Var(std::string(".") + name + ".dim." + int_to_str(i), false);
            }
        }

        Contents(const Type &t, int dims, const std::string &name_) :
            t(t), name(sanitizeName(name_)) {
            sizes.resize(dims);
            for (int i = 0; i < dims; i++) {
                sizes[i] = Var(std::string(".") + name + ".dim." + int_to_str(i), false);
            }
        }

        Type t;
        std::unique_ptr<DynImage> image;
        std::vector<Expr> sizes;
        const std::string name;
    };

    UniformImage::UniformImage(const Type &t, int dims) : 
        contents(new Contents(t, dims)) {
        for (int i = 0; i < dims; i++) {
            contents->sizes[i].child(*this);
        }
    }

    UniformImage::UniformImage(const Type &t, int dims, const std::string &name) : 
        contents(new Contents(t, dims, sanitizeName(name))) {
        for (int i = 0; i < dims; i++) {
            contents->sizes[i].child(*this);
        }
    }

    UniformImage::UniformImage(const UniformImage &other) :
        contents(other.contents) {
    }

    void UniformImage::operator=(const DynImage &image) {
        assert(image.type() == contents->t);
        assert((size_t)image.dimensions() == contents->sizes.size());
        contents->image.reset(new DynImage(image));
    }

    const DynImage &UniformImage::boundImage() const {
        assert(contents->image);
        return *(contents->image);
    }
         
    unsigned char *UniformImage::data() const {
        assert(contents->image);
        return contents->image->data();
    }

    bool UniformImage::operator==(const UniformImage &other) const {
        return contents == other.contents;
    }

    Expr UniformImage::operator()(const Expr &a) const {
      return UniformImageRef(*this, {a});
    }

    Expr UniformImage::operator()(const Expr &a, const Expr &b) const {
      return UniformImageRef(*this, {a, b});
    }

    Expr UniformImage::operator()(const Expr &a, const Expr &b, const Expr &c) const {
        return UniformImageRef(*this, {a, b, c});
    }

    Expr UniformImage::operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) const {
        return UniformImageRef(*this, {a, b, c, d});
    }
    
    Type UniformImage::type() const {
        return contents->t;
    }

    const std::string &UniformImage::name() const {
        return contents->name;
    }
    
    int UniformImage::dimensions() const {
        return contents->sizes.size();
    }

    const Expr &UniformImage::size(int i) const {
        return contents->sizes[i];
    }
        
}
