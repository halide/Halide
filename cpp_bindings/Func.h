#ifndef HALIDE_FUNC_H
#define HALIDE_FUNC_H

#include <memory>
#include <string>

#include "Type.h"
#include "MLVal.h"
#include "Image.h"

namespace Halide {
    
    bool use_gpu();

    class Func;
    struct FuncContents;
    class Var;

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
        FuncRef(const FuncRef &);

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
        
        shared_ptr<Contents> contents;
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

        // Generate an image from this function by Jitting the IR and running it.
        DynImage realize(int a);
        DynImage realize(int a, int b);
        DynImage realize(int a, int b, int c);
        DynImage realize(int a, int b, int c, int d);
        DynImage realize(std::vector<int> sizes);
        void realize(const DynImage &);

        /* If this function is a reduction, get a handle to its update
           step for scheduling */
        bool isReduction() const;
        Func &update() const;

        /* These methods generate scheduling gurus used for picking a
           valid schedule for this function */
        Func &tile(const Var &, const Var &,
                   const Var &, const Var &,
                   const Expr &f1, const Expr &f2);
        Func &tile(const Var &, const Var &,
                   const Var &, const Var &, 
                   const Var &, const Var &, 
                   const Expr &f1, const Expr &f2);
        Func &rename(const Var &, const Var &);
        Func &reset();
        Func &vectorize(const Var &);
        Func &unroll(const Var &);
        Func &reorder(const std::vector<Var> &);
        Func &reorder(const Var &, const Var &);
        Func &reorder(const Var &, const Var &, const Var &);
        Func &reorder(const Var &, const Var &, const Var &, const Var &);
        Func &reorder(const Var &, const Var &, const Var &, const Var &, const Var &);
        Func &chunk(const Var &);
        Func &root();
        Func &parallel(const Var &);
        Func &vectorize(const Var &, int factor);
        Func &unroll(const Var &, int factor);
        Func &split(const Var &, const Var &, const Var &, const Expr &factor);
        Func &cuda(const Var &, const Var &);
        Func &cuda(const Var &, const Var &, const Var &, const Var &);
        Func &cudaTile(const Var &, int xFactor);
        Func &cudaTile(const Var &, const Var &, int xFactor, int yFactor);


        bool operator==(const Func &other) const;

        // Various properties of the function
        const Expr &rhs() const;
        const Type &returnType() const;
        const std::vector<Expr> &args() const;
        const std::string &name() const;

        std::vector<DynUniform> uniforms() const;
        std::vector<DynImage> images() const;
        std::vector<Func> funcs() const;
        std::vector<UniformImage> uniformImages() const;

        // Get the variable defining argument i
        const Var &arg(int i) const;

        std::string serialize();

        void compileJIT();
        void compileToFile(const std::string &name, std::string target = "");

        void setErrorHandler(void (*)(char *));

        struct Arg {
            template<typename T>
            Arg(const Uniform<T> &u) : arg(Arg(DynUniform(u)).arg) {}
            template<typename T>
            Arg(const Image<T> &u) : arg(Arg(DynImage(u)).arg) {}
            Arg(const UniformImage &);
            Arg(const DynUniform &);
            Arg(const DynImage &);
            MLVal arg;
        };

        void compileToFile(const std::string &name, std::vector<Arg> args, std::string target = "");

    private:
        friend struct FuncContents;

        MLVal buildEnv();
        MLVal buildGuru();
        MLVal lower();
        MLVal inferArguments();

        shared_ptr<FuncContents> contents;
    };

}

#endif
