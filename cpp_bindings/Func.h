#ifndef FIMAGE_FUNC_H
#define FIMAGE_FUNC_H

#include <memory>
#include <string>

#include "Type.h"
#include "MLVal.h"
#include "Image.h"

namespace FImage {

    class Func;
    class Var;

    class Range {
    public:
        Range() {}
        Range(Expr min, Expr size) : range {std::pair<Expr, Expr> {min, size}} {}
        std::vector<std::pair<Expr, Expr> > range;
    };

    Range operator*(const Range &a, const Range &b);

    // A function call (if you cast it to an expr), or a function definition lhs (if you assign an expr to it).
    class FuncRef {
    public:

        FuncRef(const Func &f);
        FuncRef(const Func &f, const Expr &a); 
        FuncRef(const Func &f, const Expr &a, const Expr &b);
        FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c);
        FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d);
        FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d, const Expr &e);
        FuncRef(const Func &f, const std::vector<Expr> &args);

        // This assignment corresponds to definition. This FuncRef is
        // defined to have the given expression as its value.
        void operator=(const Expr &e);
        
        // Syntactic sugar for some reductions        
        void operator+=(const Expr &e);
        void operator*=(const Expr &e);
        void operator++(int) {*this += 1;}
        void operator--() {*this += -1;}

        // Make sure we don't directly assign an FuncRef to an FuncRef (but instead treat it as a definition)
        void operator=(const FuncRef &other) {*this = Expr(other);}

        const Func &f() const;
        const std::vector<Expr> &args() const;
        
    private:
        struct Contents;
        
        std::shared_ptr<Contents> contents;
    };

    class Func {
    public:
        Func();
        Func(const char *name);
        Func(const char *name, Type t);
        Func(const std::string &name);
        Func(const Type &t);
        Func(const std::string &name, Type t);

        // Define a function
        void define(const std::vector<Expr> &args, const Expr &rhs);
        void operator=(const Expr &rhs) {define(std::vector<Expr>(), rhs);}
        
        // Generate a call to the function (or the lhs of a definition)
        FuncRef operator()(const Expr &a) {return FuncRef(*this, a);}
        FuncRef operator()(const Expr &a, const Expr &b) {return FuncRef(*this, a, b);}
        FuncRef operator()(const Expr &a, const Expr &b, const Expr &c) {return FuncRef(*this, a, b, c);}     
        FuncRef operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d) {return FuncRef(*this, a, b, c, d);}  
        FuncRef operator()(const Expr &a, const Expr &b, const Expr &c, const Expr &d, const Expr &e) {return FuncRef(*this, a, b, c, d, e);}  
        FuncRef operator()(const std::vector<Expr> &args) {return FuncRef(*this, args);}

        // Print every time this function gets evaluated
        void trace();
        
        // Generate an image from this function by Jitting the IR and running it.
        DynImage realize(int a);
        DynImage realize(int a, int b);
        DynImage realize(int a, int b, int c);
        DynImage realize(int a, int b, int c, int d);
        void realize(const DynImage &);

        /* These methods generate a partially applied function that
         * takes a schedule and modifies it. These functions get pushed
         * onto the scheduleTransforms vector, which is traversed in
         * order starting from an initial default schedule to create a
         * mutated schedule */
        void split(const Var &, const Var &, const Var &, int factor);
        void vectorize(const Var &);
        void unroll(const Var &);
        void transpose(const Var &, const Var &);
        void chunk(const Var &, const Range &);
        void chunk(const Var &);
        void root(const Range &);
        void root();

        /* Add an explicit Serial or Parallel to the schedule. Useful
         * for defining reduction domains */
        void range(const Var &, const Expr &min, const Expr &size, bool serial = false); 

        // Convenience methods for common transforms
        void vectorize(const Var &, int factor);
        void unroll(const Var &, int factor);

        bool operator==(const Func &other) const;

        /* The space of all living functions (TODO: remove a function
           from the environment when it goes out of scope) */
        static MLVal *environment;
        
        // Various properties of the function
        const Expr &rhs() const;
        const Type &returnType() const;
        const std::vector<Expr> &args() const;
        const std::string &name() const;
        const std::vector<MLVal> &scheduleTransforms() const;

        void compile();

    private:
        struct Contents;

        std::shared_ptr<Contents> contents;
    };

}

#endif
