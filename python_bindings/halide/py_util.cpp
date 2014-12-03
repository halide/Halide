/* -----------------------------------------------------------------------
   Utility functions for Halide Python bindings.
   ----------------------------------------------------------------------- */

#include "py_util.h"
#include "../../apps/support/image_io.h"
#include <signal.h>
#include <string>
#include "Python.h"
#include "frameobject.h"

void (*signal(int signum, void (*sighandler)(int)))(int);

Expr cast_to_expr(int a) { return Expr(a); }
Expr cast_to_expr(float a) { return Expr(a); }
Expr cast_to_expr(const Func &f) { return Expr(f); }
Expr cast_to_expr(const FuncRefVar &f) { return Expr(f); }
Expr cast_to_expr(const FuncRefExpr &f) { return Expr(f); }
Expr cast_to_expr(Var v) { return Expr(v); }
Expr cast_to_expr(Expr e) { return e; }
Expr cast_to_expr(RVar r) { return Expr(r); }
Expr cast_to_expr(RDom r) { return Expr(r); }
#define DEFINE_TYPE(T) Expr cast_to_expr(const Image<T> &I) { return Expr(I); }
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) Expr cast_to_expr(const Param<T> &v) { return Expr(v); }
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

Expr cast_to_expr(const ImageParam &I) { return Expr(I); }

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

FuncRefExpr call(Func &a, Expr b) { return a(b); }
FuncRefExpr call(Func &a, Expr b, Expr c) { return a(b, c); }
FuncRefExpr call(Func &a, Expr b, Expr c, Expr d) { return a(b, c, d); }
FuncRefExpr call(Func &a, Expr b, Expr c, Expr d, Expr e) { return a(b, c, d, e); }
FuncRefExpr call(Func &a, const std::vector<Expr> &args) { return a(args); }

FuncRefVar call(Func &a, const std::vector<Var> &args) { return a(args); }
FuncRefVar call(Func &a, Var b) { return a(b); }
FuncRefVar call(Func &a, Var b, Var c) { return a(b, c); }
FuncRefVar call(Func &a, Var b, Var c, Var d) { return a(b, c, d); }
FuncRefVar call(Func &a, Var b, Var c, Var d, Var e) { return a(b, c, d, e); }

Expr call(const ImageParam &a, Expr b) { return a(b); }
Expr call(const ImageParam &a, Expr b, Expr c) { return a(b, c); }
Expr call(const ImageParam &a, Expr b, Expr c, Expr d) { return a(b, c, d); }
Expr call(const ImageParam &a, Expr b, Expr c, Expr d, Expr e) { return a(b, c, d, e); }

void set(FuncRefExpr &a, Expr b) { a = b; }
void set(FuncRefVar &a, Expr b) { a = b; }
void set(ImageParam &a, const Buffer &b) { a.set(b); }

#define DEFINE_TYPE(T) void set(ImageParam &a, Image<T> b) { a.set(b); }
#include "expand_types.h"
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) void set(Image<T> &a, Buffer b) { a = b; }
#include "expand_types.h"
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) \
void set(Param<T> &a, int b) { a.set(b); } \
void set(Param<T> &a, double b) { a.set(T(b)); }
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
            const char *filename = PyBytes_AsString(frame->f_code->co_filename);
            const char *funcname = PyBytes_AsString(frame->f_code->co_name);
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
    Buffer d(a); \
    const size_t size = (d.type().bits/8) * d.stride(dims-1) * a.extent(dims-1); \
    return std::string((char *) a.data(), size); \
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

#define DEFINE_TYPE(T) Buffer to_buffer(const Image<T> &a) { return Buffer(a); }
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

void iadd(FuncRefVar &f, const Expr &e) { f += e; }
void imul(FuncRefVar &f, const Expr &e) { f *= e; }
void iadd(FuncRefExpr &f, const Expr &e) { f += e; }
void imul(FuncRefExpr &f, const Expr &e) { f *= e; }

//void set(UniformImage &a, Image<uint8_t> b) { a = DynImage(b); }

#define DEFINE_TYPE(T) \
void assign_array(Image<T> &a, size_t base, size_t xstride) { \
    for (int x = 0; x < a.extent(0); x++) { \
        a(x) = *(T*)(((uint8_t *) base) + (xstride*x)); \
    } \
} \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride) { \
    for (int x = 0; x < a.extent(0); x++) { \
    for (int y = 0; y < a.extent(1); y++) { \
        a(x,y) = *(T*)(((uint8_t *) base) + (xstride*x) + (ystride*y)); \
    } \
    } \
} \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride, size_t zstride) { \
    for (int x = 0; x < a.extent(0); x++) { \
    for (int y = 0; y < a.extent(1); y++) { \
    for (int z = 0; z < a.extent(2); z++) { \
        a(x,y,z) = *(T*)(((uint8_t *) base) + (xstride*x) + (ystride*y) + (zstride*z)); \
    } \
    } \
    } \
} \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride, size_t zstride, size_t wstride) { \
    for (int x = 0; x < a.extent(0); x++) { \
    for (int y = 0; y < a.extent(1); y++) { \
    for (int z = 0; z < a.extent(2); z++) { \
    for (int w = 0; w < a.extent(3); w++) { \
        a(x,y,z,w) = *(T*)(((uint8_t *) base) + (xstride*x) + (ystride*y) + (zstride*z) + (wstride*w)); \
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

