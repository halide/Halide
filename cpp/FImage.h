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
    Expr(void *);
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

Expr select(Expr, Expr, Expr);
Expr operator>(Expr, Expr);
Expr operator>=(Expr, Expr);
Expr operator>(Expr, Expr);
Expr operator<=(Expr, Expr);
Expr operator!=(Expr, Expr);
Expr operator==(Expr, Expr);

// external function application. Assumes Exprs are floats.
//Expr apply((float (*)(float)), Expr);


// A loop variable with the given range [min, max)
class Var : public Expr {
public:
    Var(int a = 0, int b = 0);
    
    // Evaluation of expressions in this variable should be vectorized
    // in this variable. Must be a multiple of one of the system's
    // native vectorization widths. Larger multiples than 1 are
    // converted to unrolling instead.
    void vectorize(int n) {var->vectorize = n;}
    int vectorize() {return var->vectorize;}

    // Evaluation of expressions in this variable should be unrolled
    // by this factor. If order is parallel the memory accesses of the
    // evaluations may be intermixed, otherwise load after store,
    // store after store, and store after load orderings are respected
    // between unrolled instances.
    void unroll(int n) {var->unroll = n;}
    int unroll() {return var->unroll;}

    // Evaluations of expressions in this variable should be handled
    // by multiple worker threads. The domain should be sliced up into
    // n chunks and handed off to worker threads.
    void parallelize(int n) {var->parallelize = n;}
    int parallelize() {return var->parallelize;}

    // In what order is this coordinate traversed. If also vectorized,
    // then in what order are the vectors traversed.
    void order(Order o) {var->order = o;}
    Order order() {return var->order;}

    // All definitions in this variable should share the same loop
    // across this variable. If an early definition writes to a value
    // that a later definition will read at an earlier value of this
    // loop variable, this will probably produce incorrect
    // results. fuseLoops requires that all outer variables are also
    // fused.
    void fuseLoops(bool f) {var->fuseLoops = f;}
    bool fuseLoops() {return var->fuseLoops;}
    
    // The highest values set here become the innermost loops. The
    // default value is zero. For loop variables with the same nesting
    // value, the ones with the smallest memory stride in the first
    // store location are innermost.
    void loopNesting(int n) {var->loopNesting = n;}    
    int loopNesting() {return var->loopNesting;}

private:
    NodeData<Variable> *var;
};

// An assignable reference to a memory location (e.g. im(x, y, c), or im(sin(x), y, c))
class MemRef : public Expr {
public:
    MemRef(FImage *, Expr);
    MemRef(FImage *, Expr, Expr);
    MemRef(FImage *, Expr, Expr, Expr);
    MemRef(FImage *, Expr, Expr, Expr, Expr);

    // This assignment corresponds to definition. This MemRef is
    // defined to have the given expression as its value.
    void operator=(const Expr &);

    // In these recursive definitions, the rhs version of (*this) will
    // be interpreted as a load, and the lhs version will be a store.
    void operator+=(const Expr &e) {*this = (*this + e);}
    void operator-=(const Expr &e) {*this = (*this - e);}
    void operator*=(const Expr &e) {*this = (*this * e);}
    void operator/=(const Expr &e) {*this = (*this / e);}

    // Always use the above assignment operator, don't assign an MemRef to an MemRef
    void operator=(const MemRef &other) {*this = (const Expr &)other;}



    void debug();

    FImage *im;
    vector<Expr> indices;
private:
    // Can't assign an MemRef to an MemRef - it must be cast to an expression.

};

// The lazily evaluated image type. Has from 1 to 4 dimensions.
class FImage {
public:
    FImage(uint32_t);
    FImage(uint32_t, uint32_t);
    FImage(uint32_t, uint32_t, uint32_t);
    FImage(uint32_t, uint32_t, uint32_t, uint32_t);

    ~FImage();

    // Make an assignable reference to a location in the image (e.g. im(x, y, c))
    MemRef operator()(Expr);
    MemRef operator()(Expr, Expr);
    MemRef operator()(Expr, Expr, Expr);
    MemRef operator()(Expr, Expr, Expr, Expr);
    
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
    FImage &evaluate(time_t *time = NULL);
        
    // Dimensions
    vector<uint32_t> size;
    vector<uint32_t> stride;

    // The point of the start of the first scanline. Public for now
    // for inspection, but don't assume anything about the way data is
    // stored.
    float *data;

    // How the data is actually stored
    shared_ptr<vector<float> > buffer;

    // The vector of definitions of this image. Right now all but the first is ignored.
    vector<MemRef> definitions;
    
    // Print out the definitions of the image.
    void debug();
};

#endif
