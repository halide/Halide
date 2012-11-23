#ifndef IR_VISITOR_H
#define IR_VISITOR_H

namespace HalideInternal {
    struct IR;
    struct Expr;
    struct Stmt;
    struct IntImm;
    struct FloatImm;
    struct Cast;
    struct Var;
    struct Add;
    struct Sub;
    struct Mul;
    struct Div;
    struct Mod;
    struct Min;
    struct Max;
    struct EQ;
    struct NE;
    struct LT;
    struct LE;
    struct GT;
    struct GE;
    struct And;
    struct Or;
    struct Not;
    struct Select;
    struct Load;
    struct Ramp;
    struct Call;
    struct Let;
    struct LetStmt;
    struct PrintStmt;
    struct AssertStmt;
    struct Pipeline;
    struct For;
    struct Store;
    struct Provide;
    struct Allocate;
    struct Realize;
    struct Block;

    // A base class for things that need to walk over the IR. The
    // default implementations just recursively walk over the
    // children. Override the ones you care about.
    class IRVisitor {
    public:
        virtual void visit(const IntImm *);
        virtual void visit(const FloatImm *);
        virtual void visit(const Cast *);
        virtual void visit(const Var *);
        virtual void visit(const Add *);
        virtual void visit(const Sub *);
        virtual void visit(const Mul *);
        virtual void visit(const Div *);
        virtual void visit(const Mod *);
        virtual void visit(const Min *);
        virtual void visit(const Max *);
        virtual void visit(const EQ *);
        virtual void visit(const NE *);
        virtual void visit(const LT *);
        virtual void visit(const LE *);
        virtual void visit(const GT *);
        virtual void visit(const GE *);
        virtual void visit(const And *);
        virtual void visit(const Or *);
        virtual void visit(const Not *);
        virtual void visit(const Select *);
        virtual void visit(const Load *);
        virtual void visit(const Ramp *);
        virtual void visit(const Call *);
        virtual void visit(const Let *);
        virtual void visit(const LetStmt *);
        virtual void visit(const PrintStmt *);
        virtual void visit(const AssertStmt *);
        virtual void visit(const Pipeline *);
        virtual void visit(const For *);
        virtual void visit(const Store *);
        virtual void visit(const Provide *);
        virtual void visit(const Allocate *);
        virtual void visit(const Realize *);
        virtual void visit(const Block *);
    };
    
}

#endif
