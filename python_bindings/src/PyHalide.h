#ifndef HALIDE_PYTHON_BINDINGS_PYHALIDE_H
#define HALIDE_PYTHON_BINDINGS_PYHALIDE_H

// Include all Boost.Python headers here
#include <boost/python.hpp>
#include <boost/python/operators.hpp>
#include <boost/python/raw_function.hpp>
#include <boost/python/self.hpp>
#include <boost/python/stl_iterator.hpp>
#include <boost/python/tuple.hpp>

// Some very-commonly-used headers here, to simplify things.
// (<string> must come after the boost headers in some configs.)
#include <iostream>
#include <string>
#include <vector>

// Everyone needs Halide.h
#include "Halide.h"

namespace Halide {
namespace PythonBindings {

namespace py = boost::python;

}  // namespace PythonBindings
}  // namespace Halide


#endif  // HALIDE_PYTHON_BINDINGS_PYHALIDE_H
