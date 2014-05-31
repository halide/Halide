#ifndef HALIDE_SYNCTHREADS_H
#define HALIDE_SYNCTHREADS_H

/** \file
 * TODO
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** TODO */
Stmt fuse_gpu_thread_loops(Stmt s);

}
}

#endif
