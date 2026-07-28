#ifndef HALIDE_EXTRACT_WMMA_OPERATIONS_H
#define HALIDE_EXTRACT_WMMA_OPERATIONS_H

/** \file
 * Defines the lowering pass that injects calls to the warp-level matrix
 * multiply-accumulate intrinsics that drive NVIDIA tensor cores.
 */

#include "Expr.h"
#include "MultiRamp.h"

namespace Halide {
namespace Internal {

struct Call;
struct Store;

/** Rewrite matrix multiplies that accumulate into WMMAAccumulator memory as
 * calls to the wmma intrinsics understood by the PTX backend, and wrap them in
 * a loop over the 32 lanes of a warp. */
Stmt extract_wmma_operations(const Stmt &s);

/** Whether a Call is one of the wmma intrinsics produced by the pass above. */
bool is_wmma_intrinsic(const Call *op);

/** The index of the argument of a wmma intrinsic that gives the matrix it
 * moves to or from memory, or -1 if it doesn't have one (which is the case for
 * wmma_mma, whose operands are fragments rather than matrices).
 *
 * That argument is a Load of the whole matrix, with its lanes in row-major
 * order regardless of how the matrix is laid out in memory. The layout in
 * memory, and the distance between its rows or columns, are recoverable from
 * the index of that Load: whichever dimension has stride one is the dense one.
 *
 * Being a Load rather than a bare handle to the allocation means that the
 * passes which track uses of an allocation (dead allocation removal, shared
 * memory liveness and packing, closure construction) see the access, and see
 * its true footprint. It is never actually loaded - codegen takes its address
 * - so passes that rewrite the *structure* of a load must leave it alone, or
 * the tile shape will no longer be recoverable. */
int wmma_matrix_arg(const Call *op);

/** Whether a Store is the copy of a tensor core accumulator out to memory
 * produced by the pass above. Such a store writes the whole matrix, with each
 * lane of the warp writing the entries it holds:
 *
 *     out[matrix] = wmma_fragment_to_matrix(M, N, K, fragment)
 *         with predicate wmma_lane_owns(M, N, K)
 *
 * wmma_fragment_to_matrix_d permutes this lane's fragment up into a whole
 * matrix, leaving the entries the lane doesn't hold undefined, and
 * wmma_lane_owns says whether this lane holds each entry of the matrix in its
 * fragment. Both are opaque, because the mapping from matrix entry to lane
 * isn't architecturally specified. The layout of the matrix in memory is
 * recoverable from the index, as it is for a load.
 *
 * Passes that rewrite the structure of a store must leave these alone. */
bool is_wmma_matrix_store(const Store *op);

/** Peel the lane permutations the simplifier may have applied to the value of a
 * store, moving the inverse of each onto the index, and return what's left of
 * the value. Transposing both sides of a store cancels out, so this recovers
 * the store as it was written.
 *
 * The simplifier rewrites a store whose index isn't dense into a dense store of
 * a transposed value, so a wmma store to a column-major matrix arrives at the
 * backend as a dense store of the transpose of that matrix. That's the same
 * thing as a column-major store, which is one of the instructions, so the
 * backend just undoes the rewrite rather than preventing it. */
Expr peel_store_permutations(const Store *op, Expr *index);

/** How a matrix a wmma instruction moves is laid out in memory. */
struct WMMAMatrixLayout {
    /** The address of its first entry. */
    Expr base;
    /** The distance between its rows if it's row-major, or between its
     * columns if it's column-major, in elements. */
    Expr stride;
    bool row_major;
};

/** Read how a matrix is laid out in memory off the access pattern of the load
 * or store of it: whichever dimension has stride one is the dense one, and the
 * other stride is the distance between its rows or columns. The extraction pass
 * uses this to build the access pattern, and the backend to read it back, so
 * they can't disagree about the convention. Returns false if the access isn't a
 * dense tile of the given shape. */
bool wmma_matrix_layout(const MultiRamp &mr, int rows, int cols,
                        WMMAMatrixLayout *result);

}  // namespace Internal
}  // namespace Halide

#endif
