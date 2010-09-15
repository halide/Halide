#ifndef FIMAGE_H
#define FIMAGE_H

#include "IRNode.h"


class FImage;

// A node in an expression tree.
class Expr {
public:
    Expr(IRNode::Ptr);
    Expr(int);
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
    Range(int a, int b);
    int min, max;
};

// An assignable reference to a pixel (e.g. im(x, y, c), not im(sin(x), y, c))
class LVal : public Expr {
public:
    LVal(FImage *, Range, Range, Range);
    void operator=(Expr);
    void debug();

    FImage *im;
    Range x, y, c;
};

// The lazily evaluated image type
class FImage {
public:
    FImage(int width, int height, int channels);

    // Make an assignable reference to a location in the image (e.g. im(x, y, c))
    LVal operator()(Range, Range, Range);

    // Make a more general unassignable reference (e.g. im(x*13-y, floor(y/x), c+x+y))
    Expr operator()(Expr, Expr, Expr);

    // Actually look something up in the image. Won't return anything
    // interesting if the image hasn't been evaluated yet.
    float &FImage::operator()(int x, int y, int c) {
        return data[(y*width+x)*channels + c];
    }
    
    // Evaluate the image and return a reference to it. In the future
    // this may return a vanilla image type instead.
    FImage &evaluate();

    // Dimensions
    int width, height, channels;

    // The point of the start of the first scanline. Public for now.
    float *data;

    // The vector of definitions of this image. Right now all but the first is ignored.
    vector<LVal> definitions;
    
    // Print out the definitions of the image.
    void debug();
};

#endif
