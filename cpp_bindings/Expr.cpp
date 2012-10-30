#include <vector>

#include "Expr.h"
#include "ExprContents.h"
#include "Type.h"
#include "Var.h"
#include "Func.h"
#include "Uniform.h"
#include "Image.h"
#include "Util.h"
#include <sstream>

namespace Halide {

    ML_FUNC1(makeIntImm);
    ML_FUNC1(makeFloatImm);
    ML_FUNC2(makeUniform);
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
    ML_FUNC3(makeExternCall);
    ML_FUNC3(makeImageCall);
    ML_FUNC2(makeAnd);
    ML_FUNC2(makeOr);
    ML_FUNC1(makeNot);

    ML_FUNC1(stringOfExpr);

    ML_FUNC2(footprintOfFuncInExpr);

    Expr::Expr() {
    }

    Expr::Expr(MLVal n, Type t) : contents(new ExprContents(n, t)) {
    }

    Expr::Expr(int32_t val) : contents(new ExprContents(makeIntImm(val), Int(32))) {
        contents->isImmediate = true;
    }

    Expr::Expr(uint32_t val) : contents(new ExprContents(makeCast(UInt(32).mlval, makeIntImm(val)), UInt(32))) {
        contents->isImmediate = true;
    }

    Expr::Expr(float val) : contents(new ExprContents(makeFloatImm(val), Float(32))) {
        contents->isImmediate = true;
    }

    Expr::Expr(double val) : contents(new ExprContents(makeCast(Float(64).mlval, makeFloatImm(val)), Float(64))) {
        contents->isImmediate = true;
    }

    Expr::Expr(const Var &v) : contents(new ExprContents(makeVar((v.name())), Int(32))) {
        contents->isVar = true;
        contents->vars.push_back(v);
    }

    Expr::Expr(const RVar &v) : contents(new ExprContents(makeVar((v.name())), Int(32))) {
        contents->isRVar = true;
        assert(v.isDefined());
        assert(v.domain().isDefined());
        setRDom(v.domain());
        child(v.min());
        child(v.size());
    }

    Expr::Expr(const RDom &d) : contents(new ExprContents(makeVar((d[0].name())), Int(32))) {
        contents->isRVar = true;
        assert(d.dimensions() == 1 && "Can only use single-dimensional domains directly as expressions\n");
        setRDom(d);
        child(d[0].min());
        child(d[0].size());
    }

    Expr::Expr(const DynUniform &u) : 
        contents(new ExprContents(makeUniform(u.type().mlval, u.name()), u.type())) {
        contents->uniforms.push_back(u);
    }

    Expr::Expr(const ImageRef &l) {        
        MLVal args = makeList();
        for (size_t i = l.idx.size(); i > 0; i--) {
            args = addToList(args, l.idx[i-1].node());
        }
        MLVal node = makeImageCall(l.image.type().mlval, l.image.name(), args);
        contents.reset(new ExprContents(node, l.image.type()));
        for (size_t i = 0; i < l.idx.size(); i++) {
            child(l.idx[i]);
        }
        contents->images.push_back(l.image);
    }

    Expr::Expr(const UniformImageRef &l) {
        MLVal args = makeList();
        for (size_t i = l.idx.size(); i > 0; i--) {
            args = addToList(args, l.idx[i-1].node());
        }
        MLVal node = makeImageCall(l.image.type().mlval, l.image.name(), args);
        contents.reset(new ExprContents(node, l.image.type()));
        for (size_t i = 0; i < l.idx.size(); i++) {
            child(l.idx[i]);
        }
        contents->uniformImages.push_back(l.image);
    }

    Expr::Expr(const Expr &other) : contents(other.contents) {        
    }

    Expr::Expr(ExprContents *c) : contents(c) {}

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

    void Expr::setRDom(const RDom &dom) {
        contents->rdom = dom;
    }

    bool Expr::isImmediate() const {
        return contents->isImmediate;
    }

    int Expr::implicitArgs() const {
        return contents->implicitArgs;
    }

