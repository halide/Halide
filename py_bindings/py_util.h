#ifndef _py_util_h
#define _py_util_h

#include "Halide.h"
#include <vector>

using namespace Halide;

void assign(Func &f, const Expr &e);
Expr add(Expr a, Expr b);
Expr sub(Expr a, Expr b);
Expr neg(Expr a);
Expr mul(Expr a, Expr b);
Expr div(Expr a, Expr b);
Expr mod(Expr a, Expr b);

Expr lt(Expr a, Expr b);
Expr le(Expr a, Expr b);
Expr eq(Expr a, Expr b);
Expr ne(Expr a, Expr b);
Expr gt(Expr a, Expr b);
Expr ge(Expr a, Expr b);

Expr and_op(Expr a, Expr b);
Expr or_op(Expr a, Expr b);
Expr invert(Expr a);

Expr iadd(Expr &a, Expr b);
Expr isub(Expr &a, Expr b);
Expr imul(Expr &a, Expr b);
Expr idiv(Expr &a, Expr b);

FuncRef call(Func &a, const std::vector<Expr> &args);
FuncRef call(Func &a, Expr b);
FuncRef call(Func &a, Expr b, Expr c);
FuncRef call(Func &a, Expr b, Expr c, Expr d);
FuncRef call(Func &a, Expr b, Expr c, Expr d, Expr e);

Expr call(const UniformImage &a, Expr b);
Expr call(const UniformImage &a, Expr b, Expr c);
Expr call(const UniformImage &a, Expr b, Expr c, Expr d);
Expr call(const UniformImage &a, Expr b, Expr c, Expr d, Expr e);

void assign(FuncRef &a, Expr b);

void test_blur();

#endif

