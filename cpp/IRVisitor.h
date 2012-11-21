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

    struct IRVisitor {
        virtual void visit(IntImm *) = 0;
        virtual void visit(FloatImm *) = 0;
        virtual void visit(Cast *) = 0;
        virtual void visit(Var *) = 0;
        virtual void visit(Add *) = 0;
        virtual void visit(Sub *) = 0;
        virtual void visit(Mul *) = 0;
        virtual void visit(Div *) = 0;
        virtual void visit(Mod *) = 0;
        virtual void visit(Min *) = 0;
        virtual void visit(Max *) = 0;
        virtual void visit(EQ *) = 0;
        virtual void visit(NE *) = 0;
        virtual void visit(LT *) = 0;
        virtual void visit(LE *) = 0;
        virtual void visit(GT *) = 0;
        virtual void visit(GE *) = 0;
        virtual void visit(And *) = 0;
        virtual void visit(Or *) = 0;
        virtual void visit(Not *) = 0;
        virtual void visit(Select *) = 0;
        virtual void visit(Load *) = 0;
        virtual void visit(Ramp *) = 0;
        virtual void visit(Call *) = 0;
        virtual void visit(Let *) = 0;
        virtual void visit(LetStmt *) = 0;
        virtual void visit(PrintStmt *) = 0;
        virtual void visit(AssertStmt *) = 0;
        virtual void visit(Pipeline *) = 0;
        virtual void visit(For *) = 0;
        virtual void visit(Store *) = 0;
        virtual void visit(Provide *) = 0;
        virtual void visit(Allocate *) = 0;
        virtual void visit(Realize *) = 0;

        virtual void test() = 0;
    };
    
}

#endif
