#include <vector>

#include "Expr.h"
#include "Type.h"
#include "Var.h"
#include "Func.h"
#include "Uniform.h"
#include "Image.h"
#include <sstream>

namespace FImage {

    ML_FUNC1(makeIntImm);
    ML_FUNC1(makeFloatImm);
    ML_FUNC1(makeUIntImm);
    ML_FUNC1(makeVar);
    ML_FUNC2(makeUniform);
    ML_FUNC3(makeLoad);
    ML_FUNC2(makeCast);
    ML_FUNC2(makeAdd);
    ML_FUNC2(makeSub);
    ML_FUNC2(makeMul);
    ML_FUNC2(makeDiv);
    ML_FUNC2(makeMod);
    ML_FUNC2(makeEQ);
    ML_FUNC2(makeNE);
    ML_FUNC2(makeLT);
    ML_FUNC2(makeGT);
    ML_FUNC2(makeGE);
    ML_FUNC2(makeLE);
    ML_FUNC2(makeMax);
    ML_FUNC2(makeMin);
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
        Contents(MLVal n, Type t) : node(n), type(t), isVar(false), implicitArgs(0) {}
        Contents(const FuncRef &f);

        // Declare that this expression is the child of another for bookkeeping
        void child(const Expr &);

        // The ML-value of the expression
        MLVal node;
        
        // The (dynamic) type of the expression
        Type type;
        
        // The list of argument buffers contained within subexpressions            
        std::vector<DynImage> images;
        
        // The list of free variables found
        std::vector<Var> vars;

        // The list of reduction variables found
        std::vector<RVar> rvars;
        
        // The list of functions directly called        
        std::vector<Func> funcs;
        
        // The list of uniforms referred to
        std::vector<DynUniform> uniforms;

        // The list of uniform images referred to
        std::vector<UniformImage> uniformImages;
        
        // Sometimes it's useful to be able to tell if an expression is a simple var or not
        bool isVar;
        
        // The number of arguments that remain implicit
        int implicitArgs;
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

    Expr::Expr(double val) : contents(new Contents(makeCast(Float(64).mlval, makeFloatImm(val)), Float(64))) {
    }

    Expr::Expr(const Var &v) : contents(new Contents(makeVar((v.name())), Int(32))) {
        contents->isVar = true;
        contents->vars.push_back(v);
    }

    Expr::Expr(const RVar &v) : contents(new Contents(makeVar((v.name())), Int(32))) {
        contents->rvars.push_back(v);
    }

    Expr::Expr(const DynUniform &u) : 
        contents(new Contents(makeUniform(u.type().mlval, u.name()), u.type())) {
        contents->uniforms.push_back(u);
    }

    Expr::Expr(const ImageRef &l) :
        contents(new Contents(makeLoad(l.image.type().mlval, l.image.name(), l.idx.node()), l.image.type())) {
        contents->images.push_back(l.image);
        child(l.idx);
    }

    Expr::Expr(const UniformImageRef &l) : 
        contents(new Contents(makeLoad(l.image.type().mlval, l.image.name(), l.idx.node()), l.image.type())) {
        contents->uniformImages.push_back(l.image);
        child(l.idx);
    }

    const MLVal &Expr::node() const {
        return contents->node;
    }

    const Type &Expr::type() const {
        return contents->type;
    }

    bool Expr::isVar() const {
        return contents->isVar;
    }

    int Expr::implicitArgs() const {
        return contents->implicitArgs;
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

    const std::vector<RVar> &Expr::rvars() const {
        return contents->rvars;
    }

    const std::vector<Func> &Expr::funcs() const {
        return contents->funcs;
    }

    const std::vector<UniformImage> &Expr::uniformImages() const {
        return contents->uniformImages;
    }

    bool Expr::isDefined() const {
        return (bool)(contents);
    }

    // declare that this node has a child for bookkeeping
    void Expr::Contents::child(const Expr &c) {
        unify(images, c.images());
        unify(vars, c.vars());
        unify(rvars, c.rvars());
        unify(funcs, c.funcs());
        unify(uniforms, c.uniforms());
        unify(uniformImages, c.uniformImages());
        if (c.implicitArgs() > implicitArgs) implicitArgs = c.implicitArgs();
    }

    void Expr::child(const Expr &c) {
        contents->child(c);
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

    Expr operator-(const Expr &a) {
        return Cast(a.type(), 0) - a;
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

    Expr operator%(const Expr &a, const Expr &b) {
        Expr e(makeMod(a.node(), b.node()), a.type());
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
    
    Expr Max(const Expr &a, const Expr &b) {
        Expr e(makeMax(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr Min(const Expr &a, const Expr &b) {
        Expr e(makeMin(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr Clamp(const Expr &a, const Expr &min, const Expr &max) {
        return Max(Min(a, max), min);
    }


    Expr::Expr(const FuncRef &f) : contents(new Contents(f)) {}

    Expr::Expr(const Func &f) : contents(new Contents(f)) {}

    Expr::Contents::Contents(const FuncRef &f) {
        // make a call node
        MLVal exprlist = makeList();

        // Start with the implicit arguments
        printf("This call to %s has %d arguments when %s takes %d args\n", 
               f.f().name().c_str(),
               (int)f.args().size(),
               f.f().name().c_str(),
               (int)f.f().args().size());
        int iArgs = (int)f.f().args().size() - (int)f.args().size();
        if (iArgs < 0 && f.f().args().size() > 0) {
            printf("Too many arguments in call!\n");
            exit(-1);
        } 

        for (int i = iArgs-1; i >= 0; i--) {
            std::ostringstream ss;
            ss << "iv" << i; // implicit var
            exprlist = addToList(exprlist, makeVar(ss.str()));
        }

        for (size_t i = f.args().size(); i > 0; i--) {
            exprlist = addToList(exprlist, f.args()[i-1].node());            
        }

        //if (!f.f().rhs().isDefined()) {
            //printf("Can't infer the return type when calling a function that hasn't been defined yet\n");
        //}

        node = makeCall(f.f().returnType().mlval, 
                        (f.f().name()),
                        exprlist);
        type = f.f().returnType();

        for (size_t i = 0; i < f.args().size(); i++) {
            if (f.args()[i].implicitArgs() != 0) {
                printf("Can't use a partially applied function as an argument. We don't support higher-order functions.\n");
                exit(-1);
            }
            child(f.args()[i]);
        }

        implicitArgs = iArgs;
        
        // Add this function call to the calls list
        funcs.push_back(f.f());  

        // Reach through the call to extract buffer dependencies and function dependencies (but not free vars)
        if (f.f().rhs().isDefined()) {
            unify(images, f.f().rhs().images());
            unify(funcs, f.f().rhs().funcs());
            unify(uniforms, f.f().rhs().uniforms());
            unify(uniformImages, f.f().rhs().uniformImages());
        }

        printf("Done making call node with %d implicit args\n", implicitArgs);
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
            mlargs = addToList(mlargs, args[i-1].node());
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
