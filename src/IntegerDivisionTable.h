#ifndef HALIDE_INTEGER_DIVISION_TABLE_H
#define HALIDE_INTEGER_DIVISION_TABLE_H

/** \file
 * Tables telling us how to do integer division via fixed-point
 * multiplication for various small constants.
 */
namespace Halide {
namespace Internal {
namespace IntegerDivision {

extern int64_t table_u8[256][4];
extern int64_t table_s8[256][4];
extern int64_t table_u16[256][4];
extern int64_t table_s16[256][4];
extern int64_t table_u32[256][4];
extern int64_t table_s32[256][4];

extern int64_t table_runtime_u8[256][4];
extern int64_t table_runtime_s8[256][4];
extern int64_t table_runtime_u16[256][4];
extern int64_t table_runtime_s16[256][4];
extern int64_t table_runtime_u32[256][4];
extern int64_t table_runtime_s32[256][4];

}
}
}

#endif
