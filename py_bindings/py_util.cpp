
#include "py_util.h"
#include "png_util.h"
#include <signal.h>
#include <string>

void (*signal(int signum, void (*sighandler)(int)))(int);

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

#define DEFINE_TYPE(T) void assign(UniformImage &a, Image<T> b) { a = b; }
#include "expand_types.h"
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) void assign(Image<T> &a, DynImage b) { a = b; }
#include "expand_types.h"
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) \
void assign(Uniform<T> &a, int b) { a = b; } \
void assign(Uniform<T> &a, double b) { a = b; }
#include "expand_types.h"
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) Image<T> load_png(Image<T> a, std::string b) { return load<T>(b); }
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
//#include "expand_types.h"
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) void save_png(Image<T> a, std::string b) { save(a, b); }
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

void signal_handler(int sig_num) {
    printf("Trapped signal %d in C++ layer, exiting\n", sig_num);
    exit(0);
}

void exit_on_signal() {
    signal(SIGINT , signal_handler);
    signal(SIGABRT , signal_handler);
    signal(SIGILL , signal_handler);
    signal(SIGFPE , signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM , signal_handler);
    signal(SIGBUS, signal_handler);
}

#define DEFINE_TYPE(T) \
std::string image_to_string(const Image<T> &a) { \
    int dims = a.dimensions(); \
    DynImage d(a); \
    return std::string((char *) a.data(), (d.type().bits/8)*d.stride(dims-1)*a.size(dims-1)); \
}
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) DynImage to_dynimage(const Image<T> &a) { return DynImage(a); }
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) DynUniform to_dynuniform(const Uniform<T> &a) { return DynUniform(a); }
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

//void assign(UniformImage &a, Image<uint8_t> b) { a = DynImage(b); }

/*
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
*/
