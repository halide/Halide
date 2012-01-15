#include "Image.h"
#include "Type.h"
#include "Util.h"
#include "Uniform.h"
#include "Var.h"
#include <assert.h>
#include "../src/buffer.h"

namespace FImage {
    struct DynImage::Contents {
        Contents(const Type &t, uint32_t a);
        Contents(const Type &t, uint32_t a, uint32_t b);
        Contents(const Type &t, uint32_t a, uint32_t b, uint32_t c);
        Contents(const Type &t, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
        Contents(const Type &t, std::vector<uint32_t> sizes);
        
        void allocate(size_t bytes);
        
        Type type;
        std::vector<uint32_t> size, stride;
        const std::string name;
        unsigned char *data;
        std::vector<unsigned char> host_buffer;
        buffer_t buf;
        mutable void (*copyToHost)(buffer_t*);
    };

    DynImage::Contents::Contents(const Type &t, uint32_t a) : 
        type(t), size{a}, stride{1}, name(uniqueName('i')), copyToHost(NULL) {
        allocate(a * (t.bits/8));
    }
    
    DynImage::Contents::Contents(const Type &t, uint32_t a, uint32_t b) : 
        type(t), size{a, b}, stride{1, a}, name(uniqueName('i')), copyToHost(NULL) {
        allocate(a * b * (t.bits/8));
    }
    
    DynImage::Contents::Contents(const Type &t, uint32_t a, uint32_t b, uint32_t c) : 
        type(t), size{a, b, c}, stride{1, a, a*b}, name(uniqueName('i')), copyToHost(NULL) {
        allocate(a * b * c * (t.bits/8));
    }

    DynImage::Contents::Contents(const Type &t, uint32_t a, uint32_t b, uint32_t c, uint32_t d) : 
        type(t), size{a, b, c, d}, stride{1, a, a*b, a*b*c}, name(uniqueName('i')), copyToHost(NULL) {
        allocate(a * b * c * d * (t.bits/8));
    }

    DynImage::Contents::Contents(const Type &t, std::vector<uint32_t> sizes) :
        type(t), size(sizes), stride(sizes.size()), name(uniqueName('i')), copyToHost(NULL) {
        
        size_t total = 1;
        for (size_t i = 0; i < sizes.size(); i++) {
            stride[i] = total;
            total *= sizes[i];
        }

        allocate(total * (t.bits/8));
    }

    void DynImage::Contents::allocate(size_t bytes) {
        host_buffer.resize(bytes+16);
        data = &(host_buffer[0]);
        unsigned char offset = ((size_t)data) & 0xf;
        if (offset) {
            data += 16 - offset;
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
    
    DynImage::DynImage(const Type &t, uint32_t a) : contents(new Contents(t, a)) {}

    DynImage::DynImage(const Type &t, uint32_t a, uint32_t b) : contents(new Contents(t, a, b)) {}

    DynImage::DynImage(const Type &t, uint32_t a, uint32_t b, uint32_t c) : contents(new Contents(t, a, b, c)) {}

    DynImage::DynImage(const Type &t, uint32_t a, uint32_t b, uint32_t c, uint32_t d) : contents(new Contents(t, a, b, c, d)) {}
    DynImage::DynImage(const Type &t, std::vector<uint32_t> sizes) : contents(new Contents(t, sizes)) {}

    const Type &DynImage::type() const {
        return contents->type;
    }

    uint32_t DynImage::stride(int i) const {
        return contents->stride[i];
    }

    uint32_t DynImage::size(int i) const {
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
    
    void DynImage::setCopyToHost(void (*func)(buffer_t *)) const {
        contents->copyToHost = func;
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

    Expr DynImage::operator()(const Expr &a) const {
        if (a.isRVar()) RVar(a.rvars()[0]).bound(0, size(0));
        return ImageRef(*this, a*stride(0));
    }

    Expr DynImage::operator()(const Expr &a, const Expr &b) const {
        if (a.isRVar()) RVar(a.rvars()[0]).bound(0, size(0));
        if (b.isRVar()) RVar(b.rvars()[0]).bound(0, size(1));
        return ImageRef(*this, a*stride(0) + b*stride(1));
    }
    
    Expr DynImage::operator()(const Expr &a, const Expr &b, const Expr &c) const {
        if (a.isRVar()) RVar(a.rvars()[0]).bound(0, size(0));
        if (b.isRVar()) RVar(b.rvars()[0]).bound(0, size(1));
        if (c.isRVar()) RVar(c.rvars()[0]).bound(0, size(2));
        return ImageRef(*this, a*stride(0) + b*stride(1) + c*stride(2));
    }
    
    Expr DynImage::operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) const {
        if (a.isRVar()) RVar(a.rvars()[0]).bound(0, size(0));
        if (b.isRVar()) RVar(b.rvars()[0]).bound(0, size(1));
        if (c.isRVar()) RVar(c.rvars()[0]).bound(0, size(2));
        if (d.isRVar()) RVar(d.rvars()[0]).bound(0, size(3));
        return ImageRef(*this, a*stride(0) + b*stride(1) + c*stride(2) + d*stride(3));
    }

    struct UniformImage::Contents {
        Contents(const Type &t, int dims) :
            t(t), name(uniqueName('m')) {
            sizes.resize(dims);
            for (int i = 0; i < dims; i++) {
                std::ostringstream ss;
                ss << "." << name << ".dim." << i;
                sizes[i] = Var(ss.str());
            }
        }

        Contents(const Type &t, int dims, const std::string &name) :
            t(t), name(name) {
            sizes.resize(dims);
            for (int i = 0; i < dims; i++) {
                std::ostringstream ss;
                ss << "." << name << ".dim." << i;
                sizes[i] = Var(ss.str());
            }
        }

        Type t;
        std::unique_ptr<DynImage> image;
        std::vector<Expr> sizes;
        const std::string name;
    };

    UniformImage::UniformImage(const Type &t, int dims) : 
        contents(new Contents(t, dims)) {
    }

    UniformImage::UniformImage(const Type &t, int dims, const std::string &name) : 
        contents(new Contents(t, dims, name)) {
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
        return UniformImageRef(*this, a);
    }

    Expr UniformImage::operator()(const Expr &a, const Expr &b) const {
        return UniformImageRef(*this, a + size(0) * b);
    }

    Expr UniformImage::operator()(const Expr &a, const Expr &b, const Expr &c) const {
        return UniformImageRef(*this, a + size(0) * (b + size(1) * c));
    }

    Expr UniformImage::operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) const {
        return UniformImageRef(*this, a + size(0) * (b + size(1) * (c + size(2) * d)));
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
