#ifndef HALIDE_LOWER_STRUCT_TYPES_H
#define HALIDE_LOWER_STRUCT_TYPES_H

/** \file
 * Defines the lowering pass that rewrites field()/pack_struct() intrinsics
 * (see Type::Struct, IROperator.h) into ordinary byte-addressed loads and
 * Reinterpret nodes, or -- for a struct-typed Func that ends up inlined --
 * pure algebraic simplification with no bytes ever materialized.
 *
 * Struct types are treated as an ordinary, opaque, N-byte element type
 * throughout scheduling, bounds inference, and storage flattening -- a
 * struct-valued Func keeps exactly the dimensionality the user wrote, and
 * none of those passes need any struct-specific handling. This pass must
 * therefore run *after* storage_flattening, once every struct-typed
 * Provide/Call has become a concrete Store/Load with a real flat byte
 * index: it splits a struct-typed Store into per-field byte stores and
 * rewrites field() reads into byte-shifted loads, using that flat index.
 * Nothing downstream of this pass needs to be aware struct types exist.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Lower all field()/pack_struct() usage in a Stmt. See LowerStructTypes.cpp
 * for the exact rewrite rules. */
Stmt lower_struct_types(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
