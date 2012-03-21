#include <vector>

#include "Expr.h"
#include "Type.h"
#include "Var.h"
#include "Func.h"
#include "Uniform.h"
#include "Image.h"
#include "Tuple.h"
#include <sstream>

namespace Halide {

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
    ML_FUNC2(makeAnd);
    ML_FUNC2(makeOr);
    ML_FUNC1(makeNot);

    template<typename T>
    void set_add(std::vector<T> &a, const T &b) {
        for (size_t i = 0; i < a.size(); i++) {
            if (a[i] == b) return;
        }
        a.push_back(b);
    }

    template<typename T>
    void set_union(std::vector<T> &a, const std::vector<T> &b) {
        for (size_t i = 0; i < b.size(); i++) {
            set_add(a, b[i]);
        }
    }

    struct Expr::Contents {
        Contents(MLVal n, Type t) : node(n), type(t), isVar(false), isRVar(false), implicitArgs(0) {}
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
        bool isVar, isRVar;
        
        // The number of arguments that remain implicit
        int implicitArgs;

        // tuple shape
        std::vector<int> shape;
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
        contents->isRVar = true;
        contents->rvars.push_back(v);
        if (v.min().isDefined()) child(v.min());
        if (v.size().isDefined()) child(v.size());
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

    Expr::Expr(const Expr &other) : contents(other.contents) {        
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

    bool Expr::isRVar() const {
        return contents->isRVar;
    }

    int Expr::implicitArgs() const {
        return contents->implicitArgs;
    }

    std::vector<int> &Expr::shape() const {
        return contents->shape;
    }
    
    void Expr::addImplicitArgs(int a) {
        contents->implicitArgs += a;
    }

    void Expr::convertRVarsToVars() {
        for (size_t i = 0; i < contents->rvars.size(); i++) {
            contents->vars.push_back(Var(contents->rvars[i].name()));
        }
        contents->rvars.clear();
        if (contents->isRVar) {
            contents->isRVar = false;
            contents->isVar = true;
        }
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
        set_union(images, c.images());
        set_union(vars, c.vars());
        set_union(rvars, c.rvars());
        set_union(funcs, c.funcs());
        set_union(uniforms, c.uniforms());
        set_union(uniformImages, c.uniformImages());
        if (c.implicitArgs() > implicitArgs) implicitArgs = c.implicitArgs();
        
        for (size_t i = 0; i < c.shape().size(); i++) {
            if (i < shape.size()) {
                assert(shape[i] == c.shape()[i]);                
            } else {
                shape.push_back(c.shape()[i]);
            }
        }
    }

    void Expr::child(const Expr &c) {
        contents->child(c);
    }

    void Expr::child(const UniformImage &im) {
        set_add(contents->uniformImages, im);
    }

    void Expr::child(const DynUniform &u) {
        set_add(contents->uniforms, u);
    }

    void Expr::child(const DynImage &im) {
        set_add(contents->images, im);
    }

    void Expr::child(const Var &v) {
        set_add(contents->vars, v);
    }

    void Expr::child(const RVar &v) {
        set_add(contents->rvars, v);
    }

    void Expr::child(const Func &f) {
        set_add(contents->funcs, f);
    }

    void Expr::operator+=(const Expr & other) {        
        contents->node = makeAdd(node(), other.node());
        child(other);
    }

    /*
    Tuple Expr::operator,(const Expr &other) {
        return Tuple(*this, other);
    }
    */
    
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
        return cast(a.type(), 0) - a;
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
    
    Expr operator&&(const Expr &a, const Expr &b) {
        Expr e(makeAnd(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator||(const Expr &a, const Expr &b) {
        Expr e(makeOr(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator!(const Expr &a) {
        Expr e(makeNot(a.node()), Int(1));
        e.child(a);
        return e;
    }

    Expr builtin(Type t, const std::string &name) {
        MLVal args = makeList();
        Expr e(makeCall(t.mlval, "." + name, args), t);
        return e;
    }

    Expr builtin(Type t, const std::string &name, const Expr &a) {
        MLVal args = makeList();
        Expr arg = cast<float>(a);
        args = addToList(args, arg.node());
        Expr e(makeCall(t.mlval, "." + name, args), t);
        e.child(a);
        return e;
    }

    Expr builtin(Type t, const std::string &name, const Expr &a, const Expr &b) {
        MLVal args = makeList();
        Expr arg_a = cast<float>(a);
        Expr arg_b = cast<float>(b);
        args = addToList(args, arg_b.node());
        args = addToList(args, arg_a.node());
        Expr e(makeCall(t.mlval, "." + name, args), t);
        e.child(a);
        e.child(b);
        return e;
    }

    Expr sqrt(const Expr &a) {
        return builtin(Float(32), "sqrt_f32", a);
    }

    Expr sin(const Expr &a) {
        return builtin(Float(32), "sin_f32", a);
    }
    
    Expr cos(const Expr &a) {
        return builtin(Float(32), "cos_f32", a);
    }

    Expr pow(const Expr &a, const Expr &b) {
        return builtin(Float(32), "pow_f32", a, b);
    }

    Expr exp(const Expr &a) {
        return builtin(Float(32), "exp_f32", a);
    }

    Expr log(const Expr &a) {
        return builtin(Float(32), "log_f32", a);
    }

    Expr floor(const Expr &a) {
        return builtin(Float(32), "floor_f32", a);
    }


    Expr select(const Expr & cond, const Expr & thenCase, const Expr & elseCase) {
        Expr e(makeSelect(cond.node(), thenCase.node(), elseCase.node()), thenCase.type());
        e.child(cond);
        e.child(thenCase);
        e.child(elseCase);
        return e;
    }
    
    Expr max(const Expr &a, const Expr &b) {
        Expr e(makeMax(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr min(const Expr &a, const Expr &b) {
        Expr e(makeMin(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr clamp(const Expr &a, const Expr &mi, const Expr &ma) {
        return max(min(a, ma), mi);
    }


    Expr::Expr(const FuncRef &f) : contents(new Contents(f)) {}

    Expr::Expr(const Func &f) : contents(new Contents(f)) {}

    Expr::Contents::Contents(const FuncRef &f) {
        // make a call node
        MLVal exprlist = makeList();

        // Start with the implicit arguments
        /*printf("This call to %s has %d arguments when %s takes %d args\n", 
               f.f().name().c_str(),
               (int)f.args().size(),
               f.f().name().c_str(),
               (int)f.f().args().size()); */
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

        // Reach through the call to extract buffer dependencies and
        // function dependencies (but not free vars, tuple shape,
        // implicit args)
        if (f.f().rhs().isDefined()) {
            set_union(images, f.f().rhs().images());
            set_union(funcs, f.f().rhs().funcs());
            set_union(uniforms, f.f().rhs().uniforms());
            set_union(uniformImages, f.f().rhs().uniformImages());
        }

    }

    Expr debug(Expr expr, const std::string &prefix) {
        std::vector<Expr> args;
        return debug(expr, prefix, args);
    }

    Expr debug(Expr expr, const std::string &prefix, Expr a) {
        std::vector<Expr> args {a};
        return debug(expr, prefix, args);
    }

    Expr debug(Expr expr, const std::string &prefix, Expr a, Expr b) {
        std::vector<Expr> args {a, b};
        return debug(expr, prefix, args);
    }

    Expr debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c) {
        std::vector<Expr> args {a, b, c};
        return debug(expr, prefix, args);
    }


    Expr debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d) {
        std::vector<Expr> args {a, b, c, d};
        return debug(expr, prefix, args);
    }

    Expr debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d, Expr e) {
        std::vector<Expr> args {a, b, c, d, e};
        return debug(expr, prefix, args);
    }

    Expr debug(Expr e, const std::string &prefix, const std::vector<Expr> &args) {
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


    Expr cast(const Type &t, const Expr &e) {
        Expr c(makeCast(t.mlval, e.node()), t);
        c.child(e);
        return c;
    }

}
