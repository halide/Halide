#include "Float16.h"
#include "Float16Literal.h"

Halide::float16_t operator"" _fp16(const char *stringRepr) {
    // Note we will never get a string starting with "-".
    return Halide::float16_t(stringRepr,
                          Halide::float16_t::RoundingMode::ToNearestTiesToEven);
}
