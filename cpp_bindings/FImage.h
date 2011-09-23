#ifndef FIMAGE_H
#define FIMAGE_H

#include "MLVal.h"
#include <vector>
#include <stdint.h>
#include <string>

namespace FImage {

    class Image;
    class Var;
    class Func;

    // A node in an expression tree.
    class Expr {
      public:
        Expr();
        Expr(MLVal);
        Expr(int32_t);
        Expr(unsigned);
        Expr(float);

        void operator+=(const Expr &);
        void operator-=(const Expr &);
        void operator*=(const Expr &);
        void operator/=(const Expr &);
        
        MLVal node;
        void debug();

        // The list of argument buffers contained within subexpressions
        std::vector<Image *> args;

        // The list of free variables found
        std::vector<Var *> vars;

        // declare that this node has a child for bookkeeping
        void child(const Expr &c);
    };

    Expr operator+(const Expr &, const Expr &);
    Expr operator-(const Expr &, const Expr &);
    Expr operator*(const Expr &, const Expr &);
    Expr operator/(const Expr &, const Expr &);
    
    Expr select(const Expr &, const Expr &, const Expr &);
    Expr operator>(const Expr &, const Expr &);
    Expr operator>=(const Expr &, const Expr &);
    Expr operator>(const Expr &, const Expr &);
    Expr operator<=(const Expr &, const Expr &);
    Expr operator!=(const Expr &, const Expr &);
    Expr operator==(const Expr &, const Expr &);

    // A loop variable with the given (static) range [min, max)
    class Var : public Expr {
      public:
        Var(int a = 0, int b = 0);
    
        // Evaluation of expressions in this variable should be vectorized
        // in this variable. Must be a multiple of one of the system's
        // native vectorization widths. Larger multiples than 1 are
        // converted to unrolling instead.
        void vectorize(int n) {_vectorize = n;}
        int vectorize() {return _vectorize;}

        void unroll(int n) {_unroll = n;}
        int unroll() {return _unroll;}

        int min() {return _min;}
        int max() {return _max;}
        
        const char *name() {return _name;}
      private:
        int _vectorize, _unroll;
        int _min, _max;
        char _name[16];
        static int _instances;
    };

    // An assignable reference to a memory location (e.g. im(x, y, c), or im(sin(x), y, c))
    class MemRef : public Expr {
      public:
        MemRef(Image *, const Expr &);
        MemRef(Image *, const Expr &, const Expr &);
        MemRef(Image *, const Expr &, const Expr &, const Expr &);
        MemRef(Image *, const Expr &, const Expr &, const Expr &, const Expr &);

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

        Image *im;
        std::vector<Expr> indices;
        Expr addr;

        // If this is a store, it can be executed
        mutable void (*function_ptr)(void *); 
    };

    class Func {
    public:
      // Define a function. Can only be a gather over two variables for now
      Func(const std::string &name, Var arg1, Var arg2, const Expr &body);

      // Generate a call to the function
      Expr operator()(const Expr &, const Expr &);

      static MLVal *environment;

    protected:
      std::string name;
      MLVal definition;
      Expr body;
    };

    // The lazily evaluated image type. Has from 1 to 4 dimensions.
    class Image {
      public:
        Image(uint32_t);
        Image(uint32_t, uint32_t);
        Image(uint32_t, uint32_t, uint32_t);
        Image(uint32_t, uint32_t, uint32_t, uint32_t);
        
        ~Image();
        
        // Make an assignable reference to a location in the image (e.g. im(x, y, c))
        MemRef operator()(const Expr &);
        MemRef operator()(const Expr &, const Expr &);
        MemRef operator()(const Expr &, const Expr &, const Expr &);
        MemRef operator()(const Expr &, const Expr &, const Expr &, const Expr &);
        
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
        
        uint32_t operator()(int a) const {
            return data[a*stride[0]];
        }
        
        uint32_t operator()(int a, int b) const {
            return data[a*stride[0] + b*stride[1]];
        }
        
        uint32_t operator()(int a, int b, int c) const {
            return data[a*stride[0] + b*stride[1] + c*stride[2]];
        }
        
        uint32_t operator()(int a, int b, int c, int d) const {
            return data[a*stride[0] + b*stride[1] + c*stride[2] + d*stride[3]];
        }
        
        // Evaluate the image and return a reference to it. In the future
        // this may return a vanilla image type instead.
        Image &evaluate();
        
        // Dimensions
        std::vector<uint32_t> size;
        std::vector<uint32_t> stride;
        
        // The point of the start of the first scanline. Public for now
        // for inspection, but don't assume anything about the way data is
        // stored.
        float *data;
        
        // How the data is actually stored
        std::shared_ptr<std::vector<float> > buffer;
        
        // The vector of definitions of this image. Right now all but the first is ignored.
        std::vector<MemRef> definitions;
        
        // Print out the definitions of the image.
        void debug();

        // A unique id
        const char *name() {
            return _name;
        }
        
      private:
        static int _instances;
        char _name[16];
    };

}


#endif
