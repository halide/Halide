#include <string>

namespace HalideInternal {
    class Type {
        enum {Int, UInt, Float} t;
        int bits;
        int width;        
    };

    struct IntImm;
    struct FloatImm;
    struct Cast;
    struct Var;
    struct Bop;
    struct Cmp;

    struct IRVisitor {
        virtual void visit(IntImm *);
        virtual void visit(FloatImm *);
        virtual void visit(Cast *);
        virtual void visit(Var *);
        virtual void visit(Add *);
        virtual void visit(Sub *);
        virtual void visit(Mul *);
        virtual void visit(Div *);
        virtual void visit(Mod *);
        virtual void visit(Min *);
        virtual void visit(Max *);
    };

    struct Expr {
        virtual void visit(IRVisitor *v) = 0;
        int gc_mark, gc_ref_count;
    };

    struct IntImm : public Expr {
        int val;

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct FloatImm : public Expr {
        float val;
        
        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Cast : public Expr {
        Type t;
        Expr *val;

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Var : public Expr {
        Type t;
        std::string name;

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Add : public Expr {
        Expr *a, *b;

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct EQ : public Expr {
        Expr *a, *b;

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Load : public Expr {
        Type t;
        std::string buffer;
        Expr *val;

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Ramp : public Expr {
        Expr *a, *b;
        int n;

        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct Call : public Expr {
        Type t;
        std::string buffer;
        std::list<Expr *> args;
    };

    struct ImageCall : public Call {
        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct ExternCall : public Call {
        void visit(IRVisitor *v) {v->visit(this);}
    };

    struct FunctionCall : public Call {
        void visit(IRVisitor *v) {v->visit(this);}
    };

    // Override the ones you care about
    class IRMutator : public IRVisitor {
    private:
        template<typename T> void mutateUnaryOperator(T *op) {
            Expr *a = op->val;
            a->visit(this); a = result;
            if (a == op->val) result = op;
            else result = new T(a);            
        }

        template<typename T> void mutateBinaryOperator(T *op) {
            Expr *a = op->a, b = op->b;
            a->visit(this); a = result;
            b->visit(this); b = result;
            if (a == op->a && b == op->b) result = op;
            else result = new T(a, b);            
        }

        template<typename T> void mutateTernaryOperator(T *op) {
            Expr *a = op->a, b = op->b, c = op->c;
            a->visit(this); a = result;
            b->visit(this); b = result;
            c->visit(this); c = result;
            if (a == op->a && b == op->b && c == op->c) result = op;
            else result = new T(a, b, c);
        }

    public:
        Expr *result;

        virtual void visit(IntImm *v)   {result = v;}
        virtual void visit(FloatImm *v) {result = v;}
        virtual void visit(Add *op)     {mutateBinaryOperator(op);}
        virtual void visit(Sub *op)     {mutateBinaryOperator(op);}
        virtual void visit(Mul *op)     {mutateBinaryOperator(op);}
        virtual void visit(Div *op)     {mutateBinaryOperator(op);}
        virtual void visit(Mod *op)     {mutateBinaryOperator(op);}
        virtual void visit(Min *op)     {mutateBinaryOperator(op);}
        virtual void visit(Max *op)     {mutateBinaryOperator(op);}
        virtual void visit(EQ *op)      {mutateBinaryOperator(op);}
        virtual void visit(NE *op)      {mutateBinaryOperator(op);}
        virtual void visit(LT *op)      {mutateBinaryOperator(op);}
        virtual void visit(LE *op)      {mutateBinaryOperator(op);}
        virtual void visit(GT *op)      {mutateBinaryOperator(op);}
        virtual void visit(GE *op)      {mutateBinaryOperator(op);}
        virtual void visit(And *op)     {mutateBinaryOperator(op);}
        virtual void visit(Or *op)      {mutateBinaryOperator(op);}
        virtual void visit(Not *op)     {mutateUnaryOperator(op);}
        virtual void visit(Select *op)  {mutateTernaryOperator(op);}
        virtual void visit(Load *op)    {mutateUnaryOperator(op);}
        virtual void visit(Ramp *op)    {mutateBinaryOperator(op);}
    };
}
