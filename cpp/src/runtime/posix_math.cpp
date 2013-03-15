#include <stdint.h>
#include <math.h>
#include <float.h>

#define INLINE inline __attribute__((used)) __attribute__((always_inline)) __attribute__((nothrow))

extern "C" {
#ifdef _WIN32
extern float roundf(float);
extern double round(double);
#endif

INLINE float sqrt_f32(float x) {return sqrtf(x);}
INLINE float sin_f32(float x) {return sinf(x);}
INLINE float cos_f32(float x) {return cosf(x);}
INLINE float exp_f32(float x) {return expf(x);}
INLINE float log_f32(float x) {return logf(x);}
INLINE float pow_f32(float x, float y) {return powf(x, y);}
INLINE float floor_f32(float x) {return floorf(x);}
INLINE float ceil_f32(float x) {return ceilf(x);}
INLINE float round_f32(float x) {return roundf(x);}

INLINE double sqrt_f64(double x) {return sqrt(x);}
INLINE double sin_f64(double x) {return sin(x);}
INLINE double cos_f64(double x) {return cos(x);}
INLINE double exp_f64(double x) {return exp(x);}
INLINE double log_f64(double x) {return log(x);}
INLINE double pow_f64(double x, double y) {return pow(x, y);}
INLINE double floor_f64(double x) {return floor(x);}
INLINE double ceil_f64(double x) {return ceil(x);}
INLINE double round_f64(double x) {return round(x);}

INLINE float maxval_f32() {return FLT_MAX;}
INLINE float minval_f32() {return -FLT_MAX;}
INLINE double maxval_f64() {return DBL_MAX;}
INLINE double minval_f64() {return -DBL_MAX;}
INLINE uint8_t maxval_u8() {return 0xff;}
INLINE uint8_t minval_u8() {return 0;}
INLINE uint16_t maxval_u16() {return 0xffff;}
INLINE uint16_t minval_u16() {return 0;}
INLINE uint32_t maxval_u32() {return 0xffffffff;}
INLINE uint32_t minval_u32() {return 0;}
INLINE uint64_t maxval_u64() {return 0xffffffffffffffff;}
INLINE uint64_t minval_u64() {return 0;}
INLINE int8_t maxval_s8() {return 0x7f;}
INLINE int8_t minval_s8() {return 0x80;}
INLINE int16_t maxval_s16() {return 0x7fff;}
INLINE int16_t minval_s16() {return 0x8000;}
INLINE int32_t maxval_s32() {return 0x7fffffff;}
INLINE int32_t minval_s32() {return 0x80000000;}
INLINE int64_t maxval_s64() {return 0x7fffffffffffffff;}
INLINE int64_t minval_s64() {return 0x8000000000000000;}

INLINE int8_t abs_i8(int8_t a) {return a >= 0 ? a : -a;}
INLINE int16_t abs_i16(int16_t a) {return a >= 0 ? a : -a;}
INLINE int32_t abs_i32(int32_t a) {return a >= 0 ? a : -a;}
INLINE int64_t abs_i64(int64_t a) {return a >= 0 ? a : -a;}
INLINE float abs_f32(float a) {return a >= 0 ? a : -a;}
INLINE double abs_f64(double a) {return a >= 0 ? a : -a;}

}
