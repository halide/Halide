#ifndef FIMAGE_H
#define FIMAGE_H

#include "IRNode.h"

#ifndef _MSC_VER
unsigned long timeGetTime();
#endif //!_MSC_VER

class FImage;

// A node in an expression tree.
class Expr {
public:
    Expr();
    Expr(IRNode::Ptr);
    Expr(int64_t);
    Expr(int32_t);
    Expr(uint32_t);
    Expr(float);

    void operator+=(Expr);
    void operator-=(Expr);
    void operator*=(Expr);
    void operator/=(Expr);

    IRNode::Ptr node;
    void debug();
};

Expr operator+(Expr, Expr);
Expr operator-(Expr, Expr);
Expr operator*(Expr, Expr);
Expr operator/(Expr, Expr);

// A loop variable with the given range [min, max)
class Range : public Expr {
public:
    Range();
    Range(int a, int b);
    int min, max;
};

// An assignable reference to a pixel (e.g. im(x, y, c), not im(sin(x), y, c))
class LVal : public Expr {
public:
    LVal(FImage *, Range);
    LVal(FImage *, Range, Range);
    LVal(FImage *, Range, Range, Range);
    LVal(FImage *, Range, Range, Range, Range);
    void operator=(Expr);
    void debug();

    FImage *im;
    vector<Range> vars;
};

// The lazily evaluated image type. Has from 1 to 4 dimensions.
class FImage {
public:
    FImage(uint32_t);
    FImage(uint32_t, uint32_t);
    FImage(uint32_t, uint32_t, uint32_t);
    FImage(uint32_t, uint32_t, uint32_t, uint32_t);

    // Make an assignable reference to a location in the image (e.g. im(x, y, c))
    LVal operator()(Range);
    LVal operator()(Range, Range);
    LVal operator()(Range, Range, Range);
    LVal operator()(Range, Range, Range, Range);

    // Make a more general unassignable reference (e.g. im(x*13-y, floor(y/x), c+x+y))
    Expr operator()(Expr);
    Expr operator()(Expr, Expr);
    Expr operator()(Expr, Expr, Expr);
    Expr operator()(Expr, Expr, Expr, Expr);

    // Actually look something up in the image. Won't return anything
    // interesting if the image hasn't been evaluated yet.
    float &operator()(int a) {
        return data[a*stride[0]];
    }

    float &operator()(int a, int b) {
        return data[a*stride[0] + b*stride[1]];
    }

    float &operator()(int a, int b, int c) {
        return data[a*stride[0] + b*stride[1] + c*stride[2]];
    }

    float &operator()(int a, int b, int c, int d) {
        return data[a*stride[0] + b*stride[1] + c*stride[2] + d*stride[3]];
    }

    float operator()(int a) const {
        return data[a*stride[0]];
    }

    float operator()(int a, int b) const {
        return data[a*stride[0] + b*stride[1]];
    }

    float operator()(int a, int b, int c) const {
        return data[a*stride[0] + b*stride[1] + c*stride[2]];
    }

    float operator()(int a, int b, int c, int d) const {
        return data[a*stride[0] + b*stride[1] + c*stride[2] + d*stride[3]];
    }
    
    // Evaluate the image and return a reference to it. In the future
    // this may return a vanilla image type instead.
    FImage &evaluate();
        
    // Dimensions
    vector<uint32_t> size;
    vector<uint32_t> stride;

    // The point of the start of the first scanline. Public for now
    // for inspection, but don't assume anything about the way data is
    // stored.
    float *data;

    // The vector of definitions of this image. Right now all but the first is ignored.
    vector<LVal> definitions;
    
    // Print out the definitions of the image.
    void debug();
};

#endif
