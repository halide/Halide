
#include "py_util.h"
#include "png_util.h"
#include <signal.h>
#include <string>
#include "Python.h"
#include "Python/frameobject.h"

void (*signal(int signum, void (*sighandler)(int)))(int);

void assign(Func &f, const Expr &e) {
  f = e;
}

Expr expr_from_tuple(Expr a) { return Expr(Tuple(a)); }
Expr expr_from_tuple(Expr a, Expr b) { return Expr(Tuple(a, b)); }
Expr expr_from_tuple(Expr a, Expr b, Expr c) { return Expr(Tuple(a, b, c)); }
Expr expr_from_tuple(Expr a, Expr b, Expr c, Expr d) { return Expr(Tuple(a, b, c, d)); }

Expr expr_from_int(int a) { return Expr(a); }

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

Expr call(const DynImage &a, Expr b) { return a(b); }
Expr call(const DynImage &a, Expr b, Expr c) { return a(b, c); }
Expr call(const DynImage &a, Expr b, Expr c, Expr d) { return a(b, c, d); }
Expr call(const DynImage &a, Expr b, Expr c, Expr d, Expr e) { return a(b, c, d, e); }

void assign(DynUniform &a, int b) { a.set(b); }
void assign(DynUniform &a, double b) { a.set(b); }

void assign(FuncRef &a, Expr b) { a = b; }
void assign(UniformImage &a, const DynImage &b) { a = b; }

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
 	//PyErr_SetString(PyExc_ValueError,"Trapped signal in C++ layer, exiting");
    printf("\n");
    PyThreadState *tstate = PyThreadState_GET();
    if (NULL != tstate && NULL != tstate->frame) {
        PyFrameObject *frame = tstate->frame;

        printf("Python stack trace:\n");
        while (NULL != frame) {
            int line = frame->f_lineno;
            const char *filename = PyString_AsString(frame->f_code->co_filename);
            const char *funcname = PyString_AsString(frame->f_code->co_name);
            printf("    %s(%d): %s\n", filename, line, funcname);
            frame = frame->f_back;
        }
    }
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

#define DEFINE_TYPE(T) \
Expr call(Image<T> &a, Expr b) { return a(b); } \
Expr call(Image<T> &a, Expr b, Expr c) { return a(b,c); } \
Expr call(Image<T> &a, Expr b, Expr c, Expr d) { return a(b,c,d); }                     \
Expr call(Image<T> &a, Expr b, Expr c, Expr d, Expr e) { return a(b,c,d,e); }                   
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

Expr minimum_func(const Expr &a) { return minimum(a); }
Expr maximum_func(const Expr &a) { return maximum(a); }
Expr product_func(const Expr &a) { return product(a); }
Expr sum_func(const Expr &a) { return sum(a); }

void iadd(FuncRef &f, const Expr &e) { f += e; }
void imul(FuncRef &f, const Expr &e) { f *= e; }

//void assign(UniformImage &a, Image<uint8_t> b) { a = DynImage(b); }

#define DEFINE_TYPE(T) \
void assign_array(Image<T> &a, size_t base, size_t xstride) { \
    for (int x = 0; x < a.size(0); x++) { \
        a(x) = *(T*)(((void *) base) + (xstride*x)); \
    } \
} \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride) { \
    for (int x = 0; x < a.size(0); x++) { \
    for (int y = 0; y < a.size(1); y++) { \
        a(x,y) = *(T*)(((void *) base) + (xstride*x) + (ystride*y)); \
    } \
    } \
} \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride, size_t zstride) { \
    for (int x = 0; x < a.size(0); x++) { \
    for (int y = 0; y < a.size(1); y++) { \
    for (int z = 0; z < a.size(2); z++) { \
        a(x,y,z) = *(T*)(((void *) base) + (xstride*x) + (ystride*y) + (zstride*z)); \
    } \
    } \
    } \
} \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride, size_t zstride, size_t wstride) { \
    for (int x = 0; x < a.size(0); x++) { \
    for (int y = 0; y < a.size(1); y++) { \
    for (int z = 0; z < a.size(2); z++) { \
    for (int w = 0; w < a.size(3); w++) { \
        a(x,y,z,w) = *(T*)(((void *) base) + (xstride*x) + (ystride*y) + (zstride*z) + (wstride*w)); \
    } \
    } \
    } \
    } \
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

