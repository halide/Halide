#ifndef HALIDE_EXPR_H
#define HALIDE_EXPR_H

#include <memory>
#include <string>
#include <vector>
#include <tuple>

#include "MLVal.h"
#include "Type.h"

namespace Halide {

    class Var;
    class RVar;
    class FuncRef;
    class DynUniform;
    class ImageRef;
    class Type;
    class DynImage;
    class Func;
    class UniformImage;
    class UniformImageRef;
    
    template<typename T>
    class Uniform;
    
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
        void child(const RVar &);
        void child(const Func &);

        const MLVal &node() const;
        const Type &type() const;
        const std::vector<DynUniform> &uniforms() const;
        const std::vector<DynImage> &images() const;
        const std::vector<Var> &vars() const;
        const std::vector<RVar> &rvars() const;
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
        std::shared_ptr<Contents> contents;
    };

    // Force two exprs to have compatible types
    std::tuple<Expr, Expr> matchTypes(Expr a, Expr b);

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

    // Transcendentals
    Expr sqrt(Expr);
    Expr sin(Expr);
    Expr cos(Expr);
    Expr pow(Expr, Expr);
    Expr exp(Expr);
    Expr log(Expr);
    Expr floor(Expr);

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

}

#endif
