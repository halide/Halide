#ifndef FIMAGE_H
#define FIMAGE_H

#include "MLVal.h"
#include <vector>
#include <stdint.h>
#include <string>
#include <sstream>

namespace FImage {

    // objects with unique auto-generated names
    template<char c>
    class Named {
    public:        
        Named() {
            std::ostringstream ss;
            ss << c;
            ss << _instances++;
            _name = ss.str();
        }

        Named(const std::string &name) : _name(name) {}

        const std::string &name() const {return _name;}

    private:
        static int _instances;
        std::string _name;
    };


    class DynImage;
    class Var;
    class Func;

    // Possible types for image data
    class Type {
      public:
        MLVal mlval;
        unsigned char bits;
    };

    Type Float(unsigned char bits);
    Type Int(unsigned char bits);
    Type UInt(unsigned char bits);

    // A node in an expression tree.
    class Expr {
    public:
        Expr();
        Expr(MLVal, Type);
        Expr(int32_t);
        Expr(unsigned);
        Expr(float);

        void operator+=(const Expr &);
        void operator-=(const Expr &);
        void operator*=(const Expr &);
        void operator/=(const Expr &);
        
        MLVal node;
        Type type;

        void debug();        

        // The list of argument buffers contained within subexpressions
        std::vector<DynImage *> bufs;

        // The list of free variables found
        std::vector<Var *> vars;

        // The list of functions directly called
        std::vector<Func *> funcs;

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

