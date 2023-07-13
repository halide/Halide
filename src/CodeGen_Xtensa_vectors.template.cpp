#include <xtensa/tie/xt_ivpn.h>

#define HALIDE_MAYBE_UNUSED __attribute__((unused))

#if XCHAL_VISION_TYPE == 7
using common_int8x64_t __attribute__((ext_vector_type(64))) = int8_t;
using common_uint8x64_t __attribute__((ext_vector_type(64))) = uint8_t;
using common_int16x32_t __attribute__((ext_vector_type(32))) = int16_t;
using common_uint16x32_t __attribute__((ext_vector_type(32))) = uint16_t;
using common_int32x16_t __attribute__((ext_vector_type(16))) = int32_t;
using common_uint32x16_t __attribute__((ext_vector_type(16))) = uint32_t;
#elif XCHAL_VISION_TYPE == 8
using common_int8x128_t __attribute__((ext_vector_type(128))) = int8_t;
using common_uint8x128_t __attribute__((ext_vector_type(128))) = uint8_t;
using common_int16x64_t __attribute__((ext_vector_type(64))) = int16_t;
using common_uint16x64_t __attribute__((ext_vector_type(64))) = uint16_t;
using common_int32x32_t __attribute__((ext_vector_type(32))) = int32_t;
using common_uint32x32_t __attribute__((ext_vector_type(32))) = uint32_t;
#else
#error "Unsupported value for XCHAL_VISION_TYPE"
#endif

using int48_t = xb_int48;
using float16_t = xb_f16;
using native_vector_i8 = xb_vec2Nx8;
using native_vector_u8 = xb_vec2Nx8U;
using native_mask_i8 = vbool2N;
using native_vector_i16 = xb_vecNx16;
using native_vector_u16 = xb_vecNx16U;
using native_mask_i16 = vboolN;
using native_vector_i24 = xb_vec2Nx24;
using native_vector_i32 = xb_vecN_2x32v;
using native_vector_u32 = xb_vecN_2x32Uv;
using native_mask_i32 = vboolN_2;
using native_vector_i48 = xb_vecNx48;
using native_vector_f16 = xb_vecNxf16;
using native_vector_f32 = xb_vecN_2xf32;
using native_vector_i64 = xb_vecN_2x64w;

#if XCHAL_VISION_TYPE == 7
using int8x64_t = xb_vec2Nx8;
using uint8x64_t = xb_vec2Nx8U;
using int16x32_t = xb_vecNx16;
using uint16x32_t = xb_vecNx16U;
using int24_t = xb_int24;
using int24x64_t = xb_vec2Nx24;
using uint24x64_t = xb_vec2Nx24;
using int32x16_t = xb_vecN_2x32v;
using uint32x16_t = xb_vecN_2x32Uv;
using int48x32_t = xb_vecNx48;
using uint48x32_t = xb_vecNx48;
using int64x16_t = xb_vecN_2x64w;
using uint1x16_t = vboolN_2;
using uint1x32_t = vboolN;
using uint1x64_t = vbool2N;
using float16x16_t = xb_vecN_2xf16;
using float16x32_t = xb_vecNxf16;
using float32x16_t = xb_vecN_2xf32;
#elif XCHAL_VISION_TYPE == 8
using int8x128_t = xb_vec2Nx8;
using uint8x128_t = xb_vec2Nx8U;
using int16x64_t = xb_vecNx16;
using uint16x64_t = xb_vecNx16U;
using int24_t = xb_int24;
using int24x128_t = xb_vec2Nx24;
using uint24x128_t = xb_vec2Nx24;
using int32x32_t = xb_vecN_2x32v;
using uint32x32_t = xb_vecN_2x32Uv;
using int48x64_t = xb_vecNx48;
using uint48x64_t = xb_vecNx48;
using uint1x32_t = vboolN_2;
using uint1x64_t = vboolN;
using uint1x128_t = vbool2N;
using float16x32_t = xb_vecN_2xf16;
using float16x64_t = xb_vecNxf16;
using float32x32_t = xb_vecN_2xf32;
using int64x32_t = xb_vecN_2x64w;
#endif

using int8x4_t = xb_int32pr;
using uint8x4_t = xb_int32pr;
using int8x8_t = xb_int64pr;
using uint8x8_t = xb_int64pr;

template<typename NativeVector, int N>
struct MultipleOfNativeVector {
    NativeVector __attribute__((aligned(XCHAL_VISION_SIMD8))) native_vector[N];

    MultipleOfNativeVector() {
    }

    // TODO(vksnk): figure out a better/safer way to construct it.
    enum FromCppVector { from_native_vector };
    inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2) {
        static_assert(N == 2, "Wrong kind of constructor");
        native_vector[0] = src1;
        native_vector[1] = src2;
    }

    inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3) {
        static_assert(N == 3, "Wrong kind of constructor");
        native_vector[0] = src1;
        native_vector[1] = src2;
        native_vector[2] = src3;
    }

    inline MultipleOfNativeVector(FromCppVector, const MultipleOfNativeVector<NativeVector, 2> &src1, const MultipleOfNativeVector<NativeVector, 2> &src2) {
        static_assert(N == 4, "Wrong kind of constructor");
        native_vector[0] = src1.native_vector[0];
        native_vector[1] = src1.native_vector[1];
        native_vector[2] = src2.native_vector[0];
        native_vector[3] = src2.native_vector[1];
    }

    inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4) {
        static_assert(N == 4, "Wrong kind of constructor");
        native_vector[0] = src1;
        native_vector[1] = src2;
        native_vector[2] = src3;
        native_vector[3] = src4;
    }

    inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4,
                                  const NativeVector &src5, const NativeVector &src6) {
        static_assert(N == 6, "Wrong kind of constructor");
        native_vector[0] = src1;
        native_vector[1] = src2;
        native_vector[2] = src3;
        native_vector[3] = src4;
        native_vector[4] = src5;
        native_vector[5] = src6;
    }

    inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4,
                                  const NativeVector &src5, const NativeVector &src6, const NativeVector &src7, const NativeVector &src8) {
        static_assert(N == 8, "Wrong kind of constructor");
        native_vector[0] = src1;
        native_vector[1] = src2;
        native_vector[2] = src3;
        native_vector[3] = src4;
        native_vector[4] = src5;
        native_vector[5] = src6;
        native_vector[6] = src7;
        native_vector[7] = src8;
    }

    inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4,
                                  const NativeVector &src5, const NativeVector &src6, const NativeVector &src7, const NativeVector &src8,
                                  const NativeVector &src9, const NativeVector &src10, const NativeVector &src11, const NativeVector &src12) {
        static_assert(N == 12, "Wrong kind of constructor");
        native_vector[0] = src1;
        native_vector[1] = src2;
        native_vector[2] = src3;
        native_vector[3] = src4;
        native_vector[4] = src5;
        native_vector[5] = src6;
        native_vector[6] = src7;
        native_vector[7] = src8;
        native_vector[8] = src9;
        native_vector[9] = src10;
        native_vector[10] = src11;
        native_vector[11] = src12;
    }

    inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4,
                                  const NativeVector &src5, const NativeVector &src6, const NativeVector &src7, const NativeVector &src8,
                                  const NativeVector &src9, const NativeVector &src10, const NativeVector &src11, const NativeVector &src12,
                                  const NativeVector &src13, const NativeVector &src14, const NativeVector &src15, const NativeVector &src16) {
        static_assert(N == 16, "Wrong kind of constructor");
        native_vector[0] = src1;
        native_vector[1] = src2;
        native_vector[2] = src3;
        native_vector[3] = src4;
        native_vector[4] = src5;
        native_vector[5] = src6;
        native_vector[6] = src7;
        native_vector[7] = src8;
        native_vector[8] = src9;
        native_vector[9] = src10;
        native_vector[10] = src11;
        native_vector[11] = src12;
        native_vector[12] = src13;
        native_vector[13] = src14;
        native_vector[14] = src15;
        native_vector[15] = src16;
    }
};

#if XCHAL_VISION_TYPE == 7
using uint1x96_t = MultipleOfNativeVector<uint1x32_t, 3>;
using uint1x192_t = MultipleOfNativeVector<uint1x64_t, 3>;
using uint1x256_t = MultipleOfNativeVector<uint1x64_t, 4>;
using int8x128_t = MultipleOfNativeVector<int8x64_t, 2>;
using int8x192_t = MultipleOfNativeVector<int8x64_t, 3>;
using int8x256_t = MultipleOfNativeVector<int8x64_t, 4>;
using uint8x128_t = MultipleOfNativeVector<uint8x64_t, 2>;
using uint8x192_t = MultipleOfNativeVector<uint8x64_t, 3>;
using uint8x256_t = MultipleOfNativeVector<uint8x64_t, 4>;
using int16x64_t = MultipleOfNativeVector<int16x32_t, 2>;
using uint16x64_t = MultipleOfNativeVector<uint16x32_t, 2>;
using int16x96_t = MultipleOfNativeVector<int16x32_t, 3>;
using uint16x96_t = MultipleOfNativeVector<uint16x32_t, 3>;
using int16x128_t = MultipleOfNativeVector<int16x32_t, 4>;
using uint16x128_t = MultipleOfNativeVector<uint16x32_t, 4>;
using int24x128_t = MultipleOfNativeVector<int24x64_t, 2>;
using int32x32_t = MultipleOfNativeVector<int32x16_t, 2>;
using int32x48_t = MultipleOfNativeVector<int32x16_t, 3>;
using uint32x32_t = MultipleOfNativeVector<uint32x16_t, 2>;
using uint32x48_t = MultipleOfNativeVector<uint32x16_t, 3>;
using int32x64_t = MultipleOfNativeVector<int32x16_t, 4>;
using uint32x64_t = MultipleOfNativeVector<uint32x16_t, 4>;
using int32x96_t = MultipleOfNativeVector<int32x16_t, 6>;
using uint32x96_t = MultipleOfNativeVector<uint32x16_t, 6>;
using int32x128_t = MultipleOfNativeVector<int32x16_t, 8>;
using uint32x128_t = MultipleOfNativeVector<uint32x16_t, 8>;
// TODO(vksnk): this one should be generated automatically, but isn't.
using int32x192_t = MultipleOfNativeVector<int32x16_t, 12>;
using int32x256_t = MultipleOfNativeVector<int32x16_t, 16>;
using int48x64_t = MultipleOfNativeVector<int48x32_t, 2>;
using int64x32_t = MultipleOfNativeVector<int64x16_t, 2>;
using float32x32_t = MultipleOfNativeVector<float32x16_t, 2>;
using float32x48_t = MultipleOfNativeVector<float32x16_t, 3>;
using float32x64_t = MultipleOfNativeVector<float32x16_t, 4>;
#elif XCHAL_VISION_TYPE == 8
using uint1x192_t = MultipleOfNativeVector<uint1x64_t, 3>;
using uint1x384_t = MultipleOfNativeVector<uint1x128_t, 3>;
using uint1x512_t = MultipleOfNativeVector<uint1x128_t, 4>;
using int8x256_t = MultipleOfNativeVector<int8x128_t, 2>;
using int8x512_t = MultipleOfNativeVector<int8x128_t, 4>;
using uint8x256_t = MultipleOfNativeVector<uint8x128_t, 2>;
using uint8x384_t = MultipleOfNativeVector<uint8x128_t, 3>;
using uint8x512_t = MultipleOfNativeVector<uint8x128_t, 4>;
using int16x128_t = MultipleOfNativeVector<int16x64_t, 2>;
using uint16x128_t = MultipleOfNativeVector<uint16x64_t, 2>;
using int16x192_t = MultipleOfNativeVector<int16x64_t, 3>;
using uint16x192_t = MultipleOfNativeVector<uint16x64_t, 3>;
using int16x256_t = MultipleOfNativeVector<int16x64_t, 4>;
using uint16x256_t = MultipleOfNativeVector<uint16x64_t, 4>;
using int24x256_t = MultipleOfNativeVector<int24x128_t, 2>;
using int32x64_t = MultipleOfNativeVector<int32x32_t, 2>;
using uint32x64_t = MultipleOfNativeVector<uint32x32_t, 2>;
using int32x128_t = MultipleOfNativeVector<int32x32_t, 4>;
using uint32x128_t = MultipleOfNativeVector<uint32x32_t, 4>;
using int32x192_t = MultipleOfNativeVector<int32x32_t, 6>;
using uint32x192_t = MultipleOfNativeVector<uint32x32_t, 6>;
using int32x256_t = MultipleOfNativeVector<int32x32_t, 8>;
using uint32x256_t = MultipleOfNativeVector<uint32x32_t, 8>;
// TODO(vksnk): this one should be generated automatically, but isn't.
using int32x382_t = MultipleOfNativeVector<int32x32_t, 12>;
using int32x512_t = MultipleOfNativeVector<int32x32_t, 16>;
using int48x128_t = MultipleOfNativeVector<int48x64_t, 2>;
using int64x64_t = MultipleOfNativeVector<int64x32_t, 2>;
using float32x64_t = MultipleOfNativeVector<float32x32_t, 2>;
using float32x128_t = MultipleOfNativeVector<float32x32_t, 4>;
#endif

#if XCHAL_VISION_TYPE == 7
#define VECTOR_WIDTH_I8 64
#define VECTOR_WIDTH_U8 64
#define VECTOR_WIDTH_I16 32
#define VECTOR_WIDTH_U16 32
#define VECTOR_WIDTH_F16 32
#define VECTOR_WIDTH_I32 16
#define VECTOR_WIDTH_U32 16
#define VECTOR_WIDTH_F32 16
#elif XCHAL_VISION_TYPE == 8
#define VECTOR_WIDTH_I8 128
#define VECTOR_WIDTH_U8 128
#define VECTOR_WIDTH_I16 64
#define VECTOR_WIDTH_U16 64
#define VECTOR_WIDTH_F16 64
#define VECTOR_WIDTH_I32 32
#define VECTOR_WIDTH_U32 32
#define VECTOR_WIDTH_F32 32
#endif

using native_vector_i8_x2 = MultipleOfNativeVector<native_vector_i8, 2>;
using native_vector_i8_x3 = MultipleOfNativeVector<native_vector_i8, 3>;
using native_vector_i8_x4 = MultipleOfNativeVector<native_vector_i8, 4>;

using native_vector_u8_x2 = MultipleOfNativeVector<native_vector_u8, 2>;
using native_vector_u8_x3 = MultipleOfNativeVector<native_vector_u8, 3>;
using native_vector_u8_x4 = MultipleOfNativeVector<native_vector_u8, 4>;
using native_vector_u8_x6 = MultipleOfNativeVector<native_vector_u8, 6>;

using native_vector_i16_x2 = MultipleOfNativeVector<native_vector_i16, 2>;
using native_vector_i16_x4 = MultipleOfNativeVector<native_vector_i16, 4>;

using native_vector_u16_x2 = MultipleOfNativeVector<native_vector_u16, 2>;
using native_vector_u16_x3 = MultipleOfNativeVector<native_vector_u16, 3>;
using native_vector_u16_x4 = MultipleOfNativeVector<native_vector_u16, 4>;
using native_vector_u16_x6 = MultipleOfNativeVector<native_vector_u16, 6>;
using native_vector_u16_x8 = MultipleOfNativeVector<native_vector_u16, 8>;

using native_vector_i24_x2 = MultipleOfNativeVector<native_vector_i24, 2>;

using native_vector_i32_x2 = MultipleOfNativeVector<native_vector_i32, 2>;
using native_vector_i32_x4 = MultipleOfNativeVector<native_vector_i32, 4>;
using native_vector_i32_x6 = MultipleOfNativeVector<native_vector_i32, 6>;
using native_vector_i32_x8 = MultipleOfNativeVector<native_vector_i32, 8>;
using native_vector_i32_x12 = MultipleOfNativeVector<native_vector_i32, 12>;
using native_vector_i32_x16 = MultipleOfNativeVector<native_vector_i32, 16>;

using native_vector_u32_x2 = MultipleOfNativeVector<native_vector_u32, 2>;
using native_vector_u32_x4 = MultipleOfNativeVector<native_vector_u32, 4>;

using native_vector_i48_x2 = MultipleOfNativeVector<native_vector_i48, 2>;

