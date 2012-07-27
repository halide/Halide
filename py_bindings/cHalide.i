%module(naturalvar=1) cHalide
%{
#include "Func.h"
#include "Expr.h"
#include "Var.h"
#include "Image.h"
#include "MLVal.h"
#include "Reduction.h"
//#include "Tuple.h"
#include "Type.h"
#include "py_util.h"
#include "environ_fix.h"
using namespace Halide;
%}

%include "std_string.i"
%include "std_vector.i"

%naturalvar;
%naturalvar Func;
%naturalvar Expr;

%include "Func.h"
%include "Expr.h"
%include "Var.h"
%include "Image.h"
%include "MLVal.h"
%include "Reduction.h"
%include "Type.h"
%include "Util.h"
%include "py_util.h"
%include "environ_fix.h"
%include "Uniform.h"

//%include "Tuple.h"

%template(Image_uint8) Image<uint8_t>;
%template(Image_uint16) Image<uint16_t>;
%template(Image_uint32) Image<uint32_t>;
%template(Image_int8) Image<int8_t>;
%template(Image_int16) Image<int16_t>;
%template(Image_int32) Image<int32_t>;
%template(Image_float32) Image<float>;
%template(Image_float64) Image<double>;

%template(Uniform_uint8) Uniform<uint8_t>;
%template(Uniform_uint16) Uniform<uint16_t>;
%template(Uniform_uint32) Uniform<uint32_t>;
%template(Uniform_int8) Uniform<int8_t>;
%template(Uniform_int16) Uniform<int16_t>;
%template(Uniform_int32) Uniform<int32_t>;
%template(Uniform_float32) Uniform<float>;
%template(Uniform_float64) Uniform<double>;

//%template(Image_uint64) Image<uint64_t>;
//%template(Image_int64) Image<int64_t>;

namespace std {
   %template(ListExpr) vector<Expr>;
   %template(ListVar) vector<Var>;
   %template(ListDynUniform) vector<DynUniform>;
   %template(ListDynImage) vector<DynImage>;
   %template(ListFunc) vector<Func>;
   %template(ListUniformImage) vector<UniformImage>;
   %template(ListMLVal) vector<MLVal>;
   %template(ListInt) vector<int>;
};

   //%template(ListArg) vector<Arg>;
