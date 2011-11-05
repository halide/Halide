#include "Image.h"
#include "Type.h"
#include "Util.h"

namespace FImage {
    struct DynImage::Contents {
        Contents(const Type &t, uint32_t a);
        Contents(const Type &t, uint32_t a, uint32_t b);
        Contents(const Type &t, uint32_t a, uint32_t b, uint32_t c);
        Contents(const Type &t, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
        
        void allocate(size_t bytes);
        
        Type type;
        std::vector<uint32_t> size, stride;
        const std::string name;
        unsigned char *data;
        std::vector<unsigned char> buffer;
    };

    DynImage::Contents::Contents(const Type &t, uint32_t a) : 
        type(t), size{a}, stride{1}, name(uniqueName('i')) {
        allocate(a * (t.bits/8));
    }
    
    DynImage::Contents::Contents(const Type &t, uint32_t a, uint32_t b) : 
        type(t), size{a, b}, stride{1, a}, name(uniqueName('i')) {
        allocate(a * b * (t.bits/8));
    }
    
    DynImage::Contents::Contents(const Type &t, uint32_t a, uint32_t b, uint32_t c) : 
        type(t), size{a, b, c}, stride{1, a, a*b}, name(uniqueName('i')) {
        allocate(a * b * c * (t.bits/8));
    }

    DynImage::Contents::Contents(const Type &t, uint32_t a, uint32_t b, uint32_t c, uint32_t d) : 
        type(t), size{a, b, c, d}, stride{1, a, a*b, a*b*c}, name(uniqueName('i')) {
        allocate(a * b * c * d * (t.bits/8));
    }

    void DynImage::Contents::allocate(size_t bytes) {
        buffer.resize(bytes+16);
        data = &(buffer[0]);
        unsigned char offset = ((size_t)data) & 0xf;
        if (offset) {
            data += 16 - offset;
        }
    }
    
    DynImage::DynImage(const Type &t, uint32_t a) : contents(new Contents(t, a)) {}

    DynImage::DynImage(const Type &t, uint32_t a, uint32_t b) : contents(new Contents(t, a, b)) {}

    DynImage::DynImage(const Type &t, uint32_t a, uint32_t b, uint32_t c) : contents(new Contents(t, a, b, c)) {}

    DynImage::DynImage(const Type &t, uint32_t a, uint32_t b, uint32_t c, uint32_t d) : contents(new Contents(t, a, b, c, d)) {}

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

    Expr DynImage::operator()(const Expr &a) const {
        return ImageRef(*this, a*stride(0));
    }

    Expr DynImage::operator()(const Expr &a, const Expr &b) const {
        return ImageRef(*this, a*stride(0) + b*stride(1));
    }
    
    Expr DynImage::operator()(const Expr &a, const Expr &b, const Expr &c) const {
        return ImageRef(*this, a*stride(0) + b*stride(1) + c*stride(2));
    }
    
    Expr DynImage::operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) const {
        return ImageRef(*this, a*stride(0) + b*stride(1) + c*stride(2) + d*stride(3));
    }

}