using native_vector_f32_x2 = MultipleOfNativeVector<native_vector_f32, 2>;
using native_vector_f32_x4 = MultipleOfNativeVector<native_vector_f32, 4>;

using native_vector_i64_x2 = MultipleOfNativeVector<native_vector_i64, 2>;

using native_mask_i8_x3 = MultipleOfNativeVector<native_mask_i8, 3>;
using native_mask_i8_x4 = MultipleOfNativeVector<native_mask_i8, 4>;
using native_mask_i8_x6 = MultipleOfNativeVector<native_mask_i8, 6>;
using native_mask_i16_x2 = MultipleOfNativeVector<native_mask_i16, 2>;
using native_mask_i16_x3 = MultipleOfNativeVector<native_mask_i16, 3>;

template<typename ToType, typename FromType>
HALIDE_ALWAYS_INLINE ToType convert(const FromType &from_type) = delete;

template<typename ResultType>
HALIDE_ALWAYS_INLINE ResultType ramp(int32_t base, int32_t stride) = delete;

template<typename ResultType>
HALIDE_ALWAYS_INLINE ResultType dense_ramp(int32_t base) = delete;

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 ramp<native_vector_i32_x2>(int32_t base, int32_t stride) {
    native_vector_i32 one_to_n = IVP_SEQN_2X32();
    native_vector_i32 base_w = base;
    native_vector_i32 stride_w = stride;
    native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_2 + one_to_n, stride_w))));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 dense_ramp<native_vector_i32_x2>(int32_t base) {
    const native_vector_i32 base_w = native_vector_i32(base) + IVP_SEQN_2X32();
    const native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, base_w, base_w + lanes_2);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 ramp<native_vector_i32_x4>(int32_t base, int32_t stride) {
    native_vector_i32 one_to_n = IVP_SEQN_2X32();
    native_vector_i32 base_w = base;
    native_vector_i32 stride_w = stride;
    native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    native_vector_i32 lanes_3 = VECTOR_WIDTH_I32 * 2;
    native_vector_i32 lanes_4 = VECTOR_WIDTH_I32 * 3;

    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector,
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_2 + one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_3 + one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_4 + one_to_n, stride_w))));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 dense_ramp<native_vector_i32_x4>(int32_t base) {
    native_vector_i32 base_w = IVP_ADDN_2X32(native_vector_i32(base), IVP_SEQN_2X32());
    native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    native_vector_i32 lanes_3 = VECTOR_WIDTH_I32 * 2;
    native_vector_i32 lanes_4 = VECTOR_WIDTH_I32 * 3;

    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector,
                                base_w,
                                IVP_ADDN_2X32(base_w, lanes_2),
                                IVP_ADDN_2X32(base_w, lanes_3),
                                IVP_ADDN_2X32(base_w, lanes_4));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x8 ramp<native_vector_i32_x8>(int32_t base, int32_t stride) {
    native_vector_i32 one_to_n = IVP_SEQN_2X32();
    native_vector_i32 base_w = base;
    native_vector_i32 stride_w = stride;
    native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    native_vector_i32 lanes_3 = VECTOR_WIDTH_I32 * 2;
    native_vector_i32 lanes_4 = VECTOR_WIDTH_I32 * 3;
    native_vector_i32 lanes_5 = VECTOR_WIDTH_I32 * 4;
    native_vector_i32 lanes_6 = VECTOR_WIDTH_I32 * 5;
    native_vector_i32 lanes_7 = VECTOR_WIDTH_I32 * 6;
    native_vector_i32 lanes_8 = VECTOR_WIDTH_I32 * 7;

    return native_vector_i32_x8(native_vector_i32_x8::from_native_vector,
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_2 + one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_3 + one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_4 + one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_5 + one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_6 + one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_7 + one_to_n, stride_w))),
                                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_8 + one_to_n, stride_w))));
}

template<typename ResultType, typename BaseType>
HALIDE_ALWAYS_INLINE ResultType broadcast(BaseType value) = delete;

template<>
HALIDE_ALWAYS_INLINE uint8x4_t broadcast<uint8x4_t, uint8_t>(uint8_t value) {
    native_vector_u8 v = value;
    return IVP_EXTRPRN_2X32(IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(v)), 0);
}

template<>
HALIDE_ALWAYS_INLINE uint8x8_t broadcast<uint8x8_t, uint8_t>(uint8_t value) {
    native_vector_u8 v = value;
    return IVP_EXTRPR64N_2X32(IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(v)), 0);
}

template<typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType aligned_load(const void *base, int32_t offset) {
    return *((const VectorType *)((const BaseType *)base + offset));
}

template<typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType load(const void *base, int32_t offset) {
    VectorType r;
    memcpy(&r, ((const BaseType *)base + offset), sizeof(BaseType) * Lanes);
    return r;
}

template<typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void aligned_store(const VectorType &a, void *base, int32_t offset) {
    *((VectorType *)((BaseType *)base + offset)) = a;
}

template<typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store(const VectorType &a, void *base, int32_t offset) {
    memcpy(((BaseType *)base + offset), &a, sizeof(BaseType) * Lanes);
}

template<typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType load_variable(const void *base, int32_t offset, int32_t count) {
    VectorType r;
    memcpy(&r, ((const BaseType *)base + offset), sizeof(BaseType) * count);
    return r;
}

template<typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_variable(const VectorType &a, void *base, int32_t offset, int32_t count) {
    memcpy(((BaseType *)base + offset), &a, sizeof(BaseType) * count);
}

template<>
HALIDE_ALWAYS_INLINE void store_variable<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(const native_vector_u8 &a, void *base, int32_t offset, int32_t count) {
    valign align = IVP_ZALIGN();
    xb_vec2Nx8U *__restrict ptr = (xb_vec2Nx8U *)((uint8_t *)base + offset);
    IVP_SAV2NX8U_XP(a, align, ptr, count);
    IVP_SAPOS2NX8U_FP(align, ptr);
}

template<typename VectorType, typename OffsetType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_scatter(const VectorType &a, void *base, const OffsetType &offset) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[Lanes];
    aligned_store<VectorType, BaseType, Lanes>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[Lanes];
    aligned_store<OffsetType, int32_t, Lanes>(offset, &offsets[0], 0);

    for (int i = 0; i < Lanes; i++) {
        ((BaseType *)base)[offsets[i]] = tmp[i];
    }
}

