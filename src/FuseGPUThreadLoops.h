#ifndef HALIDE_SYNCTHREADS_H
#define HALIDE_SYNCTHREADS_H

/** \file
 * Defines the lowering pass that fuses and normalizes loops over gpu
 * threads to target OpenCL and CUDA.
 */

#include "IR.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

// Rewrite all GPU loops to have a min of zero
class ZeroGPULoopMins : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op);
};

/** Converts Halide's GPGPU IR to the OpenCL/CUDA model. Within every
 * loop over gpu block indices, fuse the inner loops over thread
 * indices into a single loop (with predication to turn off
 * threads). Also injects synchronization points as needed, and hoists
 * allocations at the block level out into a single shared memory
 * array. */
Stmt fuse_gpu_thread_loops(Stmt s);

}
}

#endif
