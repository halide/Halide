#include <vector>

#include "Expr.h"
#include "Type.h"
#include "Var.h"
#include "Func.h"
#include "Uniform.h"
#include "Image.h"

namespace FImage {

    ML_FUNC1(makeIntImm);
    ML_FUNC1(makeFloatImm);
    ML_FUNC1(makeUIntImm);
    ML_FUNC1(makeVar);
    ML_FUNC3(makeLoad);
    ML_FUNC2(makeCast);
    ML_FUNC2(makeAdd);
    ML_FUNC2(makeSub);
    ML_FUNC2(makeMul);
    ML_FUNC2(makeDiv);
    ML_FUNC2(makeEQ);
    ML_FUNC2(makeNE);
    ML_FUNC2(makeLT);
    ML_FUNC2(makeGT);
    ML_FUNC2(makeGE);
    ML_FUNC2(makeLE);
    ML_FUNC3(makeSelect);
    ML_FUNC3(makeDebug);
    ML_FUNC3(makeCall);

    template<typename T>
    void unify(std::vector<T> &a, const std::vector<T> &b) {
        for (size_t i = 0; i < b.size(); i++) {
            bool is_in_a = false;
            for (size_t j = 0; j < a.size(); j++) {
                if (a[j] == b[i]) is_in_a = true;
            }
            if (!is_in_a) a.push_back(b[i]);
        }
    }

  struct Expr::Contents {
      Contents(MLVal n, Type t) : node(n), type(t), isVar(false) {}
      // The ML-value of the expression
      MLVal node;
      
      // The (dynamic) type of the expression
      Type type;
      
      // The list of argument buffers contained within subexpressions            
      std::vector<DynImage> images;
      
      // The list of free variables found
      std::vector<Var> vars;
      
      // The list of functions directly called        
      std::vector<Func> funcs;
      
      // The list of uniforms referred to
      std::vector<DynUniform> uniforms;
      
      // Sometimes it's useful to be able to tell if an expression is a simple var or not
      bool isVar;
  };       




    Expr::Expr() {
    }

    Expr::Expr(MLVal n, Type t) : contents(new Contents(n, t)) {
    }

    Expr::Expr(int32_t val) : contents(new Contents(makeIntImm(val), Int(32))) {
    }

    Expr::Expr(uint32_t val) : contents(new Contents(makeUIntImm(val), UInt(32))) {
    }

    Expr::Expr(float val) : contents(new Contents(makeFloatImm(val), Float(32))) {
    }

    Expr::Expr(const Var &v) : contents(new Contents(makeVar((v.name())), Int(32))) {
        contents->isVar = true;
        contents->vars.push_back(v);
    }

    Expr::Expr(const DynUniform &u) : contents(new Contents(makeLoad(u.type().mlval, 
                                                                     (u.name()), 
                                                                     makeIntImm((0))), 
                                                            u.type())) { 
        contents->uniforms.push_back(u);
    }

    Expr::Expr(const ImageRef &l) : contents(new Contents(makeLoad(l.image.type().mlval, 
                                                                   (l.image.name()),
                                                                   l.idx.node()),
                                                          l.image.type())) {
        contents->images.push_back(l.image);
    }

    const MLVal &Expr::node() const {
        return contents->node;
    }

    const Type &Expr::type() const {
        return contents->type;
    }

    bool  Expr::isVar() const {
        return contents->isVar;
    }

    const std::vector<DynUniform> &Expr::uniforms() const {
        return contents->uniforms;
    }

    const std::vector<DynImage> &Expr::images() const {
        return contents->images;
    }

    const std::vector<Var> &Expr::vars() const {
        return contents->vars;
    }

    const std::vector<Func> &Expr::funcs() const {
        return contents->funcs;
    }

    bool Expr::isDefined() const {
        return (bool)(contents);
    }

    // declare that this node has a child for bookkeeping
    void Expr::child(const Expr &c) {
        unify(contents->images, c.images());
        unify(contents->vars, c.vars());
        unify(contents->funcs, c.funcs());
        unify(contents->uniforms, c.uniforms());
    }