template<typename VectorType, typename OffsetType, typename PredicateType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType load_predicated(const void *base, const OffsetType &offset, const PredicateType &predicate) = delete;

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 load_predicated<native_vector_u8, native_vector_i32_x4, native_mask_i8, uint8_t, VECTOR_WIDTH_U8>(const void *base, const native_vector_i32_x4 &offset, const native_mask_i8 &predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[VECTOR_WIDTH_U8];
    aligned_store<native_vector_i32_x4, int32_t, VECTOR_WIDTH_U8>(offset, &offsets[0], 0);
    native_vector_u8 vmask = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate);
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(vmask, &mask[0], 0);

    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[VECTOR_WIDTH_U8];
    for (int i = 0; i < VECTOR_WIDTH_U8; i++) {
        if (mask[i] == 1) {
            output[i] = ((const uint8_t *)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_u8 *)output);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 load_predicated<native_vector_i16, native_vector_i32_x2, native_mask_i16, int16_t, VECTOR_WIDTH_I16>(const void *base, const native_vector_i32_x2 &offset, const native_mask_i16 &predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[VECTOR_WIDTH_I16];
    aligned_store<native_vector_i32_x2, int32_t, VECTOR_WIDTH_I16>(offset, &offsets[0], 0);
    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[VECTOR_WIDTH_I16];
    aligned_store<native_vector_i16, int16_t, VECTOR_WIDTH_I16>(vmask, &mask[0], 0);

    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[VECTOR_WIDTH_I16];
    for (int i = 0; i < VECTOR_WIDTH_I16; i++) {
        if (mask[i] == 1) {
            output[i] = ((const int16_t *)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_i16 *)output);
}

template<>
HALIDE_ALWAYS_INLINE native_mask_i16_x2 convert<native_mask_i16_x2, native_mask_i8>(const native_mask_i8 &src);

template<>
HALIDE_ALWAYS_INLINE
    native_vector_i16_x2
    load_predicated<native_vector_i16_x2, native_vector_i32_x4, native_mask_i8, int16_t, 2 * VECTOR_WIDTH_I16>(
        const void *base, const native_vector_i32_x4 &offset, const native_mask_i8 &predicate) {
    native_mask_i16_x2 c_predicate = convert<native_mask_i16_x2, native_mask_i8>(predicate);
    native_vector_i16 p1 = load_predicated<native_vector_i16, native_vector_i32_x2, native_mask_i16, int16_t, VECTOR_WIDTH_I16>(
        base,
        native_vector_i32_x2(
            native_vector_i32_x2::from_native_vector,
            offset.native_vector[0], offset.native_vector[1]),
        c_predicate.native_vector[0]);

    native_vector_i16 p2 = load_predicated<native_vector_i16, native_vector_i32_x2, native_mask_i16, int16_t, VECTOR_WIDTH_I16>(
        base,
        native_vector_i32_x2(
            native_vector_i32_x2::from_native_vector,
            offset.native_vector[2], offset.native_vector[3]),
        c_predicate.native_vector[1]);
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, p1, p2);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 load_predicated<native_vector_u16, native_vector_i32_x2, native_mask_i16, uint16_t, VECTOR_WIDTH_U16>(const void *base, const native_vector_i32_x2 &offset, const native_mask_i16 &predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[VECTOR_WIDTH_U16];
    aligned_store<native_vector_i32_x2, int32_t, VECTOR_WIDTH_U16>(offset, &offsets[0], 0);
    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[VECTOR_WIDTH_U16];
    aligned_store<native_vector_i16, int16_t, VECTOR_WIDTH_U16>(vmask, &mask[0], 0);

    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[VECTOR_WIDTH_U16];
    for (int i = 0; i < VECTOR_WIDTH_U16; i++) {
        if (mask[i] == 1) {
            output[i] = ((const uint16_t *)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_u16 *)output);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 load_predicated<native_vector_i32_x2, native_vector_i32_x2, native_mask_i16, int32_t, 2 * VECTOR_WIDTH_I32>(const void *base, const native_vector_i32_x2 &offset, const native_mask_i16 &predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);
    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i16, int16_t, 2 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    int32_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[2 * VECTOR_WIDTH_I32];
    for (int i = 0; i < 2 * VECTOR_WIDTH_I32; i++) {
        if (mask[i] == 1) {
            output[i] = ((const int32_t *)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_i32_x2 *)output);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 load_predicated<native_vector_f32_x2, native_vector_i32_x2, native_mask_i16, float, 2 * VECTOR_WIDTH_F32>(const void *base, const native_vector_i32_x2 &offset, const native_mask_i16 &predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[2 * VECTOR_WIDTH_F32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_F32>(offset, &offsets[0], 0);
    native_vector_u16 vmask = IVP_MOVNX16T(native_vector_u16(1), native_vector_u16(0), predicate);
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[2 * VECTOR_WIDTH_F32];
    aligned_store<native_vector_u16, uint16_t, 2 * VECTOR_WIDTH_F32>(vmask, &mask[0], 0);

    float __attribute__((aligned(XCHAL_VISION_SIMD8))) output[2 * VECTOR_WIDTH_F32];
    for (int i = 0; i < 2 * VECTOR_WIDTH_F32; i++) {
        if (mask[i] == 1) {
            output[i] = ((const float *)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_f32_x2 *)output);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x4 load_predicated<native_vector_f32_x4, native_vector_i32_x4, native_mask_i8, float, 4 * VECTOR_WIDTH_F32>(const void *base, const native_vector_i32_x4 &offset, const native_mask_i8 &predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[4 * VECTOR_WIDTH_F32];
    aligned_store<native_vector_i32_x4, int32_t, 4 * VECTOR_WIDTH_F32>(offset, &offsets[0], 0);
    native_vector_u8 vmask = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate);
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[4 * VECTOR_WIDTH_F32];
    aligned_store<native_vector_u8, uint8_t, 4 * VECTOR_WIDTH_F32>(vmask, &mask[0], 0);

    float __attribute__((aligned(XCHAL_VISION_SIMD8))) output[4 * VECTOR_WIDTH_F32];
    for (int i = 0; i < 4 * VECTOR_WIDTH_F32; i++) {
        if (mask[i] == 1) {
            output[i] = ((const float *)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_f32_x4 *)output);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 load_predicated<native_vector_i32_x4, native_vector_i32_x4, native_mask_i8, int32_t, 4 * VECTOR_WIDTH_I32>(const void *base, const native_vector_i32_x4 &offset, const native_mask_i8 &predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[4 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x4, int32_t, 4 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);
    native_vector_u8 vmask = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate);
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[4 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_u8, uint8_t, 4 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    int32_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[4 * VECTOR_WIDTH_I32];
    for (int i = 0; i < 4 * VECTOR_WIDTH_I32; i++) {
        if (mask[i] == 1) {
            output[i] = ((const int32_t *)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_i32_x4 *)output);
}

template<typename VectorType, typename OffsetType, typename PredicateType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_predicated(const VectorType &a, void *base, const OffsetType &offset, const PredicateType &predicate) = delete;

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u8, native_vector_i32_x4, native_mask_i8, uint8_t, VECTOR_WIDTH_U8>(const native_vector_u8 &a, void *base, const native_vector_i32_x4 &offset, const native_mask_i8 &predicate) {
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[VECTOR_WIDTH_U8];
    aligned_store<native_vector_i32_x4, int32_t, VECTOR_WIDTH_U8>(offset, &offsets[0], 0);

    native_vector_u8 vmask = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate);
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(vmask, &mask[0], 0);

    for (int i = 0; i < VECTOR_WIDTH_U8; i++) {
        if (mask[i]) {
            ((uint8_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u8_x3, native_vector_i32_x12, native_mask_i8_x3, uint8_t, 3 * VECTOR_WIDTH_U8>(const native_vector_u8_x3 &a, void *base, const native_vector_i32_x12 &offset, const native_mask_i8_x3 &predicate) {
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[3 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8_x3, uint8_t, 3 * VECTOR_WIDTH_U8>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[3 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_i32_x12, int32_t, 3 * VECTOR_WIDTH_U8>(offset, &offsets[0], 0);

    native_vector_u8 vmask0 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[0]);
    native_vector_u8 vmask1 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[1]);
    native_vector_u8 vmask2 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[2]);

    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[3 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8_x3, uint8_t, 3 * VECTOR_WIDTH_U8>(
        native_vector_u8_x3(native_vector_u8_x3::from_native_vector, vmask0, vmask1, vmask2), &mask[0], 0);

    for (int i = 0; i < 3 * VECTOR_WIDTH_U8; i++) {
        if (mask[i]) {
            ((uint8_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u8_x4, native_vector_i32_x16, native_mask_i8_x4, uint8_t, 4 * VECTOR_WIDTH_U8>(const native_vector_u8_x4 &a, void *base, const native_vector_i32_x16 &offset, const native_mask_i8_x4 &predicate) {
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[4 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8_x4, uint8_t, 4 * VECTOR_WIDTH_U8>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[4 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_i32_x16, int32_t, 4 * VECTOR_WIDTH_U8>(offset, &offsets[0], 0);

    native_vector_u8 vmask0 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[0]);
    native_vector_u8 vmask1 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[1]);
    native_vector_u8 vmask2 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[2]);
    native_vector_u8 vmask3 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[3]);

    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[4 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8_x4, uint8_t, 4 * VECTOR_WIDTH_U8>(
        native_vector_u8_x4(native_vector_u8_x4::from_native_vector, vmask0, vmask1, vmask2, vmask3), &mask[0], 0);

    for (int i = 0; i < 4 * VECTOR_WIDTH_U8; i++) {
        if (mask[i]) {
            ((uint8_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_i16, native_vector_i32_x2, native_mask_i16, int16_t, VECTOR_WIDTH_I16>(const native_vector_i16 &a, void *base, const native_vector_i32_x2 &offset, const native_mask_i16 &predicate) {
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[VECTOR_WIDTH_I16];
    aligned_store<native_vector_i16, int16_t, VECTOR_WIDTH_I16>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);

    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i16, int16_t, 2 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    for (int i = 0; i < VECTOR_WIDTH_I16; i++) {
        if (mask[i]) {
            ((int16_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_i16_x2, native_vector_i32_x4, native_mask_i8, int16_t, 2 * VECTOR_WIDTH_I16>(const native_vector_i16_x2 &a, void *base, const native_vector_i32_x4 &offset, const native_mask_i8 &predicate) {
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[2 * VECTOR_WIDTH_I16];
    aligned_store<native_vector_i16_x2, int16_t, 2 * VECTOR_WIDTH_I16>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[4 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x4, int32_t, 4 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);

    native_vector_i8 vmask = IVP_MOV2NX8T(native_vector_i8(1), native_vector_i8(0), predicate);
    int8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[4 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i8, int8_t, 4 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    for (int i = 0; i < 2 * VECTOR_WIDTH_I16; i++) {
        if (mask[i]) {
            ((int16_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u16, native_vector_i32_x2, native_mask_i16, uint16_t, VECTOR_WIDTH_U16>(const native_vector_u16 &a, void *base, const native_vector_i32_x2 &offset, const native_mask_i16 &predicate) {
    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[VECTOR_WIDTH_U16];
    aligned_store<native_vector_u16, uint16_t, VECTOR_WIDTH_U16>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);

    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i16, int16_t, 2 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    for (int i = 0; i < VECTOR_WIDTH_I16; i++) {
        if (mask[i]) {
            ((uint16_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u16_x2, native_vector_i32_x4, native_mask_i8, uint16_t, 2 * VECTOR_WIDTH_U16>(const native_vector_u16_x2 &a, void *base, const native_vector_i32_x4 &offset, const native_mask_i8 &predicate) {
    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[2 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_u16_x2, uint16_t, 2 * VECTOR_WIDTH_I16>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[4 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x4, int32_t, 4 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);

    native_vector_i8 vmask = IVP_MOV2NX8T(native_vector_i8(1), native_vector_i8(0), predicate);
    int8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[4 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i8, int8_t, 4 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    for (int i = 0; i < 2 * VECTOR_WIDTH_I16; i++) {
        if (mask[i]) {
            ((int16_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u16_x3, native_vector_i32_x6, native_mask_i16_x3, uint16_t, 3 * VECTOR_WIDTH_U16>(const native_vector_u16_x3 &a, void *base, const native_vector_i32_x6 &offset, const native_mask_i16_x3 &predicate) {
    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[3 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_u16_x3, uint16_t, 3 * VECTOR_WIDTH_U16>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[3 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_i32_x6, int32_t, 3 * VECTOR_WIDTH_U16>(offset, &offsets[0], 0);

    native_vector_u16 vmask0 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), predicate.native_vector[0]);
    native_vector_u16 vmask1 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), predicate.native_vector[1]);
    native_vector_u16 vmask2 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), predicate.native_vector[2]);

    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[3 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_u16_x3, uint16_t, 3 * VECTOR_WIDTH_U16>(
        native_vector_u16_x3(native_vector_u16_x3::from_native_vector, vmask0, vmask1, vmask2), &mask[0], 0);

    for (int i = 0; i < 3 * VECTOR_WIDTH_U16; i++) {
        if (mask[i]) {
            ((uint16_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u16_x6, native_vector_i32_x12, native_mask_i8_x3, uint16_t, 6 * VECTOR_WIDTH_U16>(const native_vector_u16_x6 &a, void *base, const native_vector_i32_x12 &offset, const native_mask_i8_x3 &predicate) {
    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[6 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_u16_x6, uint16_t, 6 * VECTOR_WIDTH_U16>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[3 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_i32_x12, int32_t, 6 * VECTOR_WIDTH_U16>(offset, &offsets[0], 0);

    native_mask_i16_x2 c_predicate0 = convert<native_mask_i16_x2, native_mask_i8>(predicate.native_vector[0]);
    native_mask_i16_x2 c_predicate1 = convert<native_mask_i16_x2, native_mask_i8>(predicate.native_vector[1]);
    native_mask_i16_x2 c_predicate2 = convert<native_mask_i16_x2, native_mask_i8>(predicate.native_vector[2]);

    native_vector_u16 vmask0 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), c_predicate0.native_vector[0]);
    native_vector_u16 vmask1 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), c_predicate0.native_vector[1]);
    native_vector_u16 vmask2 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), c_predicate1.native_vector[0]);
    native_vector_u16 vmask3 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), c_predicate1.native_vector[1]);
    native_vector_u16 vmask4 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), c_predicate2.native_vector[0]);
    native_vector_u16 vmask5 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), c_predicate2.native_vector[1]);

    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[6 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_u16_x6, uint16_t, 6 * VECTOR_WIDTH_U16>(
        native_vector_u16_x6(native_vector_u16_x6::from_native_vector, vmask0, vmask1, vmask2, vmask3, vmask4, vmask5), &mask[0], 0);

    for (int i = 0; i < 6 * VECTOR_WIDTH_U16; i++) {
        if (mask[i]) {
            ((uint16_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

template<>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_i32_x2, native_vector_i32_x2, native_mask_i16, int32_t, 2 * VECTOR_WIDTH_I32>(const native_vector_i32_x2 &a, void *base, const native_vector_i32_x2 &offset, const native_mask_i16 &predicate) {
    int32_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);

    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i16, int16_t, 2 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    for (int i = 0; i < 2 * VECTOR_WIDTH_I32; i++) {
        if (mask[i]) {
            ((int32_t *)base)[offsets[i]] = tmp[i];
        }
    }
}

inline uint8_t halide_shift_right(uint8_t a, uint8_t b) {
    return (uint16_t)a >> (uint16_t)b;
}

inline int8_t halide_shift_right(int8_t a, int8_t b) {
    return (int16_t)a >> (int16_t)b;
}

inline uint8_t halide_shift_left(uint8_t a, uint8_t b) {
    return (uint16_t)a << (uint16_t)b;
}

inline int8_t halide_shift_left(int8_t a, int8_t b) {
    return (int16_t)a << (int16_t)b;
}

template<typename VectorType, typename ScalarArgumentType, typename ScalarReturnType, int Lanes>
VectorType scalarize_unary(ScalarReturnType (*fn)(ScalarArgumentType), VectorType a) {
    ScalarArgumentType __attribute__((aligned(64))) tmp[Lanes];
    aligned_store<VectorType, ScalarArgumentType, Lanes>(a, &tmp[0], 0);

    for (int i = 0; i < Lanes; i++) {
        // Just update in-place, because it's a tmp buffer anyway.
        tmp[i] = fn(tmp[i]);
    }

    return *((VectorType *)tmp);
}

template<typename VectorType, typename ScalarArgumentType, typename ScalarReturnType, int Lanes>
VectorType scalarize_binary(ScalarReturnType (*fn)(ScalarArgumentType, ScalarArgumentType), VectorType a, VectorType b) {
    ScalarArgumentType __attribute__((aligned(64))) tmp_a[Lanes];
    aligned_store<VectorType, ScalarArgumentType, Lanes>(a, &tmp_a[0], 0);

    ScalarArgumentType __attribute__((aligned(64))) tmp_b[Lanes];
    aligned_store<VectorType, ScalarArgumentType, Lanes>(b, &tmp_b[0], 0);

    for (int i = 0; i < Lanes; i++) {
        // Just update in-place, because it's a tmp buffer anyway.
        tmp_a[i] = fn(tmp_a[i], tmp_b[i]);
    }

    return *((VectorType *)tmp_a);
}

template<typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo shuffle(const VectorTypeFrom &a, const int32_t indices[LanesTo]) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp1[LanesFrom];
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp2[LanesTo];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, &tmp1[0], 0);
    for (int i = 0; i < LanesTo; i++) {
        tmp2[i] = tmp1[indices[i]];
    }

    return *((VectorTypeTo *)tmp2);
}

template<typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType &a, const ArgType &b) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);

    return *((ResultType *)tmp);
}

template<typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType &a, const ArgType &b, const ArgType &c) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);
    store<ArgType, BaseType, LanesArg>(c, &tmp[0], 2 * LanesArg);

    return *((ResultType *)tmp);
}

template<typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType &a, const ArgType &b, const ArgType &c, const ArgType &d) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);
    store<ArgType, BaseType, LanesArg>(c, &tmp[0], 2 * LanesArg);
    store<ArgType, BaseType, LanesArg>(d, &tmp[0], 3 * LanesArg);

    return *((ResultType *)tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 concat<native_vector_i32_x2, native_vector_i32, int32_t, 2 * VECTOR_WIDTH_I32, VECTOR_WIDTH_I32>(const native_vector_i32 &a, const native_vector_i32 &b) {
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, a, b);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 concat<native_vector_i32_x4, native_vector_i32, int32_t, 4 * VECTOR_WIDTH_I32, VECTOR_WIDTH_I32>(const native_vector_i32 &a, const native_vector_i32 &b, const native_vector_i32 &c, const native_vector_i32 &d) {
    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, a, b, c, d);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 concat<native_vector_i16_x2, native_vector_i16, int16_t, 2 * VECTOR_WIDTH_I16, VECTOR_WIDTH_I16>(const native_vector_i16 &a, const native_vector_i16 &b) {
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a, b);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 concat<native_vector_u16_x2, native_vector_u16, uint16_t, 2 * VECTOR_WIDTH_U16, VECTOR_WIDTH_U16>(const native_vector_u16 &a, const native_vector_u16 &b) {
    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a, b);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8_x2 concat<native_vector_u8_x2, native_vector_u8, uint8_t, 2 * VECTOR_WIDTH_U8, VECTOR_WIDTH_U8>(const native_vector_u8 &a, const native_vector_u8 &b) {
    return native_vector_u8_x2(native_vector_u8_x2::from_native_vector, a, b);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 concat<native_vector_f32_x2, native_vector_f32, float, 2 * VECTOR_WIDTH_F32, VECTOR_WIDTH_F32>(const native_vector_f32 &a, const native_vector_f32 &b) {
    return native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a, b);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i24_x2 concat<native_vector_i24_x2, native_vector_i24, int24_t, 128, 64>(const native_vector_i24 &a, const native_vector_i24 &b) {
    return native_vector_i24_x2(native_vector_i24_x2::from_native_vector, a, b);
}

template<typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo halide_xtensa_pad_to_native(const VectorTypeFrom &a, int lanes) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesTo];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, tmp, 0);
    return load<VectorTypeTo, BaseType, LanesTo>(tmp, 0);
}

template<typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo halide_xtensa_slice_from_padded(const VectorTypeFrom &a, int lanes) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesFrom];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, tmp, 0);
    return load<VectorTypeTo, BaseType, LanesTo>(tmp, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_slice_from_padded<native_vector_u16_x2, native_vector_u16, uint16_t, 2 * VECTOR_WIDTH_U16, VECTOR_WIDTH_U16>(const native_vector_u16_x2 &a, int lanes) {
    return a.native_vector[0];
}

template<>
HALIDE_ALWAYS_INLINE native_mask_i16 halide_xtensa_pad_to_native<native_mask_i32, native_mask_i16, bool, VECTOR_WIDTH_I32, VECTOR_WIDTH_I16>(const native_mask_i32 &a, int lanes) {
    return IVP_JOINBN_2(a, a);
}

template<>
HALIDE_ALWAYS_INLINE native_mask_i8 halide_xtensa_pad_to_native<native_mask_i16, native_mask_i8, bool, VECTOR_WIDTH_I16, VECTOR_WIDTH_I8>(const native_mask_i16 &a, int lanes) {
    return IVP_JOINBN(a, a);
}

template<>
HALIDE_ALWAYS_INLINE native_mask_i8 halide_xtensa_pad_to_native<native_mask_i32, native_mask_i8, bool, VECTOR_WIDTH_I32, VECTOR_WIDTH_I8>(const native_mask_i32 &a, int lanes) {
    return IVP_JOINBN(IVP_JOINBN_2(a, a), IVP_JOINBN_2(a, a));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_u1_to_i16(const native_mask_i16 &a) {
    return IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), a);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int8x4_t load<int8x4_t, int8_t, 4>(const void *base, int32_t offset) {
    return *((const int8x4_t *)((const int8_t *)base + offset));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint8x4_t load<uint8x4_t, uint8_t, 4>(const void *base, int32_t offset) {
    return *((const uint8x4_t *)((const uint8_t *)base + offset));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u8 load<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(const void *base, int32_t offset) {
    native_vector_u8 r;
    const xb_vec2Nx8U *__restrict ptr = (const xb_vec2Nx8U *)((const uint8_t *)base + offset);
    IVP_L2U2NX8U_XP(r, ptr, 0);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_i8, int8_t, VECTOR_WIDTH_I8>(const native_vector_i8 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vec2Nx8 *__restrict ptr = (xb_vec2Nx8 *)((int8_t *)base + offset);
    IVP_SA2NX8_IP(a, align, ptr);
    IVP_SAPOS2NX8_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(const native_vector_u8 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vec2Nx8U *__restrict ptr = (xb_vec2Nx8U *)((uint8_t *)base + offset);
    IVP_SA2NX8U_IP(a, align, ptr);
    IVP_SAPOS2NX8U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16 load<native_vector_i16, int16_t, VECTOR_WIDTH_I16>(const void *base, int32_t offset) {
    xb_vecNx16 r;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const int16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16_IP(r, align, (const xb_vecNx16 *)ptr8);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_i16, int16_t, VECTOR_WIDTH_I16>(const native_vector_i16 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx16 *ptr = (xb_vecNx16 *)((int16_t *)base + offset);
    IVP_SANX16_IP(a, align, ptr);
    // Flush alignment register.
    IVP_SAPOSNX16_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_i16_x2, int16_t, 2 * VECTOR_WIDTH_I16>(const native_vector_i16_x2 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx16 *ptr = (xb_vecNx16 *)((int16_t *)base + offset);
    IVP_SANX16_IP(a.native_vector[0], align, ptr);
    IVP_SANX16_IP(a.native_vector[1], align, ptr);
    // Flush alignment register.
    IVP_SAPOSNX16_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u16 load<native_vector_u16, uint16_t, VECTOR_WIDTH_U16>(const void *base, int32_t offset) {
    xb_vecNx16U r;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const uint16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16U_IP(r, align, (const xb_vecNx16U *)ptr8);

    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_u16, uint16_t, VECTOR_WIDTH_U16>(const native_vector_u16 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx16U *ptr = (xb_vecNx16U *)((uint16_t *)base + offset);
    IVP_SANX16U_IP(a, align, ptr);
    IVP_SAPOSNX16U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16_x2 load<native_vector_i16_x2, int16_t, 2 * VECTOR_WIDTH_I16>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const int16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16_IP(r1, align, (const xb_vecNx16 *)ptr8);
    IVP_LANX16_IP(r2, align, (const xb_vecNx16 *)ptr8);

    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u16_x2 load<native_vector_u16_x2, uint16_t, 2 * VECTOR_WIDTH_U16>(const void *base, int32_t offset) {
    xb_vecNx16U r1, r2;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const int16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16U_IP(r1, align, (const xb_vecNx16U *)ptr8);
    IVP_LANX16U_IP(r2, align, (const xb_vecNx16U *)ptr8);

    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x2 load<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(const void *base, int32_t offset) {
    xb_vecN_2x32v nv8_0, nv8_1;
    const xb_vecN_2x32v *__restrict ptr = (const xb_vecN_2x32v *)((const int32_t *)base + offset);
    valign align = IVP_LA_PP((const xb_vec2Nx8 *)ptr);
    IVP_LAN_2X32_IP(nv8_0, align, ptr);
    IVP_LAN_2X32_IP(nv8_1, align, ptr);
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, nv8_0, nv8_1);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x4 load<native_vector_i32_x4, int32_t, 4 * VECTOR_WIDTH_I32>(const void *base, int32_t offset) {
    xb_vecN_2x32v nv8_0, nv8_1, nv8_2, nv8_3;
    const xb_vecN_2x32v *__restrict ptr = (const xb_vecN_2x32v *)((const int32_t *)base + offset);
    valign align = IVP_LA_PP((const xb_vec2Nx8 *)ptr);
    IVP_LAN_2X32_IP(nv8_0, align, ptr);
    IVP_LAN_2X32_IP(nv8_1, align, ptr);
    IVP_LAN_2X32_IP(nv8_2, align, ptr);
    IVP_LAN_2X32_IP(nv8_3, align, ptr);
    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, nv8_0, nv8_1, nv8_2, nv8_3);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_f32 load<native_vector_f32, float32_t, VECTOR_WIDTH_F32>(const void *base, int32_t offset) {
    native_vector_f32 r;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const float32_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2XF32_IP(r, align, (const native_vector_f32 *)ptr8);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_f32, float32_t, VECTOR_WIDTH_F32>(const native_vector_f32 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    native_vector_f32 *ptr = (native_vector_f32 *)((float32_t *)base + offset);
    IVP_SAN_2XF32_IP(a, align, ptr);
    // Flush alignment register.
    IVP_SAPOSN_2XF32_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_f32_x2, float32_t, 2 * VECTOR_WIDTH_F32>(const native_vector_f32_x2 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    native_vector_f32 *ptr = (native_vector_f32 *)((float32_t *)base + offset);
    IVP_SAN_2XF32_IP(a.native_vector[0], align, ptr);
    IVP_SAN_2XF32_IP(a.native_vector[1], align, ptr);
    // Flush alignment register.
    IVP_SAPOSN_2XF32_FP(align, ptr);
}

template<typename ResultType, typename LoadType>
HALIDE_ALWAYS_INLINE ResultType widening_load(const void *base, int32_t offset) = delete;

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16 widening_load<native_vector_i16, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const uint8_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r, align, (const xb_vecNx8U *)ptr8);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16_x2 widening_load<native_vector_i16_x2, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const uint8_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r1, align, (const xb_vecNx8U *)ptr8);
    // Pointer is automatically incremented by previous call.
    IVP_LANX8U_IP(r2, align, (const xb_vecNx8U *)ptr8);

    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u16_x2 widening_load<native_vector_u16_x2, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const uint8_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r1, align, (const xb_vecNx8U *)ptr8);
    // Pointer is automatically incremented by previous call.
    IVP_LANX8U_IP(r2, align, (const xb_vecNx8U *)ptr8);

    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32 widening_load<native_vector_i32, int16_t>(const void *base, int32_t offset) {
    native_vector_i32 r1;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const int16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16S_IP(r1, align, (const xb_vecN_2x16 *)ptr8);
    return r1;
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x2 widening_load<native_vector_i32_x2, int16_t>(const void *base, int32_t offset) {
    native_vector_i32 r1, r2;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const int16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16S_IP(r1, align, (const xb_vecN_2x16 *)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16S_IP(r2, align, (const xb_vecN_2x16 *)ptr8);

    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x2 widening_load<native_vector_i32_x2, uint16_t>(const void *base, int32_t offset) {
    native_vector_i32 r1, r2;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const uint16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U *)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U *)ptr8);

    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u32_x2 widening_load<native_vector_u32_x2, uint16_t>(const void *base, int32_t offset) {
    native_vector_u32 r1, r2;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const uint16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U *)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U *)ptr8);

    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x4 widening_load<native_vector_i32_x4, uint16_t>(const void *base, int32_t offset) {
    native_vector_i32 r1, r2, r3, r4;
    const xb_vec2Nx8 *__restrict ptr8 = (const xb_vec2Nx8 *)((const uint16_t *)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U *)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U *)ptr8);
    IVP_LAN_2X16U_IP(r3, align, (const xb_vecN_2x16U *)ptr8);
    IVP_LAN_2X16U_IP(r4, align, (const xb_vecN_2x16U *)ptr8);

    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, r1, r2, r3, r4);
}

template<typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_narrowing(const VectorType &a, void *base, int32_t offset) = delete;

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_i16, int8_t, VECTOR_WIDTH_I16>(const native_vector_i16 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx8 *__restrict ptr = (xb_vecNx8 *)((int8_t *)base + offset);
    IVP_SANX8S_IP((a << 8) >> 8, align, ptr);
    IVP_SAPOSNX8S_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_i16, uint8_t, VECTOR_WIDTH_I16>(const native_vector_i16 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx8U *__restrict ptr = (xb_vecNx8U *)((uint8_t *)base + offset);
    IVP_SANX8U_IP(a, align, ptr);
    IVP_SAPOSNX8U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_u16, uint8_t, VECTOR_WIDTH_U16>(const native_vector_u16 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx8U *__restrict ptr = (xb_vecNx8U *)((uint8_t *)base + offset);
    IVP_SANX8U_IP(a, align, ptr);
    IVP_SAPOSNX8U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_i32, int16_t, VECTOR_WIDTH_I32>(const native_vector_i32 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecN_2x16 *__restrict ptr = (xb_vecN_2x16 *)((int16_t *)base + offset);
    IVP_SAN_2X16S_IP((a << 16) >> 16, align, ptr);
    IVP_SAPOSN_2X16S_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_u32, uint16_t, VECTOR_WIDTH_U32>(const native_vector_u32 &a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecN_2x16U *__restrict ptr = (xb_vecN_2x16U *)((uint16_t *)base + offset);
    IVP_SAN_2X16U_IP(a, align, ptr);
    IVP_SAPOSN_2X16U_FP(align, ptr);
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_interleave_i16(const native_vector_i16 &a, const native_vector_i16 &b) {
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_i32_x2 halide_xtensa_interleave_i32(const native_vector_i32 &a, const native_vector_i32 &b) {
    return native_vector_i32_x2(
        native_vector_i32_x2::from_native_vector,
        IVP_SELN_2X32I(b, a, IVP_SELI_32B_INTERLEAVE_1_LO),
        IVP_SELN_2X32I(b, a, IVP_SELI_32B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_i16_x4 halide_xtensa_interleave_i16(const native_vector_i16_x2 &a, const native_vector_i16_x2 &b) {
    return native_vector_i16_x4(native_vector_i16_x4::from_native_vector,
                                IVP_SELNX16I(b.native_vector[0], a.native_vector[0], IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b.native_vector[0], a.native_vector[0], IVP_SELI_16B_INTERLEAVE_1_HI),
                                IVP_SELNX16I(b.native_vector[1], a.native_vector[1], IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b.native_vector[1], a.native_vector[1], IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_i32_x4 halide_xtensa_interleave_i32(const native_vector_i32_x2 &a, const native_vector_i32_x2 &b) {
    return native_vector_i32_x4(
        native_vector_i32_x4::from_native_vector,
        IVP_SELN_2X32I(b.native_vector[0], a.native_vector[0], IVP_SELI_32B_INTERLEAVE_1_LO),
        IVP_SELN_2X32I(b.native_vector[0], a.native_vector[0], IVP_SELI_32B_INTERLEAVE_1_HI),
        IVP_SELN_2X32I(b.native_vector[1], a.native_vector[1], IVP_SELI_32B_INTERLEAVE_1_LO),
        IVP_SELN_2X32I(b.native_vector[1], a.native_vector[1], IVP_SELI_32B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u16_x2 halide_xtensa_interleave_u16(const native_vector_u16 &a, const native_vector_u16 &b) {
    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                                IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_HI));
}

// This sequence of instructions is taken from the user guide.
// For Q8 the guide provides wrong c3 sequences.
HALIDE_ALWAYS_INLINE native_vector_u16_x3 halide_xtensa_interleave_u16(const native_vector_u16 &a, const native_vector_u16 &b, const native_vector_u16 &c) {
// 16-bit interleave patterns
#if XCHAL_VISION_TYPE == 7
    __attribute__((aligned(XCHAL_VISION_SIMD8))) unsigned char int_16B_c3_step_0[64] = {
        0, 42, 1, 22, 32, 23, 2, 43, 3, 24, 33, 25, 4, 44, 5, 26,
        34, 27, 6, 45, 7, 28, 35, 29, 8, 46, 9, 30, 36, 31, 10, 47,
        11, 0, 37, 33, 12, 48, 13, 2, 38, 35, 14, 49, 15, 4, 39, 37,
        16, 50, 17, 6, 40, 39, 18, 51, 19, 8, 41, 41, 20, 52, 21, 10};
    __attribute__((aligned(XCHAL_VISION_SIMD8))) unsigned char int_16B_c3_step_1[64] = {
        11, 42, 53, 22, 12, 23, 13, 43, 54, 24, 14, 25, 15, 44, 55, 26,
        16, 27, 17, 45, 56, 28, 18, 29, 19, 46, 57, 30, 20, 31, 21, 47,
        58, 0, 22, 1, 23, 48, 59, 2, 24, 3, 25, 49, 60, 4, 26, 5,
        27, 50, 61, 6, 28, 7, 29, 51, 62, 8, 30, 9, 31, 52, 63, 10};
    unsigned long long int_16B_c3_step_1_msk = 0xffffffff55555555ULL;
#elif XCHAL_VISION_TYPE == 8
    __attribute__((aligned(XCHAL_VISION_SIMD8))) unsigned char int_16B_c3_step_0[128] = {
        0, 43, 1, 85, 64, 44, 2, 45, 3, 86, 65, 46, 4, 47, 5, 87,
        66, 48, 6, 49, 7, 88, 67, 50, 8, 51, 9, 89, 68, 52, 10, 53,
        11, 90, 69, 54, 12, 55, 13, 91, 70, 56, 14, 57, 15, 92, 71, 58,
        16, 59, 17, 93, 72, 60, 18, 61, 19, 94, 73, 62, 20, 63, 21, 95,
        74, 0, 22, 65, 23, 96, 75, 2, 24, 67, 25, 97, 76, 4, 26, 69,
        27, 98, 77, 6, 28, 71, 29, 99, 78, 8, 30, 73, 31, 100, 79, 10,
        32, 75, 33, 101, 80, 12, 34, 77, 35, 102, 81, 14, 36, 79, 37, 103,
        82, 16, 38, 81, 39, 104, 83, 18, 40, 83, 41, 105, 84, 20, 42, 85};
    __attribute__((aligned(XCHAL_VISION_SIMD8))) unsigned char int_16B_c3_step_1[128] = {
        106, 43, 22, 85, 23, 44, 107, 45, 24, 86, 25, 46, 108, 47, 26, 87,
        27, 48, 109, 49, 28, 88, 29, 50, 110, 51, 30, 89, 31, 52, 111, 53,
        32, 90, 33, 54, 112, 55, 34, 91, 35, 56, 113, 57, 36, 92, 37, 58,
        114, 59, 38, 93, 39, 60, 115, 61, 40, 94, 41, 62, 116, 63, 42, 95,
        43, 0, 117, 1, 44, 96, 45, 2, 118, 3, 46, 97, 47, 4, 119, 5,
        48, 98, 49, 6, 120, 7, 50, 99, 51, 8, 121, 9, 52, 100, 53, 10,
        122, 11, 54, 101, 55, 12, 123, 13, 56, 102, 57, 14, 124, 15, 58, 103,
        59, 16, 125, 17, 60, 104, 61, 18, 126, 19, 62, 105, 63, 20, 127, 21};
    __attribute__((aligned(16))) unsigned char int_16B_c3_step_1_msk[16] = {
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#endif
    native_vector_u16 vRG0, vRG1, vRGB0, vRGB1, vRGB2;
    // interleave RG
    IVP_DSELNX16UI(vRG1, vRG0, b, a, IVP_DSELI_INTERLEAVE_1);
    // interleave RG, B
    IVP_DSELNX16U(vRGB1, vRGB0, c, vRG0, *((xb_vec2Nx8 *)int_16B_c3_step_0));
    IVP_DSELNX16UT(vRGB1, vRGB2, c, vRG1, *((xb_vec2Nx8 *)int_16B_c3_step_1),
                   *((vbool2N *)&int_16B_c3_step_1_msk));

    return native_vector_u16_x3(native_vector_u16_x3::from_native_vector, vRGB0, vRGB1, vRGB2);
}

HALIDE_ALWAYS_INLINE native_vector_u16_x6 halide_xtensa_interleave_u16(const native_vector_u16_x2 &a, const native_vector_u16_x2 &b, const native_vector_u16_x2 &c) {
    native_vector_u16_x3 d = halide_xtensa_interleave_u16(a.native_vector[0], b.native_vector[0], c.native_vector[0]);
    native_vector_u16_x3 e = halide_xtensa_interleave_u16(a.native_vector[1], b.native_vector[1], c.native_vector[1]);

    return native_vector_u16_x6(
        native_vector_u16_x6::from_native_vector,
        d.native_vector[0], e.native_vector[0],
        d.native_vector[1], e.native_vector[1],
        d.native_vector[2], e.native_vector[2]);
}

HALIDE_ALWAYS_INLINE native_vector_u16_x4 halide_xtensa_interleave_u16(const native_vector_u16_x2 &a, const native_vector_u16_x2 &b) {
    return native_vector_u16_x4(native_vector_u16_x4::from_native_vector,
                                IVP_SELNX16UI(b.native_vector[0], a.native_vector[0], IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16UI(b.native_vector[0], a.native_vector[0], IVP_SELI_16B_INTERLEAVE_1_HI),
                                IVP_SELNX16UI(b.native_vector[1], a.native_vector[1], IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16UI(b.native_vector[1], a.native_vector[1], IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u16_x4 halide_xtensa_interleave_u16(const native_vector_u16 &a, const native_vector_u16 &b, const native_vector_u16 &c, const native_vector_u16 &d) {
    const native_vector_u16 ab0 = IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_LO);
    const native_vector_u16 ab1 = IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_HI);
    const native_vector_u16 cd0 = IVP_SELNX16UI(d, c, IVP_SELI_16B_INTERLEAVE_1_LO);
    const native_vector_u16 cd1 = IVP_SELNX16UI(d, c, IVP_SELI_16B_INTERLEAVE_1_HI);

    return native_vector_u16_x4(native_vector_u16_x4::from_native_vector,
                                IVP_SELNX16UI(cd0, ab0, IVP_SELI_16B_INTERLEAVE_2_LO),
                                IVP_SELNX16UI(cd0, ab0, IVP_SELI_16B_INTERLEAVE_2_HI),
                                IVP_SELNX16UI(cd1, ab1, IVP_SELI_16B_INTERLEAVE_2_LO),
                                IVP_SELNX16UI(cd1, ab1, IVP_SELI_16B_INTERLEAVE_2_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u8_x2 halide_xtensa_interleave_u8(const native_vector_u8 &a, const native_vector_u8 &b) {
    return native_vector_u8_x2(native_vector_u8_x2::from_native_vector,
                               IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_LO),
                               IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u8_x3 halide_xtensa_interleave_u8(
    const native_vector_u8 &a, const native_vector_u8 &b, const native_vector_u8 &c) {
    native_vector_u8 vRG0, vRG1, vRGB0, vRGB1, vRGB2;
    IVP_DSEL2NX8UI(vRG1, vRG0, b, a, IVP_DSELI_8B_INTERLEAVE_1);
    IVP_DSEL2NX8UI(vRGB1, vRGB0, c, vRG0, IVP_DSELI_8B_INTERLEAVE_C3_STEP_0);
    IVP_DSEL2NX8UI_H(vRGB1, vRGB2, c, vRG1, IVP_DSELI_8B_INTERLEAVE_C3_STEP_1);
    return native_vector_u8_x3(native_vector_u8_x3::from_native_vector, vRGB0, vRGB1, vRGB2);
}

HALIDE_ALWAYS_INLINE native_vector_u8_x4 halide_xtensa_interleave_u8(const native_vector_u8 &a, const native_vector_u8 &b, const native_vector_u8 &c, const native_vector_u8 &d) {
    const native_vector_u8 ab0 = IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_LO);
    const native_vector_u8 ab1 = IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_HI);
    const native_vector_u8 cd0 = IVP_SEL2NX8UI(d, c, IVP_SELI_8B_INTERLEAVE_1_LO);
    const native_vector_u8 cd1 = IVP_SEL2NX8UI(d, c, IVP_SELI_8B_INTERLEAVE_1_HI);

    return native_vector_u8_x4(native_vector_u8_x4::from_native_vector,
                               IVP_SEL2NX8UI(cd0, ab0, IVP_SELI_8B_INTERLEAVE_2_LO),
                               IVP_SEL2NX8UI(cd0, ab0, IVP_SELI_8B_INTERLEAVE_2_HI),
                               IVP_SEL2NX8UI(cd1, ab1, IVP_SELI_8B_INTERLEAVE_2_LO),
                               IVP_SEL2NX8UI(cd1, ab1, IVP_SELI_8B_INTERLEAVE_2_HI));
}

HALIDE_ALWAYS_INLINE native_mask_i8_x4 halide_xtensa_interleave_u1(const native_mask_i8 &a, const native_mask_i8 &b, const native_mask_i8 &c, const native_mask_i8 &d) {
    native_vector_u8 a8 = 0, b8 = 0, c8 = 0, d8 = 0;
    IVP_INJBI2NX8(a8, a, 0);
    IVP_INJBI2NX8(b8, b, 0);
    IVP_INJBI2NX8(c8, c, 0);
    IVP_INJBI2NX8(d8, d, 0);

    native_vector_u8_x4 interleaved8 = halide_xtensa_interleave_u8(a8, b8, c8, d8);

    native_mask_i8 ra = IVP_EXTBI2NX8(interleaved8.native_vector[0], 0);
    native_mask_i8 rb = IVP_EXTBI2NX8(interleaved8.native_vector[1], 0);
    native_mask_i8 rc = IVP_EXTBI2NX8(interleaved8.native_vector[2], 0);
    native_mask_i8 rd = IVP_EXTBI2NX8(interleaved8.native_vector[3], 0);

    return native_mask_i8_x4(native_mask_i8_x4::from_native_vector, ra, rb, rc, rd);
}

HALIDE_ALWAYS_INLINE native_mask_i8_x3 halide_xtensa_interleave_u1(const native_mask_i8 &a, const native_mask_i8 &b, const native_mask_i8 &c) {
    native_vector_u8 a8 = 0, b8 = 0, c8 = 0;
    IVP_INJBI2NX8(a8, a, 0);
    IVP_INJBI2NX8(b8, b, 0);
    IVP_INJBI2NX8(c8, c, 0);

    native_vector_u8_x3 interleaved8 = halide_xtensa_interleave_u8(a8, b8, c8);

    native_mask_i8 ra = IVP_EXTBI2NX8(interleaved8.native_vector[0], 0);
    native_mask_i8 rb = IVP_EXTBI2NX8(interleaved8.native_vector[1], 0);
    native_mask_i8 rc = IVP_EXTBI2NX8(interleaved8.native_vector[2], 0);

    return native_mask_i8_x3(native_mask_i8_x3::from_native_vector, ra, rb, rc);
}

HALIDE_ALWAYS_INLINE native_mask_i16_x3 halide_xtensa_interleave_u1(const native_mask_i16 &a, const native_mask_i16 &b, const native_mask_i16 &c) {
    native_vector_u16 a8 = 0, b8 = 0, c8 = 0;
    IVP_INJBINX16(a8, a, 0);
    IVP_INJBINX16(b8, b, 0);
    IVP_INJBINX16(c8, c, 0);

    native_vector_u16_x3 interleaved8 = halide_xtensa_interleave_u16(a8, b8, c8);

    native_mask_i16 ra = IVP_EXTBINX16(interleaved8.native_vector[0], 0);
    native_mask_i16 rb = IVP_EXTBINX16(interleaved8.native_vector[1], 0);
    native_mask_i16 rc = IVP_EXTBINX16(interleaved8.native_vector[2], 0);

    return native_mask_i16_x3(native_mask_i16_x3::from_native_vector, ra, rb, rc);
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_interleave_f32(const native_vector_f32 &a, const native_vector_f32 &b) {
    return native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                                IVP_SELN_2XF32I(b, a, IVP_SELI_32B_INTERLEAVE_1_LO),
                                IVP_SELN_2XF32I(b, a, IVP_SELI_32B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_f32_x4 halide_xtensa_interleave_f32(const native_vector_f32_x2 &a, const native_vector_f32_x2 &b) {
    return native_vector_f32_x4(native_vector_f32_x4::from_native_vector,
                                IVP_SELN_2XF32I(b.native_vector[0], a.native_vector[0], IVP_SELI_32B_INTERLEAVE_1_LO),
                                IVP_SELN_2XF32I(b.native_vector[0], a.native_vector[0], IVP_SELI_32B_INTERLEAVE_1_HI),
                                IVP_SELN_2XF32I(b.native_vector[1], a.native_vector[1], IVP_SELI_32B_INTERLEAVE_1_LO),
                                IVP_SELN_2XF32I(b.native_vector[1], a.native_vector[1], IVP_SELI_32B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_f32_x4 halide_xtensa_interleave_f32(const native_vector_f32 &a, const native_vector_f32 &b,
                                                                       const native_vector_f32 &c, const native_vector_f32 &d) {
    const native_vector_f32 ab0 = IVP_SELN_2XF32I(b, a, IVP_SELI_32B_INTERLEAVE_1_LO);
    const native_vector_f32 ab1 = IVP_SELN_2XF32I(b, a, IVP_SELI_32B_INTERLEAVE_1_HI);
    const native_vector_f32 cd0 = IVP_SELN_2XF32I(d, c, IVP_SELI_32B_INTERLEAVE_1_LO);
    const native_vector_f32 cd1 = IVP_SELN_2XF32I(d, c, IVP_SELI_32B_INTERLEAVE_1_HI);

    return native_vector_f32_x4(native_vector_f32_x4::from_native_vector,
                                IVP_SELN_2XF32I(cd0, ab0, IVP_SELI_32B_INTERLEAVE_2_LO),
                                IVP_SELN_2XF32I(cd0, ab0, IVP_SELI_32B_INTERLEAVE_2_HI),
                                IVP_SELN_2XF32I(cd1, ab1, IVP_SELI_32B_INTERLEAVE_2_LO),
                                IVP_SELN_2XF32I(cd1, ab1, IVP_SELI_32B_INTERLEAVE_2_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_extract_0_of_3_u8(const native_vector_u8 &a0, const native_vector_u8 &a1, const native_vector_u8 &a2) {
    // TODO(vksnk): there is likely a better way to do it.
    native_vector_u8 vR, vG, vB, vRG0, vRG1;
    IVP_DSEL2NX8UI(vB, vRG0, a1, a0, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_0);
    IVP_DSEL2NX8UI_H(vB, vRG1, a2, a1, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_1);
    IVP_DSEL2NX8UI(vG, vR, vRG1, vRG0, IVP_DSELI_8B_DEINTERLEAVE_1);
    return vR;
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_extract_0_of_3_u8(const native_vector_u8_x3 &a) {
    return halide_xtensa_extract_0_of_3_u8(a.native_vector[0], a.native_vector[1], a.native_vector[2]);
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_extract_0_of_3_i8(const native_vector_i8 &a0, const native_vector_i8 &a1, const native_vector_i8 &a2) {
    // TODO(aelphy): there is likely a better way to do it.
    native_vector_i8 vR, vG, vB, vRG0, vRG1;
    IVP_DSEL2NX8I(vB, vRG0, a1, a0, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_0);
    IVP_DSEL2NX8I_H(vB, vRG1, a2, a1, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_1);
    IVP_DSEL2NX8I(vG, vR, vRG1, vRG0, IVP_DSELI_8B_DEINTERLEAVE_1);
    return vR;
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_extract_0_of_3_i8(const native_vector_i8_x3 &a) {
    return halide_xtensa_extract_0_of_3_i8(a.native_vector[0], a.native_vector[1], a.native_vector[2]);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_deinterleave_even_i16(const native_vector_i16_x2 &a) {
    return IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_deinterleave_odd_i16(const native_vector_i16_x2 &a) {
    return IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_deinterleave_even_i16(const native_vector_i16_x4 &a) {
    return native_vector_i16_x2(
        native_vector_i16_x2::from_native_vector,
        halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
        halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_deinterleave_odd_i16(const native_vector_i16_x4 &a) {
    return native_vector_i16_x2(
        native_vector_i16_x2::from_native_vector,
        halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
        halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_deinterleave_even_u16(const native_vector_u16_x2 &a) {
    return IVP_SELNX16UI(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_deinterleave_odd_u16(const native_vector_u16_x2 &a) {
    return IVP_SELNX16UI(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_u16_x2 halide_xtensa_deinterleave_even_u16(const native_vector_u16_x4 &a) {
    return native_vector_u16_x2(
        native_vector_u16_x2::from_native_vector,
        halide_xtensa_deinterleave_even_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
        halide_xtensa_deinterleave_even_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_u16_x2 halide_xtensa_deinterleave_odd_u16(const native_vector_u16_x4 &a) {
    return native_vector_u16_x2(
        native_vector_u16_x2::from_native_vector,
        halide_xtensa_deinterleave_odd_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
        halide_xtensa_deinterleave_odd_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_deinterleave_even_f32(const native_vector_f32_x2 &a) {
    return IVP_SELN_2XF32I(a.native_vector[1], a.native_vector[0], IVP_SELI_32B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_deinterleave_odd_f32(const native_vector_f32_x2 &a) {
    return IVP_SELN_2XF32I(a.native_vector[1], a.native_vector[0], IVP_SELI_32B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_deinterleave_even_f32(const native_vector_f32_x4 &a) {
    return native_vector_f32_x2(
        native_vector_f32_x2::from_native_vector,
        halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
        halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_deinterleave_odd_f32(const native_vector_f32_x4 &a) {
    return native_vector_f32_x2(
        native_vector_f32_x2::from_native_vector,
        halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
        halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_extract_0_of_4_f32(const native_vector_f32_x4 &a) {
    return halide_xtensa_deinterleave_even_f32(
        native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                             halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_extract_1_of_4_f32(const native_vector_f32_x4 &a) {
    return halide_xtensa_deinterleave_even_f32(
        native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                             halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_extract_2_of_4_f32(const native_vector_f32_x4 &a) {
    return halide_xtensa_deinterleave_odd_f32(
        native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                             halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_extract_3_of_4_f32(const native_vector_f32_x4 &a) {
    return halide_xtensa_deinterleave_odd_f32(
        native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                             halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_extract_0_of_4_i16(const native_vector_i16_x4 &a) {
    return halide_xtensa_deinterleave_even_i16(
        native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                             halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_extract_1_of_4_i16(const native_vector_i16_x4 &a) {
    return halide_xtensa_deinterleave_even_i16(
        native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                             halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_extract_2_of_4_i16(const native_vector_i16_x4 &a) {
    return halide_xtensa_deinterleave_odd_i16(
        native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                             halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_extract_3_of_4_i16(const native_vector_i16_x4 &a) {
    return halide_xtensa_deinterleave_odd_i16(
        native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                             halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_extract_0_of_4_u16(const native_vector_u16_x4 &a) {
    return halide_xtensa_deinterleave_even_u16(
        native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                             halide_xtensa_deinterleave_even_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_even_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_extract_1_of_4_u16(const native_vector_u16_x4 &a) {
    return halide_xtensa_deinterleave_even_u16(
        native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                             halide_xtensa_deinterleave_odd_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_odd_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_extract_2_of_4_u16(const native_vector_u16_x4 &a) {
    return halide_xtensa_deinterleave_odd_u16(
        native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                             halide_xtensa_deinterleave_even_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_even_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_extract_3_of_4_u16(const native_vector_u16_x4 &a) {
    return halide_xtensa_deinterleave_odd_u16(
        native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                             halide_xtensa_deinterleave_odd_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
                             halide_xtensa_deinterleave_odd_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_extract_0_of_8_u16(const native_vector_u16_x8 &a) {
    return halide_xtensa_deinterleave_even_u16(
        native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                             halide_xtensa_extract_0_of_4_u16(native_vector_u16_x4(native_vector_u16_x4::from_native_vector,
                                                                                   a.native_vector[0],
                                                                                   a.native_vector[1],
                                                                                   a.native_vector[2],
                                                                                   a.native_vector[3])),
                             halide_xtensa_extract_0_of_4_u16(native_vector_u16_x4(native_vector_u16_x4::from_native_vector,
                                                                                   a.native_vector[4],
                                                                                   a.native_vector[5],
                                                                                   a.native_vector[6],
                                                                                   a.native_vector[7]))));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_slice_i16(const native_vector_i16_x2 &a, int start) {
    return IVP_SELNX16(a.native_vector[1], a.native_vector[0], IVP_SEQNX16() + native_vector_i16(start));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_slice_u16(const native_vector_u16_x2 &a, int start) {
    return IVP_SELNX16U(a.native_vector[1], a.native_vector[0], IVP_SEQNX16() + native_vector_i16(start));
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_slice_i32(const native_vector_i32_x2 &a, int start) {
    return IVP_SELN_2X32(a.native_vector[1], a.native_vector[0], IVP_SEQN_2X32() + native_vector_i32(start));
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_slice_u32(const native_vector_u32_x2 &a, int start) {
    return IVP_SELN_2X32U(a.native_vector[1], a.native_vector[0], IVP_SEQN_2X32() + native_vector_i32(start));
}

/*
HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_deinterleave_even_i8(const int8x128_t& a) {
  return  IVP_SEL2NX8I(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_deinterleave_odd_i8(const int8x128_t& a) {
  return  IVP_SEL2NX8I(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_1);
}
*/
HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_deinterleave_even_u8(const native_vector_u8_x2 &a) {
    return IVP_SEL2NX8UI(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_deinterleave_odd_u8(const native_vector_u8_x2 &a) {
    return IVP_SEL2NX8UI(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_slice_f32(const native_vector_f32_x2 &a, int start) {
    return IVP_SELN_2XF32(a.native_vector[1], a.native_vector[0], IVP_ADDN_2X32(IVP_SEQN_2X32(), native_vector_i32(start)));
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_dynamic_shuffle(const native_vector_u8_x2 &a, const native_vector_i8 &b) {
    return IVP_SEL2NX8(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_dynamic_shuffle(const native_vector_i16_x2 &a, const native_vector_i16 &b) {
    return IVP_SELNX16(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_dynamic_shuffle(const native_vector_u16_x2 &a, const native_vector_i16 &b) {
    return IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_dynamic_shuffle(const native_vector_i16_x2 &a, const native_vector_i16_x2 &b) {
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                                IVP_SELNX16(a.native_vector[1], a.native_vector[0], b.native_vector[0]),
                                IVP_SELNX16(a.native_vector[1], a.native_vector[0], b.native_vector[1]));
}

HALIDE_ALWAYS_INLINE native_vector_u16_x2 halide_xtensa_dynamic_shuffle(const native_vector_u16_x2 &a, const native_vector_i16_x2 &b) {
    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                                IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b.native_vector[0]),
                                IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b.native_vector[1]));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_dynamic_shuffle(const native_vector_f32_x2 &a, const native_vector_i32 &b) {
    return IVP_SELN_2XF32(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_sat_add_i32(const native_vector_i32 &a,
                                                                 const native_vector_i32 &b) {
    // I am not 100% about it.
    xb_vecN_2x32v one = 1;
    xb_vecN_2x64w l0 = IVP_MULN_2X32(a, one);
    IVP_MULAN_2X32(l0, b, one);
    return IVP_PACKVRN_2X64W(l0, 0);
}

HALIDE_ALWAYS_INLINE native_vector_i32_x2 halide_xtensa_sat_add_i32(const native_vector_i32_x2 &a,
                                                                    const native_vector_i32_x2 &b) {
    // I am not 100% about it.
    xb_vecN_2x32v zero = 0;
    xb_vecN_2x32v one = 1;
    xb_vecN_2x64w l0 = a.native_vector[0] * one;
    IVP_MULAN_2X32(l0, b.native_vector[0], one);
    xb_vecN_2x64w l1 = a.native_vector[1] * one;
    IVP_MULAN_2X32(l1, b.native_vector[1], one);
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, IVP_PACKVN_2X64W(l0, zero), IVP_PACKVN_2X64W(l1, zero));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_add_i16(const native_vector_i16 &a, const native_mask_i16 &p, const native_vector_i16 &b, const native_vector_i16 &c) {
    native_vector_i16 r = a;
    IVP_ADDNX16T(r, b, c, p);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_sub_i16(const native_vector_i16 &a, const native_mask_i16 &p, const native_vector_i16 &b, const native_vector_i16 &c) {
    native_vector_i16 r = a;
    IVP_SUBNX16T(r, b, c, p);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_max_i16(const native_vector_i16 &a, const native_mask_i16 &p, const native_vector_i16 &b, const native_vector_i16 &c) {
    native_vector_i16 r = a;
    IVP_MAXNX16T(r, b, c, p);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_min_i16(const native_vector_i16 &a, const native_mask_i16 &p, const native_vector_i16 &b, const native_vector_i16 &c) {
    native_vector_i16 r = a;
    IVP_MINNX16T(r, b, c, p);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_sat_add_i16(const native_mask_i16 &p, const native_vector_i16 &b, const native_vector_i16 &c, const native_vector_i16 &a) {
    native_vector_i16 r = a;
    IVP_ADDSNX16T(r, b, c, p);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_sat_sub_i16(const native_vector_i16 &a, const native_mask_i16 &p, const native_vector_i16 &b, const native_vector_i16 &c) {
    native_vector_i16 r = a;
    IVP_SUBSNX16T(r, b, c, p);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i64 halide_xtensa_widen_mul_i64(const native_vector_i32 &a, const native_vector_i32 &b) {
    return IVP_MULN_2X32(a, b);
}

HALIDE_ALWAYS_INLINE native_vector_i64 halide_xtensa_widen_mul_add_i64(const native_vector_i64 &r, const native_vector_i32 &a, const native_vector_i32 &b) {
    native_vector_i64 r1 = r;
    IVP_MULAN_2X32(r1, a, b);
    return r1;
}

HALIDE_ALWAYS_INLINE native_vector_i64 halide_xtensa_widen_mul_add_i64(const native_vector_i32 &a, const native_vector_i32 &b, const native_vector_i32 &c) {
    xb_vecN_2x64w r = IVP_MULN_2X32(c, native_vector_i32(1));
    IVP_MULAN_2X32(r, a, b);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_mul_add_i48(const native_vector_i48 &a, const native_vector_i16 &b, const native_vector_i16 &c) {
    native_vector_i48 r = a;
    IVP_MULANX16(r, b, c);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_add_u24(const native_vector_i24 &a, const native_vector_u8 &b, const native_vector_u8 &c) {
    native_vector_i24 r = a;
    IVP_MULUUA2NX8(r, b, c);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_sub_u24(const native_vector_i24 &a, const native_vector_u8 &b, const native_vector_u8 &c) {
    native_vector_i24 r = a;
    IVP_MULUUS2NX8(r, b, c);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_add_i24(const native_vector_i24 &a, const native_vector_i8 &b, const native_vector_i8 &c) {
    native_vector_i24 r = a;
    IVP_MULA2NX8(r, b, c);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_i24(const native_vector_i8 &a, const native_vector_i8 &b) {
    return IVP_MUL2NX8(a, b);
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_u24(const native_vector_u8 &a, const native_vector_u8 &b) {
    return IVP_MULUU2NX8(a, b);
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_i24(
    const native_vector_i24 &acc,
    const native_vector_i8 &a0,
    const int8_t &s0,
    const native_vector_i8 &a1,
    const int8_t &s1,
    const native_vector_i8 &a2,
    const int8_t &s2,
    const native_vector_i8 &a3,
    const int8_t &s3) {
    native_vector_i24 r = acc;
    const int8_t scalar_coef[] = {s3, s2, s1, s0};
    const xb_int32pr *__restrict coef = (const xb_int32pr *)scalar_coef;
    IVP_MULQA2N8XR8(r, a0, a1, a2, a3, coef[0]);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_i24(
    const native_vector_i24 &acc,
    const native_vector_i8 &a0,
    const native_vector_i8 &a1,
    const native_vector_i8 &a2,
    const native_vector_i8 &a3,
    const int8x4_t &s) {
    native_vector_i24 r = acc;
    IVP_MULQA2N8XR8(r, a3, a2, a1, a0, s);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_i24(
    const native_vector_i24 &acc,
    const native_vector_i8_x4 &a,
    const int8x4_t &s) {
    native_vector_i24 r = acc;
    IVP_MULQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_u24(
    const native_vector_i24 &acc,
    const native_vector_u8 &a0,
    const native_vector_u8 &a1,
    const native_vector_u8 &a2,
    const native_vector_u8 &a3,
    const uint8x4_t &s) {
    native_vector_i24 r = acc;
    IVP_MULUUQA2N8XR8(r, a3, a2, a1, a0, s);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_u24(
    const native_vector_i24 &acc,
    const native_vector_u8_x4 &a,
    const uint8x4_t &s) {
    native_vector_i24 r = acc;
    IVP_MULUUQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_by_scalar_u24(
    const native_vector_i24 &acc,
    const native_vector_u8_x4 &a,
    const uint8_t &s) {
    const xb_int32pr coef = s | (s << 8) | (s << 16) | (s << 24);

    native_vector_i24 r = acc;
    IVP_MULUUQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], coef);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24_x2 halide_xtensa_dual_widen_quad_mul_add_i24(
    const native_vector_i24_x2 &acc,
    const native_vector_i8_x4 &a,
    const int8x8_t &s) {
    native_vector_i24_x2 r(acc);
    IVP_DMULQA2N8XR8(r.native_vector[1], r.native_vector[0], a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24_x2 halide_xtensa_dual_widen_quad_mul_add_u24(
    const native_vector_i24_x2 &acc,
    const native_vector_u8_x4 &a,
    const uint8x8_t &s) {
    native_vector_i24_x2 r(acc);
    IVP_DMULUUQA2N8XR8(r.native_vector[1], r.native_vector[0], a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_pair_mul_i24(const native_vector_i8 &a, const native_vector_i8 &b,
                                                                        const native_vector_i8 &c, const native_vector_i8 &d) {
    return IVP_MULP2NX8(a, b, c, d);
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_pair_mul_add_i24(const native_vector_i24 &a, const native_vector_i8 &b,
                                                                            const native_vector_i8 &c, const native_vector_i8 &d, const native_vector_i8 &e) {
    native_vector_i24 r = a;
    IVP_MULPA2NX8(r, b, c, d, e);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_pair_mul_add_u24(const native_vector_i24 &a, const native_vector_u8 &b,
                                                                            const native_vector_u8 &c, const native_vector_u8 &d, const native_vector_u8 &e) {
    native_vector_i24 r = a;
    IVP_MULUUPA2NX8(r, b, c, d, e);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_pair_mul_u24(const native_vector_u8 &a, const native_vector_u8 &b,
                                                                        const native_vector_u8 &c, const native_vector_u8 &d) {
    return IVP_MULUUP2NX8(a, b, c, d);
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_mul_i48(const native_vector_i16 &a, const native_vector_i16 &b,
                                                                        const native_vector_i16 &c, const native_vector_i16 &d) {
    return IVP_MULPNX16(a, b, c, d);
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_mul_add_i48(const native_vector_i48 &a, const native_vector_i16 &b,
                                                                            const native_vector_i16 &c, const native_vector_i16 &d, const native_vector_i16 &e) {
    native_vector_i48 r = a;
    IVP_MULPANX16(r, b, c, d, e);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_mul_u48(const native_vector_u16 &a, const native_vector_u16 &b,
                                                                        const native_vector_u16 &c, const native_vector_u16 &d) {
    return IVP_MULUUPNX16(a, b, c, d);
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_add_by_diff_u24(const native_vector_i24 &a, const native_vector_u8 &d1,
                                                                               const native_vector_u8 &d2, const native_vector_u8 &c) {
    native_vector_i24 r = a;
    IVP_MULUUPDA2NX8(r, d1, c, d2, c);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_add_i48(const native_vector_i16 &a, const native_vector_i16 &b) {
    return IVP_ADDWNX16(a, b);
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_add_i48(const native_vector_i48 &a, const native_vector_i16 &b) {
    native_vector_i48 r = a;
    IVP_ADDWANX16(r, b, native_vector_i16(0));
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_add_i48(const native_vector_i48 &a, const native_vector_i16 &b, const native_vector_i16 &c) {
    native_vector_i48 r = a;
    IVP_ADDWANX16(r, b, c);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_add_u48(const native_vector_u16 &a, const native_vector_u16 &b) {
    return IVP_ADDWUNX16U(a, b);
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_add_u48(const native_vector_i48 &a, const native_vector_u16 &b) {
    native_vector_i48 r = a;
    IVP_ADDWUANX16U(r, b, native_vector_u16(0));
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_quad_add_i48(
    const native_vector_i16 &a, const native_vector_i16 &b,
    const native_vector_i16 &c, const native_vector_i16 &d) {
    native_vector_i48 r = IVP_ADDWNX16(a, b);
    IVP_ADDWANX16(r, c, d);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_u16>(const native_vector_u16 &src);

HALIDE_ALWAYS_INLINE native_vector_i64_x2 halide_xtensa_widen_right_mul_u64(const native_vector_u32_x2 &a, const native_vector_u16 &b) {
    native_vector_u32_x2 b32 = convert<native_vector_u32_x2, native_vector_u16>(b);

    return native_vector_i64_x2(native_vector_i64_x2::from_native_vector,
                                IVP_MULUSN_2X32(a.native_vector[0], xb_vecN_2x32Uv_rtor_xb_vecN_2x32v(b32.native_vector[0])),
                                IVP_MULUSN_2X32(a.native_vector[1], xb_vecN_2x32Uv_rtor_xb_vecN_2x32v(b32.native_vector[1])));
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_add_u48(const native_vector_i48 &a, const native_vector_u16 &b, const native_vector_u16 &c) {
    native_vector_i48 r = a;
    IVP_ADDWUANX16U(r, b, c);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_add_i24(const native_vector_i24 &a, const native_vector_i8 &b) {
    native_vector_i24 r = a;
    IVP_ADDWA2NX8(r, b, native_vector_i8(0));
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_sat_narrow_i24x_with_shift_i8(const native_vector_i24 &a, int shift) {
    return IVP_PACKVRNR2NX24(a, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_sat_narrow_i24x_with_shift_u8(const native_vector_i24 &a, int shift) {
    return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKVRNR2NX24(a, shift));
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_narrow_i24_with_shift_i16(const native_vector_i24 &a, int shift) {
    native_vector_i16 even = xb_vecNx16U_rtor_xb_vecNx16(IVP_PACKVRNR2NX24_0(a, shift));
    native_vector_i16 odd = xb_vecNx16U_rtor_xb_vecNx16(IVP_PACKVRNR2NX24_1(a, shift));
    native_vector_i16_x2 r;
    IVP_DSELNX16I(r.native_vector[1], r.native_vector[0], odd, even, IVP_DSELI_INTERLEAVE_1);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_narrow_i24_with_shift_i8(const native_vector_i24 &a, int shift) {
    return IVP_PACKVR2NX24(a, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_narrow_i24_with_shift_u8(const native_vector_i24 &a, int shift) {
    return IVP_PACKVRU2NX24(a, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i32_x2 halide_xtensa_narrow_i48_with_shift_i32(const native_vector_i48 &a, int shift) {
    native_vector_i32 even = IVP_PACKVRNRNX48_0(a, shift);
    native_vector_i32 odd = IVP_PACKVRNRNX48_1(a, shift);
    native_vector_i32_x2 r;
    IVP_DSELN_2X32I(r.native_vector[1], r.native_vector[0], odd, even, IVP_DSELI_INTERLEAVE_2);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_u32_x2 halide_xtensa_narrow_i48_with_shift_u32(const native_vector_i48 &a, int shift) {
    native_vector_u32 even = IVP_PACKVRNRNX48_0(a, shift);
    native_vector_u32 odd = IVP_PACKVRNRNX48_1(a, shift);
    native_vector_u32_x2 r;
    IVP_DSELN_2X32UI(r.native_vector[1], r.native_vector[0], odd, even, IVP_DSELI_INTERLEAVE_2);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_narrow_i48_with_shift_u16(const native_vector_i48 &a, int shift) {
    return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(a, shift));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_narrow_with_shift_i16(const native_vector_i32_x2 &a, int shift) {
    xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
    return IVP_PACKVRNRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_narrow_with_shift_u16(const native_vector_i32_x2 &a, int shift) {
    xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
    return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(wide, shift));
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_narrow_high_i32(const native_vector_i64 &a) {
    return IVP_PACKHN_2X64W(a);
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_sat_narrow_shift_i32(const native_vector_i64 &a, int shift) {
    // There is only saturation *and rounding* intrinsic, so we correct for
    // rounding by subtracting the rounding factor first.
    native_vector_i64 r = a;
    IVP_MULSN_2X32(r, 1, 1 << (shift - 1));
    return IVP_PACKVN_2X64W(r, shift);
}

HALIDE_ALWAYS_INLINE int32_t halide_xtensa_full_reduce_add_u8_to_i32(const native_vector_u8 &a) {
    return xb_int16U_rtor_uint16(IVP_RADDU2NX8(a));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_lerp_i16(const native_vector_i16 &a, const native_vector_i16 &b, uint16_t w) {
    // TODO(vksnk): Halide lerp actually uses full range, but it's not clear from the documentation
    // if we can pass unsigned type to IVP_MULPN16XR16, so just to be extra careful reduce it to 14-bit
    // for now.
    uint32_t w32 = ((uint32_t(w)) >> 0);
    uint32_t alphaMalpha = ((65536 - w32) << 16) | w32;
    xb_vecNx48 output = IVP_MULSUPN16XR16(a, b, alphaMalpha);
    IVP_DECNEGWNX48(output);
    return IVP_PACKVRNX48(output, 16);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2
convert<native_vector_i16_x2, native_vector_i8>(const native_vector_i8 &src) {
    const native_vector_i16 m = native_vector_i16(1U << (8 - 1));
    native_vector_i16 x1 = IVP_MOVNX16_FROM2NX8(
        IVP_SEL2NX8I(native_vector_i8(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
    native_vector_i16 x2 = IVP_MOVNX16_FROM2NX8(
        IVP_SEL2NX8I(native_vector_i8(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, (x1 ^ m) - m, (x2 ^ m) - m);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 convert<native_vector_u16_x2, native_vector_u8>(const native_vector_u8 &src) {
    xb_vec2Nx24 wide = src * native_vector_u8(1);
    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                                IVP_CVT16U2NX24L(wide), IVP_CVT16U2NX24H(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_u8>(const native_vector_u8 &src) {
    xb_vec2Nx24 wide = src * native_vector_u8(1);
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                                IVP_CVT16S2NX24L(wide), IVP_CVT16S2NX24H(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_i24>(const native_vector_i24 &wide) {
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                                IVP_CVT16S2NX24L(wide), IVP_CVT16S2NX24H(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 convert<native_vector_u16_x2, native_vector_i24>(const native_vector_i24 &wide) {
    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                                IVP_CVT16U2NX24L(wide), IVP_CVT16U2NX24H(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i8 convert<native_vector_i8, native_vector_i16_x2>(const native_vector_i16_x2 &src) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(src.native_vector[1], src.native_vector[0]);
    return IVP_PACKL2NX24(wide);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i8
convert<native_vector_i8, native_vector_u16_x2>(const native_vector_u16_x2 &src) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(src.native_vector[1], src.native_vector[0]);
    return IVP_PACKL2NX24(wide);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_vector_i16_x2>(const native_vector_i16_x2 &src) {
    return IVP_SEL2NX8UI(IVP_MOV2NX8U_FROMNX16(src.native_vector[1]),
                         IVP_MOV2NX8U_FROMNX16(src.native_vector[0]),
                         IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i8 convert<native_vector_i8, native_vector_i32_x4>(const native_vector_i32_x4 &src) {
    xb_vec2Nx24 wide = IVP_CVT24UNX32L(src.native_vector[1], src.native_vector[0]);
    IVP_CVT24UNX32H(wide, src.native_vector[3], src.native_vector[2]);
    return IVP_PACKL2NX24(wide);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i8
convert<native_vector_i8, native_vector_u32_x4>(const native_vector_u32_x4 &src) {
    xb_vec2Nx24 wide = IVP_CVT24UNX32L(src.native_vector[1], src.native_vector[0]);
    IVP_CVT24UNX32H(wide, src.native_vector[3], src.native_vector[2]);
    return IVP_PACKL2NX24(wide);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i8 convert<native_vector_i8, native_mask_i8>(const native_mask_i8 &src) {
    return IVP_MOV2NX8T(native_vector_i8(1), native_vector_i8(0), src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_mask_i8>(const native_mask_i8 &src) {
    return IVP_MOV2NX8UT(native_vector_u8(1), native_vector_u8(0), src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_vector_i32_x4>(const native_vector_i32_x4 &src) {
    xb_vec2Nx24 wide = IVP_CVT24UNX32L(src.native_vector[1], src.native_vector[0]);
    IVP_CVT24UNX32H(wide, src.native_vector[3], src.native_vector[2]);
    return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_vector_u16_x2>(const native_vector_u16_x2 &src) {
    return IVP_SEL2NX8UI(IVP_MOV2NX8U_FROMNX16(src.native_vector[1]),
                         IVP_MOV2NX8U_FROMNX16(src.native_vector[0]),
                         IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_mask_i16>(const native_mask_i16 &src) {
    return IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), src);
}

template<>
HALIDE_ALWAYS_INLINE native_mask_i16_x2 convert<native_mask_i16_x2, native_mask_i8>(const native_mask_i8 &src) {
    return native_mask_i16_x2(native_mask_i16_x2::from_native_vector,
                              IVP_EXTRACTBL2N(src),
                              IVP_EXTRACTBH2N(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_mask_i8>(const native_mask_i8 &src) {
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                                convert<native_vector_i16, native_mask_i16>(IVP_EXTRACTBL2N(src)),
                                convert<native_vector_i16, native_mask_i16>(IVP_EXTRACTBH2N(src)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_vector_i32_x2>(const native_vector_i32_x2 &src) {
    return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32(src.native_vector[1]),
                        IVP_MOVNX16_FROMN_2X32(src.native_vector[0]),
                        IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i48 convert<native_vector_i48, native_vector_i32_x2>(const native_vector_i32_x2 &src) {
    return IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i48 convert<native_vector_i48, native_vector_u32_x2>(const native_vector_u32_x2 &src) {
    return IVP_CVT48UNX32(src.native_vector[1], src.native_vector[0]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_vector_u32_x2>(const native_vector_u32_x2 &src) {
    return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32U(src.native_vector[1]),
                        IVP_MOVNX16_FROMN_2X32U(src.native_vector[0]),
                        IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_i32_x4>(const native_vector_i32_x4 &src) {
    xb_vecNx48 wide0 = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
    xb_vecNx48 wide1 = IVP_CVT48SNX32(src.native_vector[3], src.native_vector[2]);

    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, IVP_PACKLNX48(wide0), IVP_PACKLNX48(wide1));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_vector_i32_x2>(const native_vector_i32_x2 &src) {
    return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32(src.native_vector[1]),
                         IVP_MOVNX16_FROMN_2X32(src.native_vector[0]),
                         IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_mask_i16>(const native_mask_i16 &src) {
    return IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 convert<native_vector_u16_x2, native_mask_i8>(const native_mask_i8 &src) {
    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                                convert<native_vector_u16, native_mask_i16>(IVP_EXTRACTBL2N(src)),
                                convert<native_vector_u16, native_mask_i16>(IVP_EXTRACTBH2N(src)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_vector_u32_x2>(const native_vector_u32_x2 &src) {
    return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32U(src.native_vector[1]),
                         IVP_MOVNX16_FROMN_2X32U(src.native_vector[0]),
                         IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32 convert<native_vector_u32, native_vector_i64>(const native_vector_i64 &src) {
    return IVP_PACKLN_2X64W(src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32 convert<native_vector_i32, native_mask_i32>(const native_mask_i32 &src) {
    xb_vecN_2x32v r = 0;
    IVP_INJBIN_2X32(r, src, 0);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 convert<native_vector_i32_x4, native_vector_u8>(const native_vector_u8 &src) {
    xb_vec2Nx24 wide = src * native_vector_u8(1);
    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide),
                                IVP_CVT32S2NX24HL(wide), IVP_CVT32S2NX24HH(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x4 convert<native_vector_u32_x4, native_vector_u8>(const native_vector_u8 &src) {
    xb_vec2Nx24 wide = src * native_vector_u8(1);
    return native_vector_u32_x4(native_vector_u32_x4::from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide),
                                IVP_CVT32S2NX24HL(wide), IVP_CVT32S2NX24HH(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 convert<native_vector_i32_x4, native_vector_i24>(const native_vector_i24 &src) {
    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, IVP_CVT32S2NX24LL(src), IVP_CVT32S2NX24LH(src),
                                IVP_CVT32S2NX24HL(src), IVP_CVT32S2NX24HH(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2
convert<native_vector_i32_x2, native_vector_i16>(const native_vector_i16 &src) {
    // We could use IVP_SRAINX16, but it triggers a compiler bug on Q7.
    native_vector_i16 sign_val = IVP_SRANX16(src, 15);
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                                IVP_MOVN_2X32_FROMNX16(
                                    IVP_SELNX16UI(sign_val, src, IVP_SELI_16B_INTERLEAVE_1_LO)),
                                IVP_MOVN_2X32_FROMNX16(
                                    IVP_SELNX16UI(sign_val, src, IVP_SELI_16B_INTERLEAVE_1_HI)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4
convert<native_vector_i32_x4, native_vector_i8>(const native_vector_i8 &src) {
    native_vector_i16_x2 a = convert<native_vector_i16_x2, native_vector_i8>(src);
    native_vector_i32_x2 b = convert<native_vector_i32_x2, native_vector_i16>(a.native_vector[0]);
    native_vector_i32_x2 c = convert<native_vector_i32_x2, native_vector_i16>(a.native_vector[1]);
    return native_vector_i32_x4(
        native_vector_i32_x4::from_native_vector,
        b.native_vector[0], b.native_vector[1], c.native_vector[0], c.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 convert<native_vector_i32_x4, native_vector_i16_x2>(const native_vector_i16_x2 &src) {
    auto r0 = convert<native_vector_i32_x2, native_vector_i16>(src.native_vector[0]);
    auto r1 = convert<native_vector_i32_x2, native_vector_i16>(src.native_vector[1]);

    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, r0.native_vector[0], r0.native_vector[1],
                                r1.native_vector[0], r1.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_u16>(const native_vector_u16 &src) {
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                                IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_LO)),
                                IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_HI)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_u32_x2>(const native_vector_u32_x2 &src) {
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                                src.native_vector[0], src.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_i32_x2>(const native_vector_i32_x2 &src) {
    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector,
                                src.native_vector[0], src.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 convert<native_vector_u16_x2, native_vector_i16_x2>(const native_vector_i16_x2 &src) {
    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                                src.native_vector[0], src.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_i48>(const native_vector_i48 &src) {
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                                IVP_CVT32SNX48L(src),
                                IVP_CVT32SNX48H(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_u16>(const native_vector_u16 &src) {
    xb_vec2Nx24 wide = IVP_CVT24U2NX16(0, xb_vecNx16U_rtor_xb_vecNx16(src));
    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector,
                                xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32S2NX24LL(wide)),
                                xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32S2NX24LH(wide)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_i48>(const native_vector_i48 &src) {
    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector,
                                xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48L(src)),
                                xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48H(src)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_u16_x2>(const native_vector_u16_x2 &src) {
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, src.native_vector[0], src.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32 convert<native_vector_f32, native_vector_i32>(const native_vector_i32 &src) {
    return IVP_FLOATN_2X32(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 convert<native_vector_f32_x2, native_vector_i32_x2>(const native_vector_i32_x2 &src) {
    return native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                                convert<native_vector_f32, native_vector_i32>(src.native_vector[0]),
                                convert<native_vector_f32, native_vector_i32>(src.native_vector[1]));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x4
convert<native_vector_f32_x4, native_vector_i32_x4>(const native_vector_i32_x4 &src) {
    return native_vector_f32_x4(native_vector_f32_x4::from_native_vector,
                                convert<native_vector_f32, native_vector_i32>(src.native_vector[0]),
                                convert<native_vector_f32, native_vector_i32>(src.native_vector[1]),
                                convert<native_vector_f32, native_vector_i32>(src.native_vector[2]),
                                convert<native_vector_f32, native_vector_i32>(src.native_vector[3]));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 convert<native_vector_f32_x2, native_vector_i16>(const native_vector_i16 &src) {
    native_vector_i32_x2 tmp = convert<native_vector_i32_x2, native_vector_i16>(src);
    return convert<native_vector_f32_x2, native_vector_i32_x2>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 convert<native_vector_f32_x2, native_vector_u16>(const native_vector_u16 &src) {
    native_vector_i32_x2 tmp = convert<native_vector_i32_x2, native_vector_u16>(src);
    return convert<native_vector_f32_x2, native_vector_i32_x2>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x4
convert<native_vector_f32_x4, native_vector_i8>(const native_vector_i8 &src) {
    native_vector_i32_x4 tmp = convert<native_vector_i32_x4, native_vector_i8>(src);
    return convert<native_vector_f32_x4, native_vector_i32_x4>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32 convert<native_vector_i32, native_vector_f32>(const native_vector_f32 &src) {
    return IVP_TRUNCN_2XF32(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32 convert<native_vector_u32, native_vector_f32>(const native_vector_f32 &src) {
    return IVP_UTRUNCN_2XF32(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_f32_x2>(const native_vector_f32_x2 &src) {
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                                convert<native_vector_i32, native_vector_f32>(src.native_vector[0]),
                                convert<native_vector_i32, native_vector_f32>(src.native_vector[1]));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_f32_x2>(const native_vector_f32_x2 &src) {
    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector,
                                convert<native_vector_u32, native_vector_f32>(src.native_vector[0]),
                                convert<native_vector_u32, native_vector_f32>(src.native_vector[1]));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 convert<native_vector_f32_x2, native_vector_f16>(const native_vector_f16 &src) {
    native_vector_f32_x2 output;

    IVP_DSELN_2XF32I(
        output.native_vector[1],
        output.native_vector[0],
        IVP_CVTF32NXF16_1(src),
        IVP_CVTF32NXF16_0(src),
        IVP_DSELI_INTERLEAVE_2);

    return output;
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f16 convert<native_vector_f16, native_vector_f32_x2>(const native_vector_f32_x2 &src) {
    return IVP_SELNXF16I(
        IVP_CVTF16N_2XF32_0(src.native_vector[1]),
        IVP_CVTF16N_2XF32_0(src.native_vector[0]),
        IVP_SELI_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f16 convert<native_vector_f16, native_vector_i32_x2>(const native_vector_i32_x2 &src) {
    return convert<native_vector_f16, native_vector_f32_x2>(
        native_vector_f32_x2(
            native_vector_f32_x2::from_native_vector,
            IVP_FLOATN_2X32(src.native_vector[0], 0),
            IVP_FLOATN_2X32(src.native_vector[1], 0)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_f16>(const native_vector_f16 &src) {
    native_vector_f32_x2 tmp = convert<native_vector_f32_x2, native_vector_f16>(src);
    return convert<native_vector_i32_x2, native_vector_f32_x2>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_vector_f32_x2>(const native_vector_f32_x2 &src) {
    return convert<native_vector_u16, native_vector_u32_x2>(
        convert<native_vector_u32_x2, native_vector_f32_x2>(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_vector_f32_x2>(const native_vector_f32_x2 &src) {
    native_vector_i32_x2 tmp = convert<native_vector_i32_x2, native_vector_f32_x2>(src);
    return convert<native_vector_i16, native_vector_i32_x2>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f16 convert<native_vector_f16, native_vector_i16>(const native_vector_i16 &src) {
    return IVP_FLOAT16NX16(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_vector_f16>(const native_vector_f16 &src) {
    return IVP_TRUNC16NXF16(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f16 convert<native_vector_f16, native_vector_u16>(const native_vector_u16 &src) {
    return convert<native_vector_f16, native_vector_i16>(xb_vecNx16U_rtor_xb_vecNx16(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_vector_f16>(const native_vector_f16 &src) {
    return xb_vecNx16U_rtor_xb_vecNx16(convert<native_vector_i16, native_vector_f16>(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_vector_f32_x4>(const native_vector_f32_x4 &src) {
    native_vector_i32_x4 tmp(native_vector_i32_x4::from_native_vector,
                             convert<native_vector_i32, native_vector_f32>(src.native_vector[0]),
                             convert<native_vector_i32, native_vector_f32>(src.native_vector[1]),
                             convert<native_vector_i32, native_vector_f32>(src.native_vector[2]),
                             convert<native_vector_i32, native_vector_f32>(src.native_vector[3]));
    return convert<native_vector_u8, native_vector_i32_x4>(tmp);
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_convert_to_f32_from_i32(const native_vector_i32 &src) {
    return convert<native_vector_f32, native_vector_i32>(src);
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_convert_to_f32_from_i32(const native_vector_i32_x2 &src) {
    return convert<native_vector_f32_x2, native_vector_i32_x2>(src);
}

HALIDE_ALWAYS_INLINE native_mask_i32 halide_xtensa_slice_to_native(const native_mask_i16 &src, int index, int native_lanes, int total_lanes) {
    return (index == 0) ? IVP_EXTRACTBLN(src) : IVP_EXTRACTBHN(src);
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_convert_i16_low_i32(const native_vector_i16 &src) {
    const native_vector_i32 m = native_vector_i32(1U << (16 - 1));
    native_vector_i32 x = IVP_MOVN_2X32_FROMNX16(IVP_SELNX16I(native_vector_i16(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
    native_vector_i32 r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_convert_i16_high_i32(const native_vector_i16 &src) {
    const native_vector_i32 m = native_vector_i32(1U << (16 - 1));
    native_vector_i32 x = IVP_MOVN_2X32_FROMNX16(IVP_SELNX16I(native_vector_i16(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
    native_vector_i32 r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_convert_u16_low_i32(const native_vector_u16 &src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_convert_u16_high_i32(const native_vector_u16 &src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_convert_u16_low_u32(const native_vector_u16 &src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_convert_u16_high_u32(const native_vector_u16 &src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_i32_u16(const native_vector_i32 &src0, const native_vector_i32 &src1) {
    xb_vecNx48 wide = IVP_CVT48SNX32(src1, src0);
    return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKLNX48(wide));
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_convert_concat_i16_to_i8(const native_vector_i16 &a, const native_vector_i16 &b) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(b, a);
    return IVP_PACKL2NX24(wide);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_sat_narrow_u8(const native_vector_i16_x2 &a) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
    return IVP_PACKVRU2NX24(wide, 0);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_sat_narrow_i16(const native_vector_i32_x2 &a) {
    native_vector_i32 a0 = IVP_SLSIN_2X32(a.native_vector[0], 16);
    native_vector_i32 a1 = IVP_SLSIN_2X32(a.native_vector[1], 16);
    return IVP_MOVNX16_FROMN_2X32(IVP_SELN_2X32I(a1, a0, IVP_SELI_16B_DEINTERLEAVE_1_ODD));
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_sat_narrow_with_rounding_shift_i8(const native_vector_i16_x2 &a, uint32_t shift) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
    return IVP_PACKVR2NX24(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_sat_narrow_with_rounding_shift_u8(const native_vector_i16_x2 &a, uint32_t shift) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
    return IVP_PACKVRU2NX24(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_narrow_with_rounding_shift_i16(const native_vector_i32_x2 &a, uint32_t shift) {
    xb_vecNx48 wide = convert<native_vector_i48, native_vector_i32_x2>(a);
    // Add rounding factor.
    const uint16_t half_shift_1 = (shift - 1) >> 1;
    const uint16_t half_shift_2 = (shift - 1) - half_shift_1;
    native_vector_u16 v1 = IVP_SLLNX16U(1, half_shift_1);
    native_vector_u16 v2 = IVP_SLLNX16U(1, half_shift_2);
    IVP_MULUUANX16(wide, v1, v2);
    return IVP_PACKVRNRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_sat_narrow_with_rounding_shift_i16(const native_vector_i32_x2 &a, uint32_t shift) {
    xb_vecNx48 wide = convert<native_vector_i48, native_vector_i32_x2>(a);
    return IVP_PACKVRNX48(wide, shift);
}

// TODO(vksnk): this is pretty inefficient.
HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_sat_narrow_with_signed_rounding_shift_i16(const native_vector_i32_x2 &a, int32_t shift) {
    if (shift >= 0) {
        return halide_xtensa_sat_narrow_with_rounding_shift_i16(a, (uint32_t)shift);
    }

    return halide_xtensa_sat_narrow_i16(
        native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                             IVP_SLAN_2X32(a.native_vector[0], -shift),
                             IVP_SLAN_2X32(a.native_vector[1], -shift)));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_rounding_mul_shift_right_i16(const native_vector_i16 &a, const native_vector_i16 &b, uint16_t shift) {
    xb_vecNx48 wide = a * b;
    return IVP_PACKVRNRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_rounding_shift_right_i16(const native_vector_i16 &a, uint32_t shift) {
    xb_vecNx48 wide = a * (native_vector_i16)1;
    return IVP_PACKVRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_rounding_shift_right_i32(const native_vector_i32 &a, uint32_t shift) {
    xb_vecN_2x64w wide = a * (native_vector_i32)1;
    return IVP_PACKVRN_2X64W(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_rounding_shift_right_u32(const native_vector_u32 &a, uint32_t shift) {
    xb_vecN_2x64w wide = IVP_MULUUN_2X16X32_0((native_vector_u16)1, a);
    return IVP_PACKVRN_2X64W(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_convert_concat_i16_to_u8(const native_vector_i16 &a, const native_vector_i16 &b) {
    return IVP_SEL2NX8UI(IVP_MOV2NX8_FROMNX16(b), IVP_MOV2NX8_FROMNX16(a), IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_convert_concat_u16_to_i8(const native_vector_u16 &a, const native_vector_u16 &b) {
    return IVP_SEL2NX8I(IVP_MOV2NX8_FROMNX16(b), IVP_MOV2NX8_FROMNX16(a), IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_convert_concat_u16_to_u8(const native_vector_u16 &a, const native_vector_u16 &b) {
    return IVP_SEL2NX8UI(IVP_MOV2NX8_FROMNX16(b), IVP_MOV2NX8_FROMNX16(a), IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_i8_low_i16(const native_vector_i8 &src, int native_lanes, int total_lines) {
    const native_vector_i16 m = native_vector_i16(1U << (8 - 1));
    native_vector_i16 x = IVP_MOVNX16_FROM2NX8(IVP_SEL2NX8I(native_vector_i8(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
    native_vector_i16 r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_i8_high_i16(const native_vector_i8 &src, int native_lanes, int total_lines) {
    const native_vector_i16 m = native_vector_i16(1U << (8 - 1));
    native_vector_i16 x = IVP_MOVNX16_FROM2NX8(IVP_SEL2NX8I(native_vector_i8(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
    native_vector_i16 r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_u8_low_i16(const native_vector_u8 &src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(native_vector_u8(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_u8_high_i16(const native_vector_u8 &src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(native_vector_u8(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_u8_low_u16(const native_vector_u8 &src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(native_vector_u8(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_u8_high_u16(const native_vector_u8 &src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(native_vector_u8(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_concat_i32_to_i16(const native_vector_i32 &a, const native_vector_i32 &b) {
    return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32(b), IVP_MOVNX16_FROMN_2X32(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_concat_i32_to_u16(const native_vector_i32 &a, const native_vector_i32 &b) {
    return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32(b), IVP_MOVNX16_FROMN_2X32(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_concat_u32_to_i16(const native_vector_u32 &a, const native_vector_u32 &b) {
    return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32U(b), IVP_MOVNX16_FROMN_2X32U(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_concat_u32_to_u16(const native_vector_u32 &a, const native_vector_u32 &b) {
    return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32U(b), IVP_MOVNX16_FROMN_2X32U(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_convert_i48_low_u32(const native_vector_i48 &src, int native_lanes, int total_lines) {
    return xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48L(src));
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_convert_i48_high_u32(const native_vector_i48 &src, int native_lanes, int total_lines) {
    return xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48H(src));
}

HALIDE_ALWAYS_INLINE native_mask_i16 halide_xtensa_concat_from_native(const native_mask_i32 &a, const native_mask_i32 &b) {
    return IVP_JOINBN_2(b, a);
}

HALIDE_ALWAYS_INLINE native_mask_i8 halide_xtensa_concat_from_native(const native_mask_i16 &a, const native_mask_i16 &b) {
    return IVP_JOINBN(b, a);
}

HALIDE_ALWAYS_INLINE native_mask_i8 halide_xtensa_concat_from_native(const native_mask_i32 &a, const native_mask_i32 &b, const native_mask_i32 &c, const native_mask_i32 &d) {
    return halide_xtensa_concat_from_native(halide_xtensa_concat_from_native(a, b), halide_xtensa_concat_from_native(c, d));
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_concat_from_native(const native_vector_f32 &a, const native_vector_f32 &b) {
    return native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a, b);
}

template<typename VectorType, typename OffsetType, typename BaseType, int Lanes, bool IsTCM>
VectorType gather_load(const void *base, const OffsetType &offset) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[Lanes];
    int offsets[Lanes];
    store<OffsetType, int32_t, Lanes>(offset, &offsets[0], 0);
    for (int i = 0; i < Lanes; i++) {
        tmp[i] = ((const BaseType *)base)[offsets[i]];
    }

    return *((VectorType *)tmp);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i8 gather_load<native_vector_i8, native_vector_i32_x4, int8_t, VECTOR_WIDTH_U8, true>(const void *base, const native_vector_i32_x4 &offset) {
    auto addresses1 = native_vector_i32_x2(native_vector_i32_x2::from_native_vector, offset.native_vector[0], offset.native_vector[1]);
    auto output1 = IVP_GATHERDNX8S(
        IVP_GATHERANX8S(
            (const int8_t *)base,
            convert<native_vector_u16, native_vector_i32_x2>(addresses1)));

    auto addresses2 = native_vector_i32_x2(native_vector_i32_x2::from_native_vector, offset.native_vector[2], offset.native_vector[3]);
    auto output2 = IVP_GATHERDNX8S(
        IVP_GATHERANX8S(
            (const int8_t *)base,
            convert<native_vector_u16, native_vector_i32_x2>(addresses2)));

    // NOTE(aelphy): the intrinsic for gathering 8-bit elements extends them to 16-bit, and the conversion back to 8-bit is needed
    return convert<native_vector_i8, native_vector_i16_x2>(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, output1, output2));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u8 gather_load<native_vector_u8, native_vector_i32_x4, uint8_t, VECTOR_WIDTH_U8, true>(const void *base, const native_vector_i32_x4 &offset) {
    auto addresses1 = native_vector_i32_x2(native_vector_i32_x2::from_native_vector, offset.native_vector[0], offset.native_vector[1]);
    auto output1 = IVP_GATHERDNX8U(
        IVP_GATHERANX8U(
            (const uint8_t *)base,
            convert<native_vector_u16, native_vector_i32_x2>(addresses1)));

    auto addresses2 = native_vector_i32_x2(native_vector_i32_x2::from_native_vector, offset.native_vector[2], offset.native_vector[3]);
    auto output2 = IVP_GATHERDNX8U(
        IVP_GATHERANX8U(
            (const uint8_t *)base,
            convert<native_vector_u16, native_vector_i32_x2>(addresses2)));

    // NOTE(aelphy): the intrinsic for gathering 8-bit elements extends them to 16-bit, and the conversion back to 8-bit is needed
    return convert<native_vector_u8, native_vector_u16_x2>(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, output1, output2));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16 gather_load<native_vector_i16, native_vector_i32_x2, int16_t, VECTOR_WIDTH_U16, true>(const void *base, const native_vector_i32_x2 &offset) {
    // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
    return IVP_GATHERDNX16(
        IVP_GATHERANX16(
            (const int16_t *)base,
            convert<native_vector_u16, native_vector_i32_x2>(offset) << 1));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16_x2 gather_load<native_vector_i16_x2, native_vector_i32_x4, int16_t, 2 * VECTOR_WIDTH_I16, true>(const void *base, const native_vector_i32_x4 &offset) {
    // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
    native_vector_u16 offset0 = convert<native_vector_u16, native_vector_i32_x2>(
        native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                             offset.native_vector[0], offset.native_vector[1]));
    native_vector_u16 offset1 = convert<native_vector_u16, native_vector_i32_x2>(
        native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                             offset.native_vector[2], offset.native_vector[3]));

    auto gsr0 = IVP_GATHERANX16((const int16_t *)base, offset0 << 1);
    auto gsr1 = IVP_GATHERANX16((const int16_t *)base, offset1 << 1);

    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                                IVP_GATHERDNX16(gsr0),
                                IVP_GATHERDNX16(gsr1));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u16 gather_load<native_vector_u16, native_vector_i32_x2, uint16_t, VECTOR_WIDTH_U16, true>(const void *base, const native_vector_i32_x2 &offset) {
    // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
    return IVP_GATHERDNX16U(
        IVP_GATHERANX16U(
            (const uint16_t *)base,
            convert<native_vector_u16, native_vector_i32_x2>(offset) << 1));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32 gather_load<native_vector_i32, native_vector_i32, int32_t, VECTOR_WIDTH_I32, true>(const void *base, const native_vector_i32 &offset) {
    // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
    return IVP_GATHERDN_2X32(
        IVP_GATHERAN_2X32(
            (const int32_t *)base,
            xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(offset) << 2));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u32 gather_load<native_vector_u32, native_vector_i32, uint32_t, VECTOR_WIDTH_I32, true>(const void *base, const native_vector_i32 &offset) {
    // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
    return IVP_GATHERDN_2X32U(
        IVP_GATHERAN_2X32U(
            (const uint32_t *)base,
            xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(offset) << 2));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_f32 gather_load<native_vector_f32, native_vector_i32, float, VECTOR_WIDTH_F32, true>(const void *base, const native_vector_i32 &offset) {
    // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
    return IVP_GATHERDN_2XF32(
        IVP_GATHERAN_2XF32(
            (const float *)base,
            xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(offset) << 2));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_f32_x2 gather_load<native_vector_f32_x2, native_vector_i32_x2, float, 2 * VECTOR_WIDTH_F32, true>(const void *base, const native_vector_i32_x2 &offset) {
    // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
    auto gsr0 = IVP_GATHERAN_2XF32((const float *)base,
                                   xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(offset.native_vector[0]) << 2);
    auto gsr1 = IVP_GATHERAN_2XF32((const float *)base,
                                   xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(offset.native_vector[1]) << 2);

    return native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                                IVP_GATHERDN_2XF32(gsr0),
                                IVP_GATHERDN_2XF32(gsr1));
}

HALIDE_ALWAYS_INLINE native_vector_u16
halide_xtensa_mul_add_u16(const native_vector_u16 &a, const native_vector_u16 &b, const native_vector_u16 &c) {
    native_vector_u16 r = a;
    IVP_MULANX16UPACKL(r, b, c);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24
halide_xtensa_widen_add_u24(const native_vector_u8 &a, const native_vector_u8 &b) {
    native_vector_i24 r;
    r = IVP_ADDWU2NX8U(a, b);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24
halide_xtensa_widen_accum_u24(const native_vector_i24 &a, const native_vector_u8 &b) {
    native_vector_i24 r = a;
    IVP_ADDWUA2NX8U(r, b, native_vector_u8(0));
    return r;
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8
convert<native_vector_u8, native_vector_u32_x4>(const native_vector_u32_x4 &src) {
    xb_vec2Nx24 wide = IVP_CVT24UNX32L(src.native_vector[1], src.native_vector[0]);
    IVP_CVT24UNX32H(wide, src.native_vector[3], src.native_vector[2]);
    return IVP_PACKL2NX24(wide);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x4
convert<native_vector_u32_x4, native_vector_i24>(const native_vector_i24 &src) {
    return native_vector_u32_x4(native_vector_u32_x4::from_native_vector, IVP_CVT32S2NX24LL(src), IVP_CVT32S2NX24LH(src),
                                IVP_CVT32S2NX24HL(src), IVP_CVT32S2NX24HH(src));
}

HALIDE_ALWAYS_INLINE native_vector_u32
halide_xtensa_div_32_by_low16_of_32(native_vector_u32 &a, native_vector_u32 &b) {
    native_vector_u32 quotient, remainder;
    IVP_DIVN_2X32X16U(quotient, remainder, a, IVP_MOVNX16_FROMN_2X32(b), 0);
    return quotient;
}

HALIDE_ALWAYS_INLINE native_vector_u32
halide_xtensa_div32(native_vector_u32 dividend, native_vector_u32 divisor) {
    xb_vecN_2x32Uv nsa;
    xb_vecNx16U vec_divisor;
    xb_vecN_2x32Uv quotent;
    xb_vecN_2x32Uv reminder;
    vboolN_2 predicate;

    nsa = IVP_NSAUN_2X32U(divisor);
    predicate = IVP_LTUN_2X32U(16, nsa);
    nsa = IVP_MOVN_2X32UT(0, (xb_vecN_2x32Uv)16 - nsa, predicate);
    xb_vecN_2x32Uv divisor_nsa = IVP_SRLN_2X32U(divisor, nsa);

    vec_divisor = IVP_MOVNX16_FROMN_2X32U(divisor_nsa);
    IVP_DIVN_2X32X16U(quotent, reminder, dividend, vec_divisor, 0);
    quotent = IVP_SRLN_2X32U(quotent, nsa);

    xb_vecN_2x64w dividend_wide = IVP_MULUUN_2X16X32_0(IVP_MOVNX16_FROMN_2X32U(quotent), divisor);
    xb_vecN_2x32Uv dividend_tmp = IVP_PACKLN_2X96(dividend_wide);
    predicate = IVP_LTUN_2X32U(dividend, dividend_tmp);
    IVP_SUBN_2X32UT(quotent, quotent, 1, predicate);
    return quotent;
}

HALIDE_ALWAYS_INLINE native_vector_u16
halide_xtensa_narrow_with_rounding_shift_u16(const native_vector_u32_x2 &a, uint32_t shift) {
    xb_vecNx48 wide = convert<native_vector_i48, native_vector_u32_x2>(a);
    // Add rounding factor.
    native_vector_u16 v1 = IVP_SLLNX16U(1, (shift - 1));
    IVP_MULUUANX16(wide, v1, 1);
    return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(wide, shift));
}

HALIDE_ALWAYS_INLINE native_vector_u16
halide_xtensa_narrow_i48_with_rounding_shift_u16(const native_vector_i48 &a, uint32_t shift) {
    xb_vecNx48 wide = a;
    if (15 == shift) {
        return IVP_PACKQNX48(a);
    }
    // Add rounding factor.
    native_vector_u16 v1 = IVP_SLLNX16U(1, (shift - 1));
    IVP_MULUUANX16(wide, v1, 1);
    return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(wide, shift));
}

HALIDE_ALWAYS_INLINE native_vector_i48
halide_xtensa_widen_mul_sub_i48(const native_vector_i48 &a, const native_vector_i16 &b, const native_vector_i16 &c) {
    native_vector_i48 r = a;
    IVP_MULSNX16(r, b, c);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u8
gather_load<native_vector_u8, native_vector_i16_x2, uint8_t, VECTOR_WIDTH_U8, true>(const void *base, const native_vector_i16_x2 &offset) {
    auto addresses1 = xb_vecNx16_rtor_xb_vecNx16U(offset.native_vector[0]);
    auto output1 = IVP_GATHERDNX8U(
        IVP_GATHERANX8U(
            (const uint8_t *)base,
            (addresses1)));

    auto addresses2 = xb_vecNx16_rtor_xb_vecNx16U(offset.native_vector[1]);
    auto output2 = IVP_GATHERDNX8U(
        IVP_GATHERANX8U(
            (const uint8_t *)base,
            (addresses2)));

    // NOTE(aelphy): the intrinsic for gathering 8-bit elements extends them to 16-bit, and the conversion back to 8-bit is needed
    return convert<native_vector_u8, native_vector_u16_x2>(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, output1, output2));
}
