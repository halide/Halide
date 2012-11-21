#ifndef IR_MUTATOR_H
#define IR_MUTATOR_H

#include "IRVisitor.h"
#include "IR.h"

namespace HalideInternal {
    // Override the ones you care about
    class IRMutator : public IRVisitor {
    private:
        Expr *mutate(Expr *expr) {
            expr->visit(this);
            return result;
        };

        template<typename T> void mutateBinaryOperator(T *op) {
            Expr *a = mutate(op->a);
            Expr *b = mutate(op->b);
            if (a == op->a && 
                b == op->b) result = op;
            else result = new T(a, b);            
        }

    public:
        IR *result;

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

        virtual void visit(Not *op) {
            Expr *a = mutate(op->a);
            if (a == op->a) result = a;
            else result = new Not(a);
        };

        virtual void visit(Select *op)  {
            Expr *cond = mutate(op->condition);
            Expr *t = mutate(op->true_value);
            Expr *f = mutate(op->false_value);
            if (cond == op->condition && 
                t == op->true_value && 
                f == op->false_value) result = op;
            else result = new Select(cond, t, f);
        }

        virtual void visit(Load *op) {
            Expr *index = mutate(op->index);
            if (index == op->index) result = op;
            else result = new Load(op->type, op->buffer, index);
        }

        virtual void visit(Ramp *op) {
            Expr *base = op->base, stride = op->stride;
            base->visit(this); base = result;
            stride->visit(this); stride = result;
            if (base == op->base && stride == op->stride) result = op;
            else result = new Ramp(base, stride, op->width);
        }

        virtual void visit(Call *op) {
            std::vector<Expr *> new_args(op->args.size());
            bool changed = false;

            // Mutate the args
            for (size_t i = 0; i < op->args.size(); i++) {
                Expr *old_arg = op->args[i];
                Expr *new_arg = mutate(old_arg);
                if (new_arg != old_arg) changed = true;
                new_args[i] = new_arg;
            }

            if (!changed) result = op;
            else result = new Call(op->name, op->type, new_args, op->call_type);
        }

        virtual void visit(
    };    
}

#endif
