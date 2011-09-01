#ifndef FIMAGE_H
#define FIMAGE_H

#include "MLVal.h"

namespace FImage {

    class Expr {
      public:
        Expr(int);
        Expr(MLVal);
        MLVal val;
        Expr operator+(const Expr &b);

        // TODO: eventually this will only exist for top-levels
        mutable void (*function_ptr)(void *);
    };



    class Var : public Expr {
      public:
        Var();
        Var(const char *str);
    };

    Expr Add(const Expr &, const Expr &);
    Expr IntImm(int);
    //Expr Var(const char *c);
    void print(const Expr &);

    void run(const Expr &stmt, void *args);
    Expr Load(int buf, const Expr &idx);
    Expr Store(const Expr &val, int buf, const Expr &idx);
    Expr renumber(int oldn, int newn, const Expr &e);
}


#endif
