#ifndef HALIDE_WRAP_EXTERN_STAGES_H
#define HALIDE_WRAP_EXTERN_STAGES_H

#include "Module.h"

/** \file
 *
 * Defines a pass over a Module that adds wrapper LoweredFuncs to any
 * extern stages that need them */

namespace Halide {
namespace Internal {

/** Add wrappers for any LoweredFuncs that need them. This currently
 * wraps extern calls to stages that expect the old buffer_t type. */
void wrap_extern_stages(Module m);

}
}

#endif
