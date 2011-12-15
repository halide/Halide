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

        bool isVar() const;
        bool isDefined() const;
        
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

    // Make a debug node
    Expr Debug(Expr, const std::string &prefix, const std::vector<Expr> &args);
    Expr Debug(Expr, const std::string &prefix);
    Expr Debug(Expr, const std::string &prefix, Expr a);
    Expr Debug(Expr, const std::string &prefix, Expr a, Expr b);
    Expr Debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c);
    Expr Debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d);
    Expr Debug(Expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d, Expr e);

    // Make a ternary operator
    Expr Select(const Expr &, const Expr &, const Expr &);

    Expr Max(const Expr &, const Expr &);
    Expr Min(const Expr &, const Expr &);
    Expr Clamp(const Expr &, const Expr &, const Expr &);

    // Make a cast node
    Expr Cast(const Type &, const Expr &);

    template<typename T>
    Expr Cast(const Expr &e) {
        return Cast(TypeOf<T>(), e);
    }

}

#endif
