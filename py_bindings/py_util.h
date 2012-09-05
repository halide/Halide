#ifndef _py_util_h
#define _py_util_h

#include "Halide.h"
#include <vector>

using namespace Halide;

void assign(Func &f, const Expr &e);
void iadd(FuncRef &f, const Expr &e);
void imul(FuncRef &f, const Expr &e);
Expr add(Expr a, Expr b);
Expr sub(Expr a, Expr b);
Expr neg(Expr a);
Expr mul(Expr a, Expr b);
Expr div(Expr a, Expr b);
Expr mod(Expr a, Expr b);

Expr expr_from_int(int a);

Expr expr_from_tuple(Expr a);
Expr expr_from_tuple(Expr a, Expr b);
Expr expr_from_tuple(Expr a, Expr b, Expr c);
Expr expr_from_tuple(Expr a, Expr b, Expr c, Expr d);

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

FuncRef call(Func &a, const std::vector<Expr> &args);
FuncRef call(Func &a, Expr b);
FuncRef call(Func &a, Expr b, Expr c);
FuncRef call(Func &a, Expr b, Expr c, Expr d);
FuncRef call(Func &a, Expr b, Expr c, Expr d, Expr e);

Expr call(const UniformImage &a, Expr b);
Expr call(const UniformImage &a, Expr b, Expr c);
Expr call(const UniformImage &a, Expr b, Expr c, Expr d);
Expr call(const UniformImage &a, Expr b, Expr c, Expr d, Expr e);

Expr call(const DynImage &a, Expr b);
Expr call(const DynImage &a, Expr b, Expr c);
Expr call(const DynImage &a, Expr b, Expr c, Expr d);
Expr call(const DynImage &a, Expr b, Expr c, Expr d, Expr e);

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

void assign(FuncRef &a, Expr b);
void assign(UniformImage &a, const DynImage &b);

#define DEFINE_TYPE(T) void assign(UniformImage &a, Image<T> b);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) void assign(Image<T> &a, DynImage b);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

void assign(DynUniform &a, int b);
void assign(DynUniform &a, double b);

#define DEFINE_TYPE(T) \
void assign(Uniform<T> &a, int b); \
void assign(Uniform<T> &a, double b);
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

#define DEFINE_TYPE(T) DynImage to_dynimage(const Image<T> &a);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

#define DEFINE_TYPE(T) DynUniform to_dynuniform(const Uniform<T> &a);
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

