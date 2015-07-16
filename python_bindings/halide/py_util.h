/* -----------------------------------------------------------------------
   Utility functions for Halide Python bindings.
   ----------------------------------------------------------------------- */

#ifndef _py_util_h
#define _py_util_h

#include "Halide.h"
#include <vector>

using namespace Halide;

void iadd(FuncRefVar &f, const Expr &e);
void imul(FuncRefVar &f, const Expr &e);
void iadd(FuncRefExpr &f, const Expr &e);
void imul(FuncRefExpr &f, const Expr &e);
Expr add(Expr a, Expr b);
Expr sub(Expr a, Expr b);
Expr neg(Expr a);
Expr mul(Expr a, Expr b);
Expr div(Expr a, Expr b);
Expr mod(Expr a, Expr b);

Expr cast_to_expr(int a);
Expr cast_to_expr(float a);
Expr cast_to_expr(const Func &f);
Expr cast_to_expr(const FuncRefVar &f);
Expr cast_to_expr(const FuncRefExpr &f);
Expr cast_to_expr(Var v);
Expr cast_to_expr(Expr e);
Expr cast_to_expr(RVar r);
Expr cast_to_expr(RDom r);
#define DEFINE_TYPE(T) Expr cast_to_expr(const Image<T> &I);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) Expr cast_to_expr(const Param<T> &v);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

Expr cast_to_expr(const ImageParam &I);

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

Expr minimum_func(const Expr &a);
Expr maximum_func(const Expr &a);
Expr product_func(const Expr &a);
Expr sum_func(const Expr &a);

FuncRefExpr call(Func &a, const std::vector<Expr> &args);
FuncRefExpr call(Func &a, Expr b);
FuncRefExpr call(Func &a, Expr b, Expr c);
FuncRefExpr call(Func &a, Expr b, Expr c, Expr d);
FuncRefExpr call(Func &a, Expr b, Expr c, Expr d, Expr e);
FuncRefVar call(Func &a, const std::vector<Var> &args);
FuncRefVar call(Func &a, Var b);
FuncRefVar call(Func &a, Var b, Var c);
FuncRefVar call(Func &a, Var b, Var c, Var d);
FuncRefVar call(Func &a, Var b, Var c, Var d, Var e);

Expr call(const ImageParam &a, Expr b);
Expr call(const ImageParam &a, Expr b, Expr c);
Expr call(const ImageParam &a, Expr b, Expr c, Expr d);
Expr call(const ImageParam &a, Expr b, Expr c, Expr d, Expr e);

#define DEFINE_TYPE(T) \
Expr call(Image<T> &a, Expr b);                             \
Expr call(Image<T> &a, Expr b, Expr c);                     \
Expr call(Image<T> &a, Expr b, Expr c, Expr d);                     \
Expr call(Image<T> &a, Expr b, Expr c, Expr d, Expr e);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

void set(FuncRefVar &a, Expr b);
void set(FuncRefExpr &a, Expr b);
void set(ImageParam &a, const Buffer &b);

#define DEFINE_TYPE(T) void set(ImageParam &a, Image<T> b);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) void set(Image<T> &a, Buffer b);
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
void set(Param<T> &a, int b); \
void set(Param<T> &a, double b);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) Image<T> load_png(Image<T> a, std::string b);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) void save_png(Image<T> a, std::string b);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

void exit_on_signal();

#define DEFINE_TYPE(T) std::string image_to_string(const Image<T> &a);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) Buffer to_buffer(const Image<T> &a);
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
void assign_array(Image<T> &a, size_t base, size_t xstride); \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride); \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride, size_t zstride); \
void assign_array(Image<T> &a, size_t base, size_t xstride, size_t ystride, size_t zstride, size_t wstride);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#endif

