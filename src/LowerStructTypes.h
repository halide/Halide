#ifndef HALIDE_LOWER_STRUCT_TYPES_H
#define HALIDE_LOWER_STRUCT_TYPES_H

/** \file
 * Defines the lowering pass that rewrites field()/pack_struct() intrinsics
 * into ordinary loads and Reinterpret nodes.
 *
 * Struct types are treated as an ordinary, opaque, N-byte element type
 * throughout scheduling, bounds inference, and storage flattening. This
 * pass must run *after* storage_flattening, once every struct-typed
 * Provide/Call has become a concrete Store/Load with a real flat byte
 * index. It splits a struct-typed Store into per-field byte stores and
 * rewrites field() reads into byte-shifted loads, using that flat index.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Lower all field()/pack_struct() usage in a Stmt. */
Stmt lower_struct_types(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
