#ifndef IR_H
#define IR_H
#include <string>
#include <vector>
#include <utility>
#include <assert.h>

#include "IRVisitor.h"

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
        virtual void visit(IRVisitor *v) const = 0;
        int gc_mark;
    };

    struct Expr : public IR {
    };

    struct Stmt : public IR {
    };

    struct IntImm : public Expr {
        int value;
        IntImm(float v) : value(v) {}

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct FloatImm : public Expr {
        float value;
        FloatImm(float v) : value(v) {}

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Cast : public Expr {
        Type type;
        const Expr *value;
        Cast(Type t, const Expr *v) : type(t), value(v) {
            assert(v && "Cast of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Var : public Expr {
        Type type;
        const string name;
        Var(Type t, const string n) : type(t), name(n) {}

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Add : public Expr {
        const Expr *a, *b;
        Add(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "Add of NULL");
            assert(b && "Add of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Sub : public Expr {
        const Expr *a, *b;
        Sub(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "Sub of NULL");
            assert(b && "Sub of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Mul : public Expr {
        const Expr *a, *b;
        Mul(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "Mul of NULL");
            assert(b && "Mul of NULL");
        }        

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Div : public Expr {
        const Expr *a, *b;
        Div(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "Div of NULL");
            assert(b && "Div of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Mod : public Expr {
        const Expr *a, *b;
        Mod(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "Mod of NULL");
            assert(b && "Mod of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Min : public Expr {
        const Expr *a, *b;
        Min(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "Min of NULL");
            assert(b && "Min of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Max : public Expr {
        const Expr *a, *b;
        Max(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "Max of NULL");
            assert(b && "Max of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct EQ : public Expr {
        const Expr *a, *b;
        EQ(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "EQ of NULL");
            assert(b && "EQ of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct NE : public Expr {
        const Expr *a, *b;
        NE(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "NE of NULL");
            assert(b && "NE of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct LT : public Expr {
        const Expr *a, *b;
        LT(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "LT of NULL");
            assert(b && "LT of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct LE : public Expr {
        const Expr *a, *b;
        LE(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "LE of NULL");
            assert(b && "LE of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct GT : public Expr {
        const Expr *a, *b;
        GT(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "GT of NULL");
            assert(b && "GT of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct GE : public Expr {
        const Expr *a, *b;
        GE(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "GE of NULL");
            assert(b && "GE of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct And : public Expr {
        const Expr *a, *b;
        And(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "And of NULL");
            assert(b && "And of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Or : public Expr {
        const Expr *a, *b;
        Or(const Expr *_a, const Expr *_b) : a(_a), b(_b) {
            assert(a && "Or of NULL");
            assert(b && "Or of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Not : public Expr {
        const Expr *a;
        Not(const Expr *_a) : a(_a) {
            assert(a && "Not of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Select : public Expr {
        const Expr *condition, *true_value, *false_value;
        Select(const Expr *c, const Expr *t, const Expr *f) : 
            condition(c), true_value(t), false_value(f) {
            assert(condition && "Select of NULL");
            assert(true_value && "Select of NULL");
            assert(false_value && "Select of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}        
    };

    struct Load : public Expr {
        Type type;
        const string buffer;
        const Expr *index;
        Load(Type t, const string b, const Expr *i) : 
            type(t), buffer(b), index(i) {
            assert(index && "Load of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Ramp : public Expr {
        const Expr *base, *stride;
        int width;
        Ramp(const Expr *b, const Expr *s, int w) : 
            base(b), stride(s), width(w) {
            assert(base && "Ramp of NULL");
            assert(stride && "Ramp of NULL");
            assert(w > 0 && "Ramp of width <= 0");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Call : public Expr {
        Type type;
        const string buffer;
        vector<const Expr *> args;
        typedef enum {Image, Extern, Halide} CallType;
        CallType call_type;

        Call(Type t, const string b, const vector<const Expr *> &a, CallType ct) : 
            type(t), buffer(b), args(a), call_type(ct) {
            for (size_t i = 0; i < args.size(); i++) {
                assert(args[i] && "Call of NULL");
            }
        }


        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Let : public Expr {
        const string name;
        const Expr *value, *body;
        Let(const string n, const Expr *v, const Expr *b) : 
            name(n), value(v), body(b) {
            assert(value && "Let of NULL");
            assert(body && "Let of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct LetStmt : public Stmt {
        const string name;
        const Expr *value;
        const Stmt *body;
        LetStmt(const string n, const Expr *v, const Stmt *b) : 
            name(n), value(v), body(b) {
            assert(value && "LetStmt of NULL");
            assert(body && "LetStmt of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct PrintStmt : public Stmt {
        const string prefix;
        vector<const Expr *> args;
        PrintStmt(const string p, const vector<const Expr *> &a) :
            prefix(p), args(a) {
            for (size_t i = 0; i < args.size(); i++) {
                assert(args[i] && "PrintStmt of NULL");
            }
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct AssertStmt : public Stmt {
        // if condition then val else error out with message
        const Expr *condition;
        const string message;

        AssertStmt(const Expr *c, const string m) :
            condition(c), message(m) {
            assert(condition && "AssertStmt of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Pipeline : public Stmt {
        const Stmt *produce, *update, *consume;

        Pipeline(const Stmt *p, const Stmt *u, const Stmt *c) : 
            produce(p), update(u), consume(c) {
            assert(produce && "Pipeline of NULL");
            // update is allowed to be null
            assert(consume && "Pipeline of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };
    
    struct For : public Stmt {
        const string name;
        const Expr *min, *extent;
        typedef enum {Serial, Parallel, Vectorized, Unrolled} ForType;
        ForType for_type;
        const Stmt *body;

        For(const string n, const Expr *m, const Expr *e, ForType f, const Stmt *b) :
            name(n), min(m), extent(e), for_type(f), body(b) {
            assert(min && "For of NULL");
            assert(extent && "For of NULL");
            assert(body && "For of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Store : public Stmt {
        const string buffer;
        const Expr *value, *index;

        Store(const string b, const Expr *v, const Expr *i) :
            buffer(b), value(v), index(i) {
            assert(value && "Store of NULL");
            assert(index && "Store of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Provide : public Stmt {
        const string buffer;
        const Expr *value;
        vector<const Expr *> args;

        Provide(const string b, const Expr *v, const vector<const Expr *> &a) : 
            buffer(b), value(v), args(a) {
            assert(value && "Provide of NULL");
            for (size_t i = 0; i < args.size(); i++) {
                assert(args[i] && "Provide of NULL");
            }
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Allocate : public Stmt {
        const string buffer;
        Type type;
        const Expr *size;
        const Stmt *body;

        Allocate(const string buf, Type t, const Expr *s, const Stmt *bod) : 
            buffer(buf), type(t), size(s), body(bod) {
            assert(size && "Allocate of NULL");
            assert(body && "Allocate of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Realize : public Stmt {
        const string buffer;
        Type type;
        vector<pair<const Expr *, const Expr *> > bounds;
        const Stmt *body;

        Realize(const string buf, Type t, const vector<pair<const Expr *, const Expr *> > &bou, const Stmt *bod) : 
            buffer(buf), type(t), bounds(bou), body(bod) {
            for (size_t i = 0; i < bounds.size(); i++) {
                assert(bounds[i].first && "Realize of NULL");
                assert(bounds[i].second && "Realize of NULL");
            }
            assert(body && "Realize of NULL");
        }

        void visit(IRVisitor *v) const {v->visit(this);}
    };

    struct Block : public Stmt {
        const Stmt *first, *rest;
        
        Block(const Stmt *f, const Stmt *r) : 
            first(f), rest(r) {
            assert(first && "Block of NULL");
            // rest is allowed to be null
        }
        
        void visit(IRVisitor *v) const {v->visit(this);}
    };
}

#endif