    void Expr::operator+=(const Expr & other) {        
        contents->node = makeAdd(node(), other.node());
        child(other);
    }
    
    void Expr::operator*=(const Expr & other) {
        contents->node = makeMul(node(), other.node());
        child(other);
    }

    void Expr::operator/=(const Expr & other) {
        contents->node = makeDiv(node(), other.node());
        child(other);
    }

    void Expr::operator-=(const Expr & other) {
        contents->node = makeSub(node(), other.node());
        child(other);
    }

    Expr operator+(const Expr & a, const Expr & b) {
        Expr e(makeAdd(a.node(), b.node()), a.type());
        e.child(a); 
        e.child(b); 
        return e;
    }

    Expr operator-(const Expr & a, const Expr & b) {
        Expr e(makeSub(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator*(const Expr & a, const Expr & b) {
        Expr e(makeMul(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator/(const Expr & a, const Expr & b) {
        Expr e(makeDiv(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator>(const Expr & a, const Expr & b) {
        Expr e(makeGT(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<(const Expr & a, const Expr & b) {
        Expr e(makeLT(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator>=(const Expr & a, const Expr & b) {
        Expr e(makeGE(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<=(const Expr & a, const Expr & b) {
        Expr e(makeLE(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator!=(const Expr & a, const Expr & b) {
        Expr e(makeNE(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator==(const Expr & a, const Expr & b) {
        Expr e(makeEQ(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr Select(const Expr & cond, const Expr & thenCase, const Expr & elseCase) {
        Expr e(makeSelect(cond.node(), thenCase.node(), elseCase.node()), thenCase.type());
        e.child(cond);
        e.child(thenCase);
        e.child(elseCase);
        return e;
    }
    
    Expr::Expr(const FuncRef &f) {
        // make a call node
        MLVal exprlist = makeList();
        for (size_t i = f.args().size(); i > 0; i--) {
            exprlist = addToList(exprlist, f.args()[i-1].node());            
        }
        printf("Making a call node\n");

        //if (!f.f().rhs().isDefined()) {
            //printf("Can't infer the return type when calling a function that hasn't been defined yet\n");
        //}

        contents.reset(new Contents(makeCall(f.f().returnType().mlval, 
                                             (f.f().name()),
                                             exprlist), 
                                    f.f().returnType()));
        
        for (size_t i = 0; i < f.args().size(); i++) {
            child(f.args()[i]);
        }
        
        // Add this function call to the calls list
        contents->funcs.push_back(f.f());  

        // Reach through the call to extract buffer dependencies and function dependencies (but not free vars)
        if (f.f().rhs().isDefined()) {
            unify(contents->images, f.f().rhs().images());
            unify(contents->funcs, f.f().rhs().funcs());
            unify(contents->uniforms, f.f().rhs().uniforms());
        }
    }

    Expr Debug(Expr expr, const std::string &prefix) {
        std::vector<Expr> args;
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr expr, const std::string &prefix, Expr a) {
        std::vector<Expr> args {a};
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr expr, const std::string &prefix, Expr a, Expr b) {
        std::vector<Expr> args {a, b};
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c) {
        std::vector<Expr> args {a, b, c};
        return Debug(expr, prefix, args);
    }


    Expr Debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d) {
        std::vector<Expr> args {a, b, c, d};
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d, Expr e) {
        std::vector<Expr> args {a, b, c, d, e};
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr e, const std::string &prefix, const std::vector<Expr> &args) {
        MLVal mlargs = makeList();
        for (size_t i = args.size(); i > 0; i--) {
            mlargs = addToList(mlargs, args[i-1].node()());
        }

        Expr d(makeDebug(e.node(), (prefix), mlargs), e.type());        
        d.child(e);
        for (size_t i = 0; i < args.size(); i++) {
            d.child(args[i]);
        }
        return d;
    }


    Expr Cast(const Type &t, const Expr &e) {
        Expr cast(makeCast(t.mlval, e.node()), t);
        cast.child(e);
        return cast;
    }

}
