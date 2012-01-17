#ifndef FIMAGE_EXPR_H
#define FIMAGE_EXPR_H

#include <memory>
#include <string>
#include <vector>
#include "MLVal.h"
#include "Type.h"

namespace FImage {

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
        
        template<typename T> Expr(const Uniform<T> &u) : contents(Expr((DynUniform)u).contents) {}

        void operator+=(const Expr &);
        void operator-=(const Expr &);
        void operator*=(const Expr &);
        void operator/=(const Expr &);

        // declare that this node has a child for bookkeeping
        void child(const Expr &c);

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

        // When an expression is captured and placed inside an
        // anonymous function body, any reduction vars become regular
        // vars to the anonymous function
        void convertRVarsToVars();
        
      private:
        struct Contents;
        std::shared_ptr<Contents> contents;
    };

    // Make a binary op node
    Expr operator+(const Expr &, const Expr &);
    Expr operator-(const Expr &);
    Expr operator-(const Expr &, const Expr &);
    Expr operator*(const Expr &, const Expr &);
    Expr operator/(const Expr &, const Expr &);
    Expr operator%(const Expr &, const Expr &);

    // Make a comparison node
    Expr operator>(const Expr &, const Expr &);
    Expr operator>=(const Expr &, const Expr &);
    Expr operator<(const Expr &, const Expr &);
    Expr operator<=(const Expr &, const Expr &);
    Expr operator!=(const Expr &, const Expr &);
    Expr operator==(const Expr &, const Expr &);

    // Logical operators
    Expr operator&&(const Expr &, const Expr &);
    Expr operator||(const Expr &, const Expr &);
    Expr operator!(const Expr &);

    // Transcendentals
    Expr sqrt(const Expr &);
    Expr sin(const Expr &);
    Expr cos(const Expr &);
    Expr pow(const Expr &, const Expr &);
    Expr exp(const Expr &);
    Expr log(const Expr &);
    Expr floor(const Expr &);

    // Make a debug node
    Expr debug(Expr, const std::string &prefix, const std::vector<Expr> &args);
    Expr debug(Expr, const std::string &prefix);
    Expr debug(Expr, const std::string &prefix, Expr a);
    Expr debug(Expr, const std::string &prefix, Expr a, Expr b);
    Expr debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c);
    Expr debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d);
    Expr debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d, Expr e);

    // Make a ternary operator
    Expr select(const Expr &, const Expr &, const Expr &);

    Expr max(const Expr &, const Expr &);
    Expr min(const Expr &, const Expr &);
    Expr clamp(const Expr &, const Expr &, const Expr &);

    // Make a cast node
    Expr cast(const Type &, const Expr &);

    template<typename T>
    Expr cast(const Expr &e) {
        return cast(TypeOf<T>(), e);
    }

}

#endif
