#ifndef IR_H
#define IR_H
#include <string>
#include <vector>
#include <pair>

namespace HalideInternal {

    using std::string;
    using std::vector;
    using std::pair;

    class Type {
        enum {Int, UInt, Float, Stmt} t;
        int bits;
        int width;        
    };

    struct IR {
        virtual void visit(IRVisitor *v) = 0;
        int gc_mark, gc_ref_count;
    };

    struct Expr : public IR {
    };

    struct Stmt : public IR {
    };

    struct IntImm : public Expr {
        int value;
        IntImm(float v) : value(v) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct FloatImm : public Expr {
        float value;
        FloatImm(float v) : value(v) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Cast : public Expr {
        Type type;
        Expr *value;
        Cast(Type t, Expr *v) : type(t), value(v) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Var : public Expr {
        Type type;
        string name;
        Var(Type t, string n) : type(t), name(n) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Add : public Expr {
        Expr *a, *b;
        Add(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Sub : public Expr {
        Expr *a, *b;
        Sub(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Mul : public Expr {
        Expr *a, *b;
        Mul(Expr *_a, Expr *_b) : a(_a), b(_b) {}        

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Div : public Expr {
        Expr *a, *b;
        Div(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Mod : public Expr {
        Expr *a, *b;
        Mod(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Min : public Expr {
        Expr *a, *b;
        Min(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Max : public Expr {
        Expr *a, *b;
        Max(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct EQ : public Expr {
        Expr *a, *b;
        EQ(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct NE : public Expr {
        Expr *a, *b;
        NE(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct LT : public Expr {
        Expr *a, *b;
        LT(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct LE : public Expr {
        Expr *a, *b;
        LE(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct GT : public Expr {
        Expr *a, *b;
        GT(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct GE : public Expr {
        Expr *a, *b;
        GE(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct And : public Expr {
        Expr *a, *b;
        And(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Or : public Expr {
        Expr *a, *b;
        Or(Expr *_a, Expr *_b) : a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Not : public Expr {
        Expr *a;
        Not(Expr *_a) : a(_a) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Select : public Expr {
        Expr *condition, *true_value, *false_value;
        Select(Expr *c, Expr *t, Expr *f) : 
            condition(c), true_value(t), false_value(f) {}

        void visit(IRVisitor *v) {v->visit(this);}        
    };

    struct Load : public Expr {
        Type type;
        string buffer;
        Expr *index;
        Load(Type t, string b, Expr *i) : 
            type(t), buffer(b), index(i) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Ramp : public Expr {
        Expr *base, *stride;
        int width;
        Ramp(Expr *b, Expr *s, int w) : 
            base(b), stride(s), width(w) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Call : public Expr {
        Type type;
        string buffer;
        vector<Expr *> args;
        typedef enum {Image, Extern, Halide} CallType;
        CallType call_type;

        Call(Type t, string b, const vector<Expr *> &a, CallType ct) : 
            type(t), buffer(b), args(a), call_type(ct) {}


        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Let : public Expr {
        string name;
        Expr *a, *b;
        Let(string n, Expr *_a, Expr *_b) : 
            name(n), a(_a), b(_b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct LetStmt : public Stmt {
        string name;
        Expr *value;
        Stmt *body;
        LetStmt(string n, Expr *v, Stmt *b) : 
            name(n), value(v), body(b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct PrintStmt : public Stmt {
        string prefix;
        vector<Expr *> args;
        Stmt *next;
        PrintStmt(string p, const vector<Expr *> &a, Stmt *n) :
            prefix(p), args(a), next(n) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct AssertStmt : public Stmt {
        // if condition then val else error out with message
        Expr *condition;
        string message;
        Stmt *next;

        AssertStmt(string p, const vector<Expr *> &a, Stmt *n) :
            prefix(p), args(a), next(n) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Pipeline : public Stmt {
        Stmt *produce, *update, *consume;

        Pipeline(Stmt *p, Stmt *u, Stmt *c) : 
            produce(p), update(u), consume(c);

        void visit(IRVisitor *v) {v->visit(this);}
    };
    
    struct For : public Stmt {
        string name;
        Expr *min, *extent;
        typedef enum {Serial, Parallel, Vectorized, Unrolled} ForType;
        ForType for_type;
        Stmt *body;        

        For(string n, Expr *m, Expr *e, ForType f, Stmt *b) :
            name(n), min(m), extent(e), for_type(f), body(b) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Store : public Stmt {
        string buffer;
        Expr *value, *index;

        Store(string b, Expr *v, Expr *i) :
            buffer(b), value(v), index(i) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Provide : public Stmt {
        string buffer;
        Expr *value;
        vector<Expr *> args;

        Provide(string b, Expr *v, const vector<Expr *> &a) : 
            buffer(b), value(v), args(a) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Allocate : public Stmt {
        string buffer;
        Type type;
        Expr *size;
        Stmt *body;

        Allocate(string buf, Type t, Expr *s, Stmt *bod) : 
            buffer(buf), type(t), size(s), body(bod) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Realize : public Stmt {
        string buffer;
        Type type;
        vector<pair<Expr *, Expr *> > bounds;
        Stmt *body;

        Provide(string buf, Type t, const vector<pair<Expr *, Expr *> > &bou, Stmt *bod) : 
            buffer(buf), type(t), bounds(bou), body(bod) {}

        void visit(IRVisitor *v) {v->visit(this);}
    };
}
