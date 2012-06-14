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
using namespace Halide;
%}

%include "std_string.i"
%include "std_vector.i"
%naturalvar;
%naturalvar Func;
%naturalvar Expr;

//%rename(add_expr) operator+(Expr, Expr);

%include "Func.h"
%include "Expr.h"
%include "Var.h"
%include "Image.h"
%include "MLVal.h"
%include "Reduction.h"
//%include "Tuple.h"
%include "Type.h"
%include "py_util.h"

//%extend Expr {
//    Expr __add__(Expr *other) {
//        return $self + *other;
//    }
//};
