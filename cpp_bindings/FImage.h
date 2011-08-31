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
}


#endif
