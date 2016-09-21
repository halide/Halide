// Copyright Jim Bosch 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef HALIDE_NUMPY_HPP_INCLUDED
#define HALIDE_NUMPY_HPP_INCLUDED

/**
 *  @file numpy/numpy.hpp
 *  @brief Main public header file for boost.numpy.
 */

#include "dtype.hpp"
#include "ndarray.hpp"

namespace Halide {
namespace numpy {

/**
 *  @brief Initialize the Numpy C-API
 *
 *  This must be called before using anything in boost.numpy;
 *  It should probably be the first line inside BOOST_PYTHON_MODULE.
 *
 *  @internal This just calls the Numpy C-API functions "import_array()"
 *            and "import_ufunc()", and then calls
 *            dtype::register_scalar_converters().
 */
void initialize(bool register_scalar_converters = true);

}  // namespace Halide::numpy
}  // namespace Halide

#endif  // !HALIDE_NUMPY_HPP_INCLUDED
