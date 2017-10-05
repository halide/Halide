#ifndef EXPR_H
#define EXPR_H

#include <boost/python.hpp>
#include "Halide.h"
#include <vector>

void defineExpr();

boost::python::object expr_vector_to_python_tuple(const std::vector<Halide::Expr> &t);
std::vector<Halide::Expr> python_tuple_to_expr_vector(const boost::python::object &obj);

template <typename T>
std::vector<T> python_collection_to_vector(const boost::python::object &obj) {
    std::vector<T> result;
    for (ssize_t i = 0; i < boost::python::len(obj); i++) {
        result.push_back(boost::python::extract<T>(obj[i]));
    }
    return result;
}

#endif  // EXPR_H
