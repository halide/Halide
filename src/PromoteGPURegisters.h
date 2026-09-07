#ifndef HALIDE_PROMOTE_GPU_REGISTERS_H
#define HALIDE_PROMOTE_GPU_REGISTERS_H

/** \file
 *
 * Defines a lowering pass that turns an allocation in register memory outside
 * the loops over GPU threads into one register per thread per site.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** An allocation in MemoryType::Register outside the loops over GPU threads
 * describes storage that is private to a thread, so what looks like one
 * allocation of many elements is really a handful of registers held by each
 * thread. Find the sites it is accessed at, check that a thread's accesses to
 * each one always land on the same elements, and that different sites never
 * share an element. Then give each site registers of its own and move the
 * allocation inside the loops over threads, which is where storage private to
 * a thread belongs.
 *
 * Any two accesses must be provably to the same elements or to none of the same
 * elements, which is what MemoryType::Register asks for: "all stores must be at
 * constant coordinates". A thread that indexes its own storage dynamically gets
 * a user error.
 *
 * Must run after the loops over threads have been fused, so that there is one
 * loop nest for the allocation to move inside of. */
Stmt promote_gpu_registers(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