    void Expr::addImplicitArgs(int a) {
        contents->implicitArgs += a;
    }

    void Expr::convertRVarsToVars() {
        if (contents->rdom.isDefined()) {
            for (int i = 0; i < contents->rdom.dimensions(); i++) {
                contents->vars.push_back(Var(contents->rdom[i].name()));
            }
            contents->rdom = RDom();
        }
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

    const RDom &Expr::rdom() const {
        return contents->rdom;
    }

    const std::vector<Func> &Expr::funcs() const {
        return contents->funcs;
    }

    const std::vector<Func> &Expr::transitiveFuncs() const {
        return contents->transitiveFuncs;
    }

    const std::vector<UniformImage> &Expr::uniformImages() const {
        return contents->uniformImages;
    }

    std::vector<int> Expr::footprint(const Func& f) const {
        MLVal fp = footprintOfFuncInExpr(f.name(), contents->node);
        assert(!listEmpty(fp));

        std::vector<int> footprint;
        for (; !listEmpty(fp); fp = listTail(fp)) {          
            footprint.push_back(int(listHead(fp)));
        }

        return footprint;
    }

    bool Expr::isDefined() const {
        return (bool)(contents);
    }

    const std::string Expr::pretty() const {
        return stringOfExpr(contents->node);
    }


    void Expr::child(Expr c) {
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

    void Expr::child(const Func &f) {
        set_add(contents->funcs, f);
    }

    void Expr::operator+=(Expr other) {                
        other = cast(type(), other);
        contents->node = makeAdd(node(), other.node());
        child(other);
    }

    
    void Expr::operator*=(Expr other) {
        other = cast(type(), other);
        contents->node = makeMul(node(), other.node());
        child(other);
    }

    void Expr::operator/=(Expr other) {
        other = cast(type(), other);
        contents->node = makeDiv(node(), other.node());
        child(other);
    }

    void Expr::operator-=(Expr other) {
        other = cast(type(), other);
        contents->node = makeSub(node(), other.node());
        child(other);
    }

    void matchTypes(Expr *pa, Expr *pb) {
        Expr a = *pa, b = *pb;

        Type ta = a.type(), tb = b.type();

        if (ta == tb) return;
        
        // int(a) * float(b) -> float(b)
        // uint(a) * float(b) -> float(b)
        if (!ta.isFloat() && tb.isFloat()) {*pa = cast(tb, a); return;}
        if (ta.isFloat() && !tb.isFloat()) {*pb = cast(ta, b); return;}
        
        // float(a) * float(b) -> float(max(a, b))
        if (ta.isFloat() && tb.isFloat()) {
            if (ta.bits > tb.bits) *pb = cast(ta, b);
            else *pa = cast(tb, a); 
            return;
        }

        // (u)int(a) * (u)intImm(b) -> int(a)
        if (!ta.isFloat() && !tb.isFloat() && b.isImmediate()) {*pb = cast(ta, b); return;}
        if (!tb.isFloat() && !ta.isFloat() && a.isImmediate()) {*pa = cast(tb, a); return;}

        // uint(a) * uint(b) -> uint(max(a, b))
        if (ta.isUInt() && tb.isUInt()) {
            if (ta.bits > tb.bits) *pb = cast(ta, b);
            else *pa = cast(tb, a);
            return;
        }

        // int(a) * (u)int(b) -> int(max(a, b))
        if (!ta.isFloat() && !tb.isFloat()) {
            int bits = std::max(ta.bits, tb.bits);
            *pa = cast(Int(bits), a);
            *pb = cast(Int(bits), b);
            return;
        }        

        printf("Could not match types: %s, %s\n", ta.str().c_str(), tb.str().c_str());
        assert(false && "Failed type coercion");
    }

    Expr operator+(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to + must have the same type");
        matchTypes(&a, &b);
        Expr e(makeAdd(a.node(), b.node()), a.type());
        e.child(a); 
        e.child(b); 
        return e;
    }

    Expr operator-(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to - must have the same type");
        matchTypes(&a, &b);
        Expr e(makeSub(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator-(Expr a) {
        return cast(a.type(), 0) - a;
    }

    Expr operator*(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to * must have the same type");
        matchTypes(&a, &b);
        Expr e(makeMul(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator/(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to / must have the same type");
        matchTypes(&a, &b);
        Expr e(makeDiv(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator%(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to % must have the same type");
        matchTypes(&a, &b);
        Expr e(makeMod(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator>(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to > must have the same type");
        matchTypes(&a, &b);
        Expr e(makeGT(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to < must have the same type");
        matchTypes(&a, &b);
        Expr e(makeLT(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator>=(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to >= must have the same type");
        matchTypes(&a, &b);
        Expr e(makeGE(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<=(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to <= must have the same type");
        matchTypes(&a, &b);
        Expr e(makeLE(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator!=(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to != must have the same type");
        matchTypes(&a, &b);
        Expr e(makeNE(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator==(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to == must have the same type");
        matchTypes(&a, &b);
        Expr e(makeEQ(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator&&(Expr a, Expr b) {
        //assert(a.type() == Int(1) && b.type() == Int(1) && "Arguments to && must be bool");
        a = cast(Int(1), a);
        b = cast(Int(1), b);        
        Expr e(makeAnd(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator||(Expr a, Expr b) {
        //assert(a.type() == Int(1) && b.type() == Int(1) && "Arguments to || must be bool");
        a = cast(Int(1), a);
        b = cast(Int(1), b);        
        Expr e(makeOr(a.node(), b.node()), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator!(Expr a) {
        //assert(a.type() == Int(1) && "Argument to ! must be bool");
        a = cast(Int(1), a);
        Expr e(makeNot(a.node()), Int(1));
        e.child(a);
        return e;
    }

    Expr builtin(Type t, const std::string &name) {
        MLVal args = makeList();
        Expr e(makeExternCall(t.mlval, name, args), t);
        return e;
    }

    Expr builtin(Type t, const std::string &name, Expr a) {
        MLVal args = makeList();
        args = addToList(args, a.node());
        Expr e(makeExternCall(t.mlval, name, args), t);
        e.child(a);
        return e;
    }

    Expr builtin(Type t, const std::string &name, Expr a, Expr b) {
        MLVal args = makeList();
        args = addToList(args, b.node());
        args = addToList(args, a.node());
        Expr e(makeExternCall(t.mlval, name, args), t);
        e.child(a);
        e.child(b);
        return e;
    }

    Expr builtin(Type t, const std::string &name, Expr a, Expr b, Expr c) {
        MLVal args = makeList();
        args = addToList(args, c.node());
        args = addToList(args, b.node());
        args = addToList(args, a.node());
        Expr e(makeExternCall(t.mlval, name, args), t);
        e.child(a);
        e.child(b);
        e.child(c);
        return e;
    }

    Expr builtin(Type t, const std::string &name, Expr a, Expr b, Expr c, Expr d) {
        MLVal args = makeList();
        args = addToList(args, d.node());
        args = addToList(args, c.node());
        args = addToList(args, b.node());
        args = addToList(args, a.node());
        Expr e(makeExternCall(t.mlval, name, args), t);
        e.child(a);
        e.child(b);
        e.child(c);
        e.child(d);
        return e;
    }

    Expr sqrt(Expr a) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "sqrt_f64", a);
        }
        // Otherwise cast to float
        a = cast(Float(32), a);
        return builtin(Float(32), "sqrt_f32", a);
    }

    Expr sin(Expr a) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "sin_f64", a);
        }
        //assert(a.type() == Float(32) && "Argument to sin must be a float");
        a = cast(Float(32), a);
        return builtin(Float(32), "sin_f32", a);
    }
    
    Expr cos(Expr a) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "cos_f64", a);
        }
        a = cast(Float(32), a);
        return builtin(Float(32), "cos_f32", a);
    }

    Expr pow(Expr a, Expr b) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "pow_f64", a, cast(Float(64), b));
        }
        a = cast(Float(32), a);
        b = cast(Float(32), b);        
        return builtin(Float(32), "pow_f32", a, b);
    }

    Expr exp(Expr a) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "exp_f64", a);
        }
        a = cast(Float(32), a);
        return builtin(Float(32), "exp_f32", a);
    }

    Expr log(Expr a) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "log_f64", a);
        }
        a = cast(Float(32), a);
        return builtin(Float(32), "log_f32", a);
    }

    Expr floor(Expr a) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "floor_f64", a);
        }
        a = cast(Float(32), a);
        return builtin(Float(32), "floor_f32", a);
    }

    Expr ceil(Expr a) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "ceil_f64", a);
        }
        a = cast(Float(32), a);
        return builtin(Float(32), "ceil_f32", a);
    }

    Expr round(Expr a) {
        if (a.type() == Float(64)) {
            return builtin(Float(64), "round_f64", a);
        }
        a = cast(Float(32), a);
        return builtin(Float(32), "round_f32", a);
    }

    Expr abs(Expr a) {
        if (a.type() == Int(8))
            return builtin(Int(8), "abs_i8", a);
        if (a.type() == Int(16)) 
            return builtin(Int(16), "abs_i16", a);
        if (a.type() == Int(32)) 
            return builtin(Int(32), "abs_i32", a);
        if (a.type() == Int(64)) 
            return builtin(Int(64), "abs_i64", a);
        if (a.type() == Float(32)) 
            return builtin(Float(32), "abs_f32", a);
        if (a.type() == Float(64)) 
            return builtin(Float(64), "abs_f64", a);
        assert(0 && "Invalid type for abs");
    }

    Expr select(Expr cond, Expr thenCase, Expr elseCase) {
        //assert(thenCase.type() == elseCase.type() && "then case must have same type as else case in select");
        //assert(cond.type() == Int(1) && "condition must have type bool in select");
        matchTypes(&thenCase, &elseCase);
        cond = cast(Int(1), cond);
        Expr e(makeSelect(cond.node(), thenCase.node(), elseCase.node()), thenCase.type());
        e.child(cond);
        e.child(thenCase);
        e.child(elseCase);
        return e;
    }
    
    Expr max(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to max must have the same type");
        matchTypes(&a, &b);
        Expr e(makeMax(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }

    Expr min(Expr a, Expr b) {
        //assert(a.type() == b.type() && "Arguments to min must have the same type");
        matchTypes(&a, &b);
        Expr e(makeMin(a.node(), b.node()), a.type());
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr clamp(Expr a, Expr mi, Expr ma) {      
        //assert(a.type() == mi.type() && a.type() == ma.type() && "Arguments to clamp must have the same type");
        mi = cast(a.type(), mi);
        ma = cast(a.type(), ma);
        return max(min(a, ma), mi);
    }


    Expr::Expr(const FuncRef &f) : contents(new ExprContents(f)) {}

    Expr::Expr(const Func &f) : contents(new ExprContents(f)) {}

    Expr debug(Expr expr, const std::string &prefix) {
        std::vector<Expr> args;
        return debug(expr, prefix, args);
    }

    Expr debug(Expr expr, const std::string &prefix, Expr a) {
        std::vector<Expr> args = vec(a);
        return debug(expr, prefix, args);
    }

    Expr debug(Expr expr, const std::string &prefix, Expr a, Expr b) {
        std::vector<Expr> args = vec(a, b);
        return debug(expr, prefix, args);
    }

    Expr debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c) {
        std::vector<Expr> args = vec(a, b, c);
        return debug(expr, prefix, args);
    }


    Expr debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d) {
        std::vector<Expr> args = vec(a, b, c, d);
        return debug(expr, prefix, args);
    }

    Expr debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d, Expr e) {
        std::vector<Expr> args = vec(a, b, c, d, e);
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


    Expr cast(Type t, Expr e) {
        if (e.type() == t) return e;
        Expr c(makeCast(t.mlval, e.node()), t);
        c.child(e);
        return c;
    }

}
