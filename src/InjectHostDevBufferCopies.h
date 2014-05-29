#ifndef HALIDE_HOST_GPU_BUFFER_COPIES_H
#define HALIDE_HOST_GPU_BUFFER_COPIES_H

/** \file
 * Defines the lowering passes that deal with host and device buffer flow.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Inject calls to halide_dev_malloc, halide_copy_to_dev, and
 * halide_copy_to_host as needed. */
Stmt inject_host_dev_buffer_copies(Stmt s);

/** Inject calls to halide_dev_free as needed. */
Stmt inject_dev_frees(Stmt s);

}
}

#endif
