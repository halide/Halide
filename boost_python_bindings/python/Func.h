#ifndef FUNC_H
#define FUNC_H

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include <boost/python/tuple.hpp>
#include <vector>
#include <string>

#include "../../src/Var.h"
#include "../../src/Expr.h"

void defineFunc();

void tuple_to_var_expr_vector(
        const std::string debug_name,
        const boost::python::tuple &args_passed,
        std::vector<Halide::Var> &var_args,
        std::vector<Halide::Expr> &expr_args);


#endif // FUNC_H