    // Make a debug node
    Expr Debug(Expr, const std::string &prefix, const std::vector<Expr> &args);
    Expr Debug(Expr, const std::string &prefix);
    Expr Debug(Expr, const std::string &prefix, Expr a);
    Expr Debug(Expr, const std::string &prefix, Expr a, Expr b);
    Expr Debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c);
    Expr Debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d);
    Expr Debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d, Expr e);

    // The base image type with no typed accessors
    class DynImage : public Named<'i'> {
    public:

        DynImage(size_t bytes, uint32_t a);
        DynImage(size_t bytes, uint32_t a, uint32_t b);
        DynImage(size_t bytes, uint32_t a, uint32_t b, uint32_t c);
        DynImage(size_t bytes, uint32_t a, uint32_t b, uint32_t c, uint32_t d);

        Expr load(Type type, const Expr &idx);

        std::vector<uint32_t> size, stride;
        unsigned char *data;

    private:
        void allocate(size_t bytes);
        std::shared_ptr<std::vector<unsigned char> >buffer;
    };

    template<typename T>
    Type TypeOf();

    template<>
    Type TypeOf<float>() {
        return Float(32);
    }

    template<>
    Type TypeOf<double>() {
        return Float(64);
    }

    template<>
    Type TypeOf<unsigned char>() {
        return UInt(8);
    }

    template<>
    Type TypeOf<unsigned short>() {
        return UInt(16);
    }

    template<>
    Type TypeOf<unsigned int>() {
        return UInt(32);
    }

    template<>
    Type TypeOf<bool>() {
        return Int(1);
    }

    template<>
    Type TypeOf<char>() {
        return Int(8);
    }

    template<>
    Type TypeOf<short>() {
        return Int(16);
    }

    template<>
    Type TypeOf<int>() {
        return Int(32);
    }

    template<>
    Type TypeOf<signed char>() {
        return Int(8);
    }

    // Make a cast node
    Expr Cast(const Type &, const Expr &);

    template<typename T>
    Expr Cast(const Expr &e) {return Cast(TypeOf<T>(), e);}


    // The (typed) image type
    template<typename T>
    class Image : public DynImage {
    public:
        Image(uint32_t a) : DynImage(a * sizeof(T), a) {}
        Image(uint32_t a, uint32_t b) : DynImage(a*b*sizeof(T), a, b) {}
        Image(uint32_t a, uint32_t b, uint32_t c) : DynImage(a*b*c*sizeof(T), a, b, c) {}
        Image(uint32_t a, uint32_t b, uint32_t c, uint32_t d) : DynImage(a*b*c*d*sizeof(T), a, b, c, d) {}
        Image(DynImage im) : DynImage(im) {}

        // make a Load
        Expr operator()(const Expr &a) {
            return load(TypeOf<T>(), a*stride[0]);
        }

        Expr operator()(const Expr &a, const Expr &b) {
            return load(TypeOf<T>(), a*stride[0] + b*stride[1]);
        }

        Expr operator()(const Expr &a, const Expr &b, const Expr &c) {
            return load(TypeOf<T>(), a*stride[0] + b*stride[1] + c*stride[2]);
        }


        Expr operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) {
            return load(TypeOf<T>(), a*stride[0] + b*stride[1] + c*stride[2] + d*stride[3]);
        }
        
        // Actually look something up in the image. Won't return anything
        // interesting if the image hasn't been evaluated yet.
        T &operator()(int a) {
            return ((T*)data)[a*stride[0]];
        }
        
        T &operator()(int a, int b) {
            return ((T*)data)[a*stride[0] + b*stride[1]];
        }
        
        T &operator()(int a, int b, int c) {
            return ((T*)data)[a*stride[0] + b*stride[1] + c*stride[2]];
        }
        
        T &operator()(int a, int b, int c, int d) {
            return ((T*)data)[a*stride[0] + b*stride[1] + c*stride[2] + d*stride[3]];
        }
    };

    // A loop variable with the given (static) range [min, max)
    class Var : public Expr, public Named<'v'> {
    public:
        Var();
        Var(const std::string &name);
    };

    class Range {
    public:
        Range() {}
        Range(Expr min, Expr size) : range {std::pair<Expr, Expr> {min, size}} {}
        std::vector<std::pair<Expr, Expr> > range;
    };

    Range operator*(const Range &a, const Range &b);

    class Func;

    // A function call (if you cast it to an expr), or a function definition lhs (if you assign an expr to it).
    class FuncRef {
    public:
        // Yay C++0x initializer lists
        FuncRef(Func *f, const Expr &a) : 
            f(f), func_args{a} {}
        FuncRef(Func *f, const Expr &a, const Expr &b) :
            f(f), func_args{a, b} {}
        FuncRef(Func *f, const Expr &a, const Expr &b, const Expr &c) :
            f(f), func_args{a, b, c} {}
        FuncRef(Func *f, const Expr &a, const Expr &b, const Expr &c, const Expr &d) : 
            f(f), func_args{a, b, c, d} {}
        FuncRef(Func *f, const std::vector<Expr> &args) :
            f(f), func_args(args) {}

        // Turn it into a function call
        operator Expr();

        // This assignment corresponds to definition. This FuncRef is
        // defined to have the given expression as its value.
        void operator=(const Expr &e);
        
        // Make sure we don't directly assign an FuncRef to an FuncRef (but instead treat it as a definition)
        void operator=(const FuncRef &other) {*this = (const Expr &)other;}
                        
        // A pointer to the function object that this lhs defines.
        Func *f;

        std::vector<Expr> func_args;

    };

    class Func : public Named<'f'> {
    public:
        Func() : function_ptr(NULL) {}
        Func(const std::string &name) : Named<'f'>(name), function_ptr(NULL) {}

        // Define a function
        void define(const std::vector<Expr> &func_args, const Expr &rhs);
        
        // Generate a call to the function (or the lhs of a definition)
        FuncRef operator()(const Expr &a) {return FuncRef(this, a);}
        FuncRef operator()(const Expr &a, const Expr &b) {return FuncRef(this, a, b);}
        FuncRef operator()(const Expr &a, const Expr &b, const Expr &c) {return FuncRef(this, a, b, c);}     
        FuncRef operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) {return FuncRef(this, a, b, c, d);}  
        FuncRef operator()(const std::vector<Expr> &args) {return FuncRef(this, args);}

        // Print every time this function gets evaluated
        void trace();
        
        // Generate an image from this function by Jitting the IR and running it.

        DynImage realize(int a);
        DynImage realize(int a, int b);
        DynImage realize(int a, int b, int c);
        DynImage realize(int a, int b, int c, int d);

        void realize(const DynImage &im);
        
        /* These methods generate a partially applied function that
         * takes a schedule and modifies it. These functions get pushed
         * onto the schedule_transforms vector, which is traversed in
         * order starting from an initial default schedule to create a
         * mutated schedule */
        void split(const Var &, const Var &, const Var &, int factor);
        void vectorize(const Var &);
        void unroll(const Var &);
        void transpose(const Var &, const Var &);
        void chunk(const Var &, const Range &);

        // Convenience methods for common transforms
        void vectorize(const Var &, int factor);


        /* The space of all living functions (TODO: remove a function
           from the environment when it goes out of scope) */
        static MLVal *environment;

        // The scalar value returned by the function
        Expr rhs;
        std::vector<Expr> args;
        MLVal arglist;

    protected:

        /* The ML definition object (name, return type, argnames, body)
           The body here evaluates the function over an entire range,
           and the arg list will include a min and max value for every
           free variable. */
        MLVal definition;

        /* A list of schedule transforms to apply when realizing. These should be
           partially applied ML functions that map a schedule to a schedule. */
        std::vector<MLVal> schedule_transforms;

        // The compiled form of this function
        mutable void (*function_ptr)(void *); 
    };


}


#endif
