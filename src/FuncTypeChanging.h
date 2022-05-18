#ifndef HALIDE_FUNC_TYPE_CHANGING_H
#define HALIDE_FUNC_TYPE_CHANGING_H

/** \file
 * Support for changing the function's return type by fusing a number of
 * consequtive elements, or splitting a single element into parts,
 * along a certain dimension.
 */

#include "Func.h"
#include "FuncExtras.h"

namespace Halide {

namespace FuncTypeChanging {

enum class ChunkOrder {
    // Example:
    //  i32 0x0D0C0B0A -> 4xi8  -> { 0x0A, 0x0B, 0x0C, 0x0D }
    //  i32 0x0D0C0B0A -> 2xi16 -> { 0x0B0A, 0x0D0C }
    //  4xi8 { 0x0A, 0x0B, 0x0C, 0x0D } -> i32 -> 0x0D0C0B0A
    //  2xi16 { 0x0B0A, 0x0D0C }        -> i32 -> 0x0D0C0B0A
    //  2xi16 { 0x0D0C, 0x0B0A }        -> i32 -> 0x0B0A0D0C
    LowestFirst,

    // Example:
    //  i32 0x0D0C0B0A -> 4xi8   -> { 0x0D, 0x0C, 0x0B, 0x0A }
    //  i32 0x0D0C0B0A -> 2xi16t -> { 0x0D0C, 0x0B0A }
    //  4xi8 { 0x0A, 0x0B, 0x0C, 0x0D } -> i32 -> 0x0A0B0C0D
    //  2xi16 { 0x0B0A, 0x0D0C }        -> i32 -> 0x0B0A0D0C
    //  2xi16 { 0x0D0C, 0x0B0A }        -> i32 -> 0x0D0C0B0A
    HighestFirst,

    Default = LowestFirst  // DO NOT CHANGE.
};

Func change_type(const Func &input, const Type &dst_type, const Var &dim,
                 const std::string &name,
                 ChunkOrder chunk_order = ChunkOrder::Default);

template<typename T>
HALIDE_NO_USER_CODE_INLINE Func change_type(
    const T &func_like, const Type &dst_type, const Var &dim,
    const std::string &name, ChunkOrder chunk_order = ChunkOrder::Default) {
    return change_type(Internal::func_like_to_func(func_like), dst_type, dim,
                       name, chunk_order);
}

}  // namespace FuncTypeChanging

}  // namespace Halide

#endif
