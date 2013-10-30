
/* Python naive wrapper for Halide C++ implementation, resulting in module cHalide. */

%module(naturalvar=1) cHalide
%include "stdint.i"

%{
#include "Halide.h"
#include "py_util.h"
using namespace Halide;
%}

namespace Halide {
%ignore Internal;
}

%include "std_string.i"
%include "std_vector.i"

%naturalvar;
%naturalvar Func;
%naturalvar Expr;

%include "Halide.h"
%include "py_util.h"

%template(Image_uint8) Image<uint8_t>;
%template(Image_uint16) Image<uint16_t>;
%template(Image_uint32) Image<uint32_t>;
%template(Image_int8) Image<int8_t>;
%template(Image_int16) Image<int16_t>;
%template(Image_int32) Image<int32_t>;
%template(Image_float32) Image<float>;
%template(Image_float64) Image<double>;

%template(Param_uint8) Param<uint8_t>;
%template(Param_uint16) Param<uint16_t>;
%template(Param_uint32) Param<uint32_t>;
%template(Param_int8) Param<int8_t>;
%template(Param_int16) Param<int16_t>;
%template(Param_int32) Param<int32_t>;
%template(Param_float32) Param<float>;
%template(Param_float64) Param<double>;

//%template(Image_uint64) Image<uint64_t>;
//%template(Image_int64) Image<int64_t>;

namespace std {
   %template(ListExpr) vector<Expr>;
   %template(ListVar) vector<Var>;
   %template(ListFunc) vector<Func>;
   %template(ListInt) vector<int>;
   %template(ListString) vector<std::string>;
   %template(ListArgument) vector<Argument>;
   %template(ListBuffer) vector<Buffer>;
};
