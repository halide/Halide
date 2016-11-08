// Copyright Jim Bosch 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef HALIDE_NUMPY_INTERNAL_HPP_INCLUDED
#define HALIDE_NUMPY_INTERNAL_HPP_INCLUDED

/**
 *  @file numpy/internal.hpp
 *  @brief Internal header file to include the Numpy C-API headers.
 *
 *  This should only be included by source files in the boost.numpy library itself.
 */

#include <boost/python.hpp>
#ifdef HALIDE_NUMPY_INTERNAL
#define NO_IMPORT_ARRAY
#define NO_IMPORT_UFUNC
#else
#ifndef HALIDE_NUMPY_INTERNAL_MAIN
ERROR_internal_hpp_is_for_internal_use_only
#endif
#endif
#define PY_ARRAY_UNIQUE_SYMBOL HALIDE_NUMPY_ARRAY_API
#define PY_UFUNC_UNIQUE_SYMBOL HALIDE_UFUNC_ARRAY_API
#include "numpy.hpp"
#include <numpy/arrayobject.h>
#include <numpy/ufuncobject.h>

#define NUMPY_OBJECT_MANAGER_TRAITS_IMPL(pytype, manager) \
    PyTypeObject const *object_manager_traits<manager>::get_pytype() { return &pytype; }

#endif  // !HALIDE_NUMPY_INTERNAL_HPP_INCLUDED
