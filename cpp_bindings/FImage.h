#ifndef FIMAGE_H
#define FIMAGE_H

#include "MLVal.h"

namespace FImage {
    typedef MLVal Expr;
    Expr Add(const Expr &, const Expr &);
    Expr IntImm(int);
    Expr Var(const char *c);
    void print(const Expr &);
}


#endif
