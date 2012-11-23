#ifndef IR_VISITOR_H
#define IR_VISITOR_H

namespace HalideInternal {
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

    struct IRVisitor {
        virtual void visit(const IntImm *) = 0;
        virtual void visit(const FloatImm *) = 0;
        virtual void visit(const Cast *) = 0;
        virtual void visit(const Var *) = 0;
        virtual void visit(const Add *) = 0;
        virtual void visit(const Sub *) = 0;
        virtual void visit(const Mul *) = 0;
        virtual void visit(const Div *) = 0;
        virtual void visit(const Mod *) = 0;
        virtual void visit(const Min *) = 0;
        virtual void visit(const Max *) = 0;
        virtual void visit(const EQ *) = 0;
        virtual void visit(const NE *) = 0;
        virtual void visit(const LT *) = 0;
        virtual void visit(const LE *) = 0;
        virtual void visit(const GT *) = 0;
        virtual void visit(const GE *) = 0;
        virtual void visit(const And *) = 0;
        virtual void visit(const Or *) = 0;
        virtual void visit(const Not *) = 0;
        virtual void visit(const Select *) = 0;
        virtual void visit(const Load *) = 0;
        virtual void visit(const Ramp *) = 0;
        virtual void visit(const Call *) = 0;
        virtual void visit(const Let *) = 0;
        virtual void visit(const LetStmt *) = 0;
        virtual void visit(const PrintStmt *) = 0;
        virtual void visit(const AssertStmt *) = 0;
        virtual void visit(const Pipeline *) = 0;
        virtual void visit(const For *) = 0;
        virtual void visit(const Store *) = 0;
        virtual void visit(const Provide *) = 0;
        virtual void visit(const Allocate *) = 0;
        virtual void visit(const Realize *) = 0;
        virtual void visit(const Block *) = 0;

        virtual void test() = 0;
    };
    
}

#endif
