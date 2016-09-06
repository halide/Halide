// Copyright Jim Bosch 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define HALIDE_NUMPY_INTERNAL_MAIN
#include "dtype.hpp"
#include "internal.hpp"

namespace Halide {
namespace numpy {

#if PY_MAJOR_VERSION == 2
static void wrap_import_array() {
    import_array();
}
#else
static void *wrap_import_array() {
    import_array();
    return nullptr;
}
#endif

void initialize(bool register_scalar_converters) {
    wrap_import_array();
    import_ufunc();
    if (register_scalar_converters)
        dtype::register_scalar_converters();
}
}
}
