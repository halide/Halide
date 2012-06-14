
#include "py_util.h"

void assign(Func &f, const Expr &e) {
  f = e;
}

Expr add(Expr a, Expr b) { return a+b; }
Expr sub(Expr a, Expr b) { return a-b; }
Expr neg(Expr a) { return -a; }
Expr mul(Expr a, Expr b) { return a*b; }
Expr div(Expr a, Expr b) { return a/b; }
Expr mod(Expr a, Expr b) { return a%b; }

Expr lt(Expr a, Expr b) { return a < b; }
Expr le(Expr a, Expr b) { return a <= b; }
Expr eq(Expr a, Expr b) { return a == b; }
Expr ne(Expr a, Expr b) { return a != b; }
Expr gt(Expr a, Expr b) { return a > b; }
Expr ge(Expr a, Expr b) { return a >= b; }

Expr and_op(Expr a, Expr b) { return a&&b; }
Expr or_op(Expr a, Expr b) { return a||b; }
Expr invert(Expr a) { return !a; }

Expr iadd(Expr &a, Expr b) { a += b; return a; }
Expr isub(Expr &a, Expr b) { a -= b; return a; }
Expr imul(Expr &a, Expr b) { a *= b; return a; }
Expr idiv(Expr &a, Expr b) { a /= b; return a; }

FuncRef call(Func &a, Expr b) { return a(b); }
FuncRef call(Func &a, Expr b, Expr c) { return a(b, c); }
FuncRef call(Func &a, Expr b, Expr c, Expr d) { return a(b, c, d); }
FuncRef call(Func &a, Expr b, Expr c, Expr d, Expr e) { return a(b, c, d, e); }
FuncRef call(Func &a, const std::vector<Expr> &args) { return a(args); }

Expr call(const UniformImage &a, Expr b) { return a(b); }
Expr call(const UniformImage &a, Expr b, Expr c) { return a(b, c); }
Expr call(const UniformImage &a, Expr b, Expr c, Expr d) { return a(b, c, d); }
Expr call(const UniformImage &a, Expr b, Expr c, Expr d, Expr e) { return a(b, c, d, e); }

void assign(FuncRef &a, Expr b) { a = b; }

void test_blur() {
  UniformImage input(UInt(16), 2, "inputimage");
  Func blur_x("blur_x"), blur_y("blur_y");
  Var x("x"), y("y");

  // The algorithm
  blur_x(x, y) = (input(x-1, y) + input(x, y) + input(x+1, y))/3;
  blur_y(x, y) = (blur_x(x, y-1) + blur_x(x, y) + blur_x(x, y+1))/3;
  
  blur_y.compileToFile("halide_blur");
}


int main() {
    test_blur();
    return 0;
}

