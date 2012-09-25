#ifndef HALIDE_EXPR_H
#define HALIDE_EXPR_H

#include <string>
#include <vector>

#include "MLVal.h"
#include "Type.h"
#include "Uniform.h"

namespace Halide {

    class Var;
    class RVar;
    class RDom;
    class FuncRef;
    class ImageRef;
    class Type;
    class DynImage;
    class Func;
    class UniformImage;
    class UniformImageRef;
    
    // A node in an expression tree.
    class Expr {
    public:
        Expr();
        Expr(MLVal, Type);

        Expr(int32_t);
        Expr(unsigned);
        Expr(float);
        Expr(double);
        Expr(const Var &);
        Expr(const RVar &);
        Expr(const RDom &);
        Expr(const FuncRef &);
        Expr(const DynUniform &);
        Expr(const ImageRef &);
        Expr(const UniformImageRef &);
        Expr(const Func &);
        Expr(const Expr &);
        
        template<typename T> Expr(const Uniform<T> &u) : contents(Expr((DynUniform)u).contents) {}

        void operator+=(Expr);
        void operator-=(Expr);
        void operator*=(Expr);
        void operator/=(Expr);

        // declare that this node depends on something
        void child(Expr);

        // These calls are only used to inject dependence that isn't
        // implied by the way the expression was constructed
        void child(const UniformImage &);
        void child(const DynUniform &);
        void child(const DynImage &);
        void child(const Var &);
        void child(const Func &);

        const MLVal &node() const;
        const Type &type() const;
        const std::vector<DynUniform> &uniforms() const;
        const std::vector<DynImage> &images() const;
        const std::vector<Var> &vars() const;
        void setRDom(const RDom &dom);
        const RDom &rdom() const;
        const std::vector<Func> &funcs() const;
        const std::vector<UniformImage> &uniformImages() const;
        int implicitArgs() const;
        void addImplicitArgs(int);

        bool isVar() const;
        bool isRVar() const;
        bool isDefined() const;
        bool isImmediate() const;

        // For a scalar this is an empty vector
        // For a tuple this gives the shape of the tuple
        std::vector<int> &shape() const;

        // When an expression is captured and placed inside an
        // anonymous function body, any reduction vars become regular
        // vars to the anonymous function
        void convertRVarsToVars();
        
      private:
        struct Contents;
        shared_ptr<Contents> contents;
    };

    // Make a binary op node
    Expr operator+(Expr, Expr);
    Expr operator-(Expr);
    Expr operator-(Expr, Expr);
    Expr operator*(Expr, Expr);
    Expr operator/(Expr, Expr);
    Expr operator%(Expr, Expr);

    // Make a comparison node
    Expr operator>(Expr, Expr);
    Expr operator>=(Expr, Expr);
    Expr operator<(Expr, Expr);
    Expr operator<=(Expr, Expr);
    Expr operator!=(Expr, Expr);
    Expr operator==(Expr, Expr);

    // Logical operators
    Expr operator&&(Expr, Expr);
    Expr operator||(Expr, Expr);
    Expr operator!(Expr);

    // Calls to builtin functions
    Expr builtin(Type, const std::string &name);
    Expr builtin(Type, const std::string &name, Expr);
    Expr builtin(Type, const std::string &name, Expr, Expr);
    Expr builtin(Type, const std::string &name, Expr, Expr, Expr);
    Expr builtin(Type, const std::string &name, Expr, Expr, Expr, Expr);

    // Transcendentals and other builtins
    Expr sqrt(Expr);
    Expr sin(Expr);
    Expr cos(Expr);
    Expr pow(Expr, Expr);
    Expr exp(Expr);
    Expr log(Expr);
    Expr floor(Expr);
    Expr ceil(Expr);
    Expr round(Expr);
    Expr abs(Expr);

    // Make a debug node
    Expr debug(Expr, const std::string &prefix, const std::vector<Expr> &args);
    Expr debug(Expr, const std::string &prefix);
    Expr debug(Expr, const std::string &prefix, Expr a);
    Expr debug(Expr, const std::string &prefix, Expr a, Expr b);
    Expr debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c);
    Expr debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d);
    Expr debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d, Expr e);

    // Make a ternary operator
    Expr select(Expr, Expr, Expr);

    Expr max(Expr, Expr);
    Expr min(Expr, Expr);
    Expr clamp(Expr, Expr, Expr);

    // Make a cast node
    Expr cast(Type, Expr);

    template<typename T>
    Expr cast(Expr e) {
        return cast(TypeOf<T>(), e);
    }


    // Convenience macros that lift functions that take regular types
    // into functions that take and return exprs, and call the original
    // function at runtime under the hood. See test/cpp/c_function for
    // example usage.
#define HalideExtern_0(rt, name)                        \
    Halide::Expr name() {                               \
    return Halide::builtin(TypeOf<rt>(), #name);        \
  }

#define HalideExtern_1(rt, name, t1)                                    \
    Halide::Expr name(Halide::Expr a1) {                                \
    assert(a1.type() == Halide::TypeOf<t1>() && "Type mismatch for argument 1 of " #name); \
    return Halide::builtin(Halide::TypeOf<rt>(), #name, a1);            \
  }

#define HalideExtern_2(rt, name, t1, t2)                                \
    Halide::Expr name(Halide::Expr a1, Halide::Expr a2) {               \
    assert(a1.type() == Halide::TypeOf<t1>() && "Type mismatch for argument 1 of " #name); \
    assert(a2.type() == Halide::TypeOf<t2>() && "Type mismatch for argument 2 of " #name); \
    return builtin(Halide::TypeOf<rt>(), #name, a1, a2);                \
  }

#define HalideExtern_3(rt, name, t1, t2, t3)                            \
    Halide::Expr name(Halide::Expr a1, Halide::Expr a2, Halide::Expr a3) { \
    assert(a1.type() == Halide::TypeOf<t1>() && "Type mismatch for argument 1 of " #name); \
    assert(a2.type() == Halide::TypeOf<t2>() && "Type mismatch for argument 2 of " #name); \
    assert(a3.type() == Halide::TypeOf<t3>() && "Type mismatch for argument 3 of " #name); \
    return builtin(Halide::TypeOf<rt>(), #name, a1, a2, a3);            \
  }

#define HalideExtern_4(rt, name, t1, t2, t3, t4)                        \
    Halide::Expr name(Halide::Expr a1, Halide::Expr a2, Halide::Expr a3, Halide::Expr a4) { \
    assert(a1.type() == Halide::TypeOf<t1>() && "Type mismatch for argument 1 of " #name); \
    assert(a2.type() == Halide::TypeOf<t2>() && "Type mismatch for argument 2 of " #name); \
    assert(a3.type() == Halide::TypeOf<t3>() && "Type mismatch for argument 3 of " #name); \
    assert(a4.type() == Halide::TypeOf<t4>() && "Type mismatch for argument 4 of " #name); \
    return builtin(Halide::TypeOf<rt>(), #name, a1, a2, a3, a4);        \
  }

}

#endif
