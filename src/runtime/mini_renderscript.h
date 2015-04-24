#define RS_VERSION 21

#ifdef __cplusplus
extern "C" {
#endif
extern int __android_log_print(int, const char *, const char *, ...);
#ifdef __cplusplus
}
#endif

enum {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,    /* only for SetMinPriority() */

  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,

  ANDROID_LOG_SILENT,     /* only for SetMinPriority(); must be last */
};

#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__);
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__);
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__);

enum RSError {
    RS_SUCCESS = 0,  ///< No error
    RS_ERROR_INVALID_PARAMETER =
        1,  ///< An invalid parameter was passed to a function
    RS_ERROR_RUNTIME_ERROR =
        2,  ///< The RenderScript driver returned an error; this is
    ///< often indicative of a kernel that crashed
    RS_ERROR_INVALID_ELEMENT =
        3,  ///< An invalid Element was passed to a function
    RS_ERROR_MAX = 9999
};

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RsBitmapCallback_t)(void *);

typedef struct {
    uint32_t colorMin;
    uint32_t colorPref;
    uint32_t alphaMin;
    uint32_t alphaPref;
    uint32_t depthMin;
    uint32_t depthPref;
    uint32_t stencilMin;
    uint32_t stencilPref;
    uint32_t samplesMin;
    uint32_t samplesPref;
    float samplesQ;
} RsSurfaceConfig;

enum RsMessageToClientType {
    RS_MESSAGE_TO_CLIENT_NONE = 0,
    RS_MESSAGE_TO_CLIENT_EXCEPTION = 1,
    RS_MESSAGE_TO_CLIENT_RESIZE = 2,
    RS_MESSAGE_TO_CLIENT_ERROR = 3,
    RS_MESSAGE_TO_CLIENT_USER = 4,
    RS_MESSAGE_TO_CLIENT_NEW_BUFFER = 5
};

enum RsTextureTarget { RS_TEXTURE_2D, RS_TEXTURE_CUBE };

enum RsDepthFunc {
    RS_DEPTH_FUNC_ALWAYS,
    RS_DEPTH_FUNC_LESS,
    RS_DEPTH_FUNC_LEQUAL,
    RS_DEPTH_FUNC_GREATER,
    RS_DEPTH_FUNC_GEQUAL,
    RS_DEPTH_FUNC_EQUAL,
    RS_DEPTH_FUNC_NOTEQUAL
};

enum RsBlendSrcFunc {
    RS_BLEND_SRC_ZERO,  // 0
    RS_BLEND_SRC_ONE,  // 1
    RS_BLEND_SRC_DST_COLOR,  // 2
    RS_BLEND_SRC_ONE_MINUS_DST_COLOR,  // 3
    RS_BLEND_SRC_SRC_ALPHA,  // 4
    RS_BLEND_SRC_ONE_MINUS_SRC_ALPHA,  // 5
    RS_BLEND_SRC_DST_ALPHA,  // 6
    RS_BLEND_SRC_ONE_MINUS_DST_ALPHA,  // 7
    RS_BLEND_SRC_SRC_ALPHA_SATURATE,  // 8
    RS_BLEND_SRC_INVALID = 100,
};

enum RsBlendDstFunc {
    RS_BLEND_DST_ZERO,  // 0
    RS_BLEND_DST_ONE,  // 1
    RS_BLEND_DST_SRC_COLOR,  // 2
    RS_BLEND_DST_ONE_MINUS_SRC_COLOR,  // 3
    RS_BLEND_DST_SRC_ALPHA,  // 4
    RS_BLEND_DST_ONE_MINUS_SRC_ALPHA,  // 5
    RS_BLEND_DST_DST_ALPHA,  // 6
    RS_BLEND_DST_ONE_MINUS_DST_ALPHA,  // 7

    RS_BLEND_DST_INVALID = 100,
};

enum RsTexEnvMode {
    RS_TEX_ENV_MODE_NONE,
    RS_TEX_ENV_MODE_REPLACE,
    RS_TEX_ENV_MODE_MODULATE,
    RS_TEX_ENV_MODE_DECAL
};

enum RsProgramParam {
    RS_PROGRAM_PARAM_INPUT,
    RS_PROGRAM_PARAM_OUTPUT,
    RS_PROGRAM_PARAM_CONSTANT,
    RS_PROGRAM_PARAM_TEXTURE_TYPE,
};

enum RsPrimitive {
    RS_PRIMITIVE_POINT,
    RS_PRIMITIVE_LINE,
    RS_PRIMITIVE_LINE_STRIP,
    RS_PRIMITIVE_TRIANGLE,
    RS_PRIMITIVE_TRIANGLE_STRIP,
    RS_PRIMITIVE_TRIANGLE_FAN,

    RS_PRIMITIVE_INVALID = 100,
};

enum RsPathPrimitive {
    RS_PATH_PRIMITIVE_QUADRATIC_BEZIER,
    RS_PATH_PRIMITIVE_CUBIC_BEZIER
};

enum RsAnimationInterpolation {
    RS_ANIMATION_INTERPOLATION_STEP,
    RS_ANIMATION_INTERPOLATION_LINEAR,
    RS_ANIMATION_INTERPOLATION_BEZIER,
    RS_ANIMATION_INTERPOLATION_CARDINAL,
    RS_ANIMATION_INTERPOLATION_HERMITE,
    RS_ANIMATION_INTERPOLATION_BSPLINE
};

enum RsAnimationEdge {
    RS_ANIMATION_EDGE_UNDEFINED,
    RS_ANIMATION_EDGE_CONSTANT,
    RS_ANIMATION_EDGE_GRADIENT,
    RS_ANIMATION_EDGE_CYCLE,
    RS_ANIMATION_EDGE_OSCILLATE,
    RS_ANIMATION_EDGE_CYLE_RELATIVE
};

enum RsA3DClassID {
    RS_A3D_CLASS_ID_UNKNOWN,
    RS_A3D_CLASS_ID_MESH,
    RS_A3D_CLASS_ID_TYPE,
    RS_A3D_CLASS_ID_ELEMENT,
    RS_A3D_CLASS_ID_ALLOCATION,
    RS_A3D_CLASS_ID_PROGRAM_VERTEX,
    RS_A3D_CLASS_ID_PROGRAM_RASTER,
    RS_A3D_CLASS_ID_PROGRAM_FRAGMENT,
    RS_A3D_CLASS_ID_PROGRAM_STORE,
    RS_A3D_CLASS_ID_SAMPLER,
    RS_A3D_CLASS_ID_ANIMATION,
    RS_A3D_CLASS_ID_ADAPTER_1D,
    RS_A3D_CLASS_ID_ADAPTER_2D,
    RS_A3D_CLASS_ID_SCRIPT_C,
    RS_A3D_CLASS_ID_SCRIPT_KERNEL_ID,
    RS_A3D_CLASS_ID_SCRIPT_FIELD_ID,
    RS_A3D_CLASS_ID_SCRIPT_METHOD_ID,
    RS_A3D_CLASS_ID_SCRIPT_GROUP,
    RS_A3D_CLASS_ID_CLOSURE,
    RS_A3D_CLASS_ID_SCRIPT_GROUP2,
    RS_A3D_CLASS_ID_SCRIPT_INVOKE_ID
};

enum RsCullMode {
    RS_CULL_BACK,
    RS_CULL_FRONT,
    RS_CULL_NONE,
    RS_CULL_INVALID = 100,
};

enum RsScriptIntrinsicID {
    RS_SCRIPT_INTRINSIC_ID_UNDEFINED = 0,
    RS_SCRIPT_INTRINSIC_ID_CONVOLVE_3x3 = 1,
    RS_SCRIPT_INTRINSIC_ID_COLOR_MATRIX = 2,
    RS_SCRIPT_INTRINSIC_ID_LUT = 3,
    RS_SCRIPT_INTRINSIC_ID_CONVOLVE_5x5 = 4,
    RS_SCRIPT_INTRINSIC_ID_BLUR = 5,
    RS_SCRIPT_INTRINSIC_ID_YUV_TO_RGB = 6,
    RS_SCRIPT_INTRINSIC_ID_BLEND = 7,
    RS_SCRIPT_INTRINSIC_ID_3DLUT = 8,
    RS_SCRIPT_INTRINSIC_ID_HISTOGRAM = 9,
    // unused 10, 11
    RS_SCRIPT_INTRINSIC_ID_RESIZE = 12,
    RS_SCRIPT_INTRINSIC_ID_BLAS = 13,
    RS_SCRIPT_INTRINSIC_ID_OEM_START = 0x10000000
};

typedef struct {
    RsA3DClassID classID;
    const char *objectName;
} RsFileIndexEntry;

enum RsThreadPriorities {
    RS_THREAD_PRIORITY_LOW = 15,
    RS_THREAD_PRIORITY_NORMAL_GRAPHICS = -8,
    RS_THREAD_PRIORITY_NORMAL = -1,
    RS_THREAD_PRIORITY_LOW_LATENCY = -4
};

// Structure for rs.spec functions

// typedef struct {
//     RsElement e;
//     uint32_t dimX;
//     uint32_t dimY;
//     uint32_t dimZ;
//     bool mipmaps;
//     bool faces;
//     uint32_t yuv;
//     uint32_t array0;
//     uint32_t array1;
//     uint32_t array2;
//     uint32_t array3;
// } RsTypeCreateParams;

#ifdef __cplusplus
};
#endif

typedef void *RsAsyncVoidPtr;

typedef void *RsAdapter1D;
typedef void *RsAdapter2D;
typedef void *RsAllocation;
typedef void *RsAnimation;
typedef void *RsClosure;
typedef void *RsContext;
typedef void *RsDevice;
typedef void *RsElement;
typedef void *RsFile;
typedef void *RsFont;
typedef void *RsSampler;
typedef void *RsScript;
typedef void *RsScriptKernelID;
typedef void *RsScriptInvokeID;
typedef void *RsScriptFieldID;
typedef void *RsScriptMethodID;
typedef void *RsScriptGroup;
typedef void *RsScriptGroup2;
typedef void *RsMesh;
typedef void *RsPath;
typedef void *RsType;
typedef void *RsObjectBase;

typedef void *RsProgram;
typedef void *RsProgramVertex;
typedef void *RsProgramFragment;
typedef void *RsProgramStore;
typedef void *RsProgramRaster;

typedef void *RsNativeWindow;

typedef void (*RsBitmapCallback_t)(void *);

typedef struct { float m[16]; } rs_matrix4x4;

typedef struct { float m[9]; } rs_matrix3x3;

typedef struct { float m[4]; } rs_matrix2x2;

enum RsDeviceParam { RS_DEVICE_PARAM_FORCE_SOFTWARE_GL, RS_DEVICE_PARAM_COUNT };

enum RsContextType {
    RS_CONTEXT_TYPE_NORMAL,
    RS_CONTEXT_TYPE_DEBUG,
    RS_CONTEXT_TYPE_PROFILE
};

enum RsAllocationUsageType {
    RS_ALLOCATION_USAGE_SCRIPT = 0x0001,
    RS_ALLOCATION_USAGE_GRAPHICS_TEXTURE = 0x0002,
    RS_ALLOCATION_USAGE_GRAPHICS_VERTEX = 0x0004,
    RS_ALLOCATION_USAGE_GRAPHICS_CONSTANTS = 0x0008,
    RS_ALLOCATION_USAGE_GRAPHICS_RENDER_TARGET = 0x0010,
    RS_ALLOCATION_USAGE_IO_INPUT = 0x0020,
    RS_ALLOCATION_USAGE_IO_OUTPUT = 0x0040,
    RS_ALLOCATION_USAGE_SHARED = 0x0080,

    RS_ALLOCATION_USAGE_ALL = 0x00FF
};

enum RsAllocationMipmapControl {
    RS_ALLOCATION_MIPMAP_NONE = 0,
    RS_ALLOCATION_MIPMAP_FULL = 1,
    RS_ALLOCATION_MIPMAP_ON_SYNC_TO_TEXTURE = 2
};

enum RsAllocationCubemapFace {
    RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_X = 0,
    RS_ALLOCATION_CUBEMAP_FACE_NEGATIVE_X = 1,
    RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_Y = 2,
    RS_ALLOCATION_CUBEMAP_FACE_NEGATIVE_Y = 3,
    RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_Z = 4,
    RS_ALLOCATION_CUBEMAP_FACE_NEGATIVE_Z = 5
};

enum RsDataType {
    RS_TYPE_NONE,
    RS_TYPE_FLOAT_16,
    RS_TYPE_FLOAT_32,
    RS_TYPE_FLOAT_64,
    RS_TYPE_SIGNED_8,
    RS_TYPE_SIGNED_16,
    RS_TYPE_SIGNED_32,
    RS_TYPE_SIGNED_64,
    RS_TYPE_UNSIGNED_8,
    RS_TYPE_UNSIGNED_16,
    RS_TYPE_UNSIGNED_32,
    RS_TYPE_UNSIGNED_64,

    RS_TYPE_BOOLEAN,

    RS_TYPE_UNSIGNED_5_6_5,
    RS_TYPE_UNSIGNED_5_5_5_1,
    RS_TYPE_UNSIGNED_4_4_4_4,

    RS_TYPE_MATRIX_4X4,
    RS_TYPE_MATRIX_3X3,
    RS_TYPE_MATRIX_2X2,

    RS_TYPE_ELEMENT = 1000,
    RS_TYPE_TYPE,
    RS_TYPE_ALLOCATION,
    RS_TYPE_SAMPLER,
    RS_TYPE_SCRIPT,
    RS_TYPE_MESH,
    RS_TYPE_PROGRAM_FRAGMENT,
    RS_TYPE_PROGRAM_VERTEX,
    RS_TYPE_PROGRAM_RASTER,
    RS_TYPE_PROGRAM_STORE,
    RS_TYPE_FONT,

    RS_TYPE_INVALID = 10000,
};

enum RsDataKind {
    RS_KIND_USER,

    RS_KIND_PIXEL_L = 7,
    RS_KIND_PIXEL_A,
    RS_KIND_PIXEL_LA,
    RS_KIND_PIXEL_RGB,
    RS_KIND_PIXEL_RGBA,
    RS_KIND_PIXEL_DEPTH,
    RS_KIND_PIXEL_YUV,

    RS_KIND_INVALID = 100,
};

enum RsSamplerParam {
    RS_SAMPLER_MIN_FILTER,
    RS_SAMPLER_MAG_FILTER,
    RS_SAMPLER_WRAP_S,
    RS_SAMPLER_WRAP_T,
    RS_SAMPLER_WRAP_R,
    RS_SAMPLER_ANISO
};

enum RsSamplerValue {
    RS_SAMPLER_NEAREST,
    RS_SAMPLER_LINEAR,
    RS_SAMPLER_LINEAR_MIP_LINEAR,
    RS_SAMPLER_WRAP,
    RS_SAMPLER_CLAMP,
    RS_SAMPLER_LINEAR_MIP_NEAREST,
    RS_SAMPLER_MIRRORED_REPEAT,

    RS_SAMPLER_INVALID = 100,
};

enum RsDimension {
    RS_DIMENSION_X,
    RS_DIMENSION_Y,
    RS_DIMENSION_Z,
    RS_DIMENSION_LOD,
    RS_DIMENSION_FACE,

    RS_DIMENSION_ARRAY_0 = 100,
    RS_DIMENSION_ARRAY_1,
    RS_DIMENSION_ARRAY_2,
    RS_DIMENSION_ARRAY_3,
    RS_DIMENSION_MAX = RS_DIMENSION_ARRAY_3
};

enum RsError {
    RS_ERROR_NONE = 0,
    RS_ERROR_BAD_SHADER = 1,
    RS_ERROR_BAD_SCRIPT = 2,
    RS_ERROR_BAD_VALUE = 3,
    RS_ERROR_OUT_OF_MEMORY = 4,
    RS_ERROR_DRIVER = 5,

    // Errors that only occur in the debug context.
    RS_ERROR_FATAL_DEBUG = 0x0800,

    RS_ERROR_FATAL_UNKNOWN = 0x1000,
    RS_ERROR_FATAL_DRIVER = 0x1001,
    RS_ERROR_FATAL_PROGRAM_LINK = 0x1002
};

enum RsForEachStrategy {
    RS_FOR_EACH_STRATEGY_SERIAL = 0,
    RS_FOR_EACH_STRATEGY_DONT_CARE = 1,
    RS_FOR_EACH_STRATEGY_DST_LINEAR = 2,
    RS_FOR_EACH_STRATEGY_TILE_SMALL = 3,
    RS_FOR_EACH_STRATEGY_TILE_MEDIUM = 4,
    RS_FOR_EACH_STRATEGY_TILE_LARGE = 5
};

// Script to Script
typedef struct {
    enum RsForEachStrategy strategy;
    uint32_t xStart;
    uint32_t xEnd;
    uint32_t yStart;
    uint32_t yEnd;
    uint32_t zStart;
    uint32_t zEnd;
    uint32_t arrayStart;
    uint32_t arrayEnd;
    uint32_t array2Start;
    uint32_t array2End;
    uint32_t array3Start;
    uint32_t array3End;
    uint32_t array4Start;
    uint32_t array4End;

} RsScriptCall;

enum RsContextFlags {
    RS_CONTEXT_SYNCHRONOUS = 0x0001,
    RS_CONTEXT_LOW_LATENCY = 0x0002,
    RS_CONTEXT_LOW_POWER = 0x0004
};

enum RsBlasTranspose {
    RsBlasNoTrans = 111,
    RsBlasTrans = 112,
    RsBlasConjTrans = 113
};

enum RsBlasUplo { RsBlasUpper = 121, RsBlasLower = 122 };

enum RsBlasDiag { RsBlasNonUnit = 131, RsBlasUnit = 132 };

enum RsBlasSide { RsBlasLeft = 141, RsBlasRight = 142 };

enum RsBlasFunction {
    RsBlas_nop = 0,
    RsBlas_sdsdot,
    RsBlas_dsdot,
    RsBlas_sdot,
    RsBlas_ddot,
    RsBlas_cdotu_sub,
    RsBlas_cdotc_sub,
    RsBlas_zdotu_sub,
    RsBlas_zdotc_sub,
    RsBlas_snrm2,
    RsBlas_sasum,
    RsBlas_dnrm2,
    RsBlas_dasum,
    RsBlas_scnrm2,
    RsBlas_scasum,
    RsBlas_dznrm2,
    RsBlas_dzasum,
    RsBlas_isamax,
    RsBlas_idamax,
    RsBlas_icamax,
    RsBlas_izamax,
    RsBlas_sswap,
    RsBlas_scopy,
    RsBlas_saxpy,
    RsBlas_dswap,
    RsBlas_dcopy,
    RsBlas_daxpy,
    RsBlas_cswap,
    RsBlas_ccopy,
    RsBlas_caxpy,
    RsBlas_zswap,
    RsBlas_zcopy,
    RsBlas_zaxpy,
    RsBlas_srotg,
    RsBlas_srotmg,
    RsBlas_srot,
    RsBlas_srotm,
    RsBlas_drotg,
    RsBlas_drotmg,
    RsBlas_drot,
    RsBlas_drotm,
    RsBlas_sscal,
    RsBlas_dscal,
    RsBlas_cscal,
    RsBlas_zscal,
    RsBlas_csscal,
    RsBlas_zdscal,
    RsBlas_sgemv,
    RsBlas_sgbmv,
    RsBlas_strmv,
    RsBlas_stbmv,
    RsBlas_stpmv,
    RsBlas_strsv,
    RsBlas_stbsv,
    RsBlas_stpsv,
    RsBlas_dgemv,
    RsBlas_dgbmv,
    RsBlas_dtrmv,
    RsBlas_dtbmv,
    RsBlas_dtpmv,
    RsBlas_dtrsv,
    RsBlas_dtbsv,
    RsBlas_dtpsv,
    RsBlas_cgemv,
    RsBlas_cgbmv,
    RsBlas_ctrmv,
    RsBlas_ctbmv,
    RsBlas_ctpmv,
    RsBlas_ctrsv,
    RsBlas_ctbsv,
    RsBlas_ctpsv,
    RsBlas_zgemv,
    RsBlas_zgbmv,
    RsBlas_ztrmv,
    RsBlas_ztbmv,
    RsBlas_ztpmv,
    RsBlas_ztrsv,
    RsBlas_ztbsv,
    RsBlas_ztpsv,
    RsBlas_ssymv,
    RsBlas_ssbmv,
    RsBlas_sspmv,
    RsBlas_sger,
    RsBlas_ssyr,
    RsBlas_sspr,
    RsBlas_ssyr2,
    RsBlas_sspr2,
    RsBlas_dsymv,
    RsBlas_dsbmv,
    RsBlas_dspmv,
    RsBlas_dger,
    RsBlas_dsyr,
    RsBlas_dspr,
    RsBlas_dsyr2,
    RsBlas_dspr2,
    RsBlas_chemv,
    RsBlas_chbmv,
    RsBlas_chpmv,
    RsBlas_cgeru,
    RsBlas_cgerc,
    RsBlas_cher,
    RsBlas_chpr,
    RsBlas_cher2,
    RsBlas_chpr2,
    RsBlas_zhemv,
    RsBlas_zhbmv,
    RsBlas_zhpmv,
    RsBlas_zgeru,
    RsBlas_zgerc,
    RsBlas_zher,
    RsBlas_zhpr,
    RsBlas_zher2,
    RsBlas_zhpr2,
    RsBlas_sgemm,
    RsBlas_ssymm,
    RsBlas_ssyrk,
    RsBlas_ssyr2k,
    RsBlas_strmm,
    RsBlas_strsm,
    RsBlas_dgemm,
    RsBlas_dsymm,
    RsBlas_dsyrk,
    RsBlas_dsyr2k,
    RsBlas_dtrmm,
    RsBlas_dtrsm,
    RsBlas_cgemm,
    RsBlas_csymm,
    RsBlas_csyrk,
    RsBlas_csyr2k,
    RsBlas_ctrmm,
    RsBlas_ctrsm,
    RsBlas_zgemm,
    RsBlas_zsymm,
    RsBlas_zsyrk,
    RsBlas_zsyr2k,
    RsBlas_ztrmm,
    RsBlas_ztrsm,
    RsBlas_chemm,
    RsBlas_cherk,
    RsBlas_cher2k,
    RsBlas_zhemm,
    RsBlas_zherk,
    RsBlas_zher2k
};

// custom complex types because of NDK support
typedef struct {
    float r;
    float i;
} RsFloatComplex;

typedef struct {
    double r;
    double i;
} RsDoubleComplex;

typedef union {
    float f;
    RsFloatComplex c;
    double d;
    RsDoubleComplex z;
} RsBlasScalar;

typedef struct {
    RsBlasFunction func;
    RsBlasTranspose transA;
    RsBlasTranspose transB;
    RsBlasUplo uplo;
    RsBlasDiag diag;
    RsBlasSide side;
    int M;
    int N;
    int K;
    RsBlasScalar alpha;
    RsBlasScalar beta;
    int incX;
    int incY;
    int KL;
    int KU;
} RsBlasCall;

//
// API definition below taken from https://android.googlesource.com/platform/frameworks/rs/+/master/cpp/rsDispatch.h.
//
typedef void (*SetNativeLibDirFnPtr)(RsContext con, const char *nativeLibDir, size_t length);
typedef const void* (*AllocationGetTypeFnPtr)(RsContext con, RsAllocation va);
typedef void (*TypeGetNativeDataFnPtr)(RsContext, RsType, uintptr_t *typeData, uint32_t typeDataSize);
typedef void (*ElementGetNativeDataFnPtr)(RsContext, RsElement, uintptr_t *elemData, uint32_t elemDataSize);
typedef void (*ElementGetSubElementsFnPtr)(RsContext, RsElement, uintptr_t *ids, const char **names, uint32_t *arraySizes, uint32_t dataSize);
typedef RsDevice (*DeviceCreateFnPtr) ();
typedef void (*DeviceDestroyFnPtr) (RsDevice dev);
typedef void (*DeviceSetConfigFnPtr) (RsDevice dev, RsDeviceParam p, int32_t value);
typedef RsContext (*ContextCreateFnPtr)(RsDevice vdev, uint32_t version, uint32_t sdkVersion, RsContextType ct, uint32_t flags);
typedef void (*GetNameFnPtr)(RsContext, void * obj, const char **name);
typedef RsClosure (*ClosureCreateFnPtr)(RsContext, RsScriptKernelID, RsAllocation, RsScriptFieldID*, size_t, uintptr_t*, size_t, int*, size_t, RsClosure*, size_t, RsScriptFieldID*, size_t);
typedef RsClosure (*InvokeClosureCreateFnPtr)(RsContext, RsScriptInvokeID, const void*, const size_t, const RsScriptFieldID*, const size_t, const uintptr_t*, const size_t, const int*, const size_t);
typedef void (*ClosureSetArgFnPtr)(RsContext, RsClosure, uint32_t, uintptr_t, size_t);
typedef void (*ClosureSetGlobalFnPtr)(RsContext, RsClosure, RsScriptFieldID, uintptr_t, size_t);
typedef void (*ContextDestroyFnPtr) (RsContext);
typedef RsMessageToClientType (*ContextGetMessageFnPtr) (RsContext, void*, size_t, size_t*, size_t, uint32_t*, size_t);
typedef RsMessageToClientType (*ContextPeekMessageFnPtr) (RsContext, size_t*, size_t, uint32_t*, size_t);
typedef void (*ContextSendMessageFnPtr) (RsContext, uint32_t, const uint8_t*, size_t);
typedef void (*ContextInitToClientFnPtr) (RsContext);
typedef void (*ContextDeinitToClientFnPtr) (RsContext);
typedef RsType (*TypeCreateFnPtr) (RsContext, RsElement, uint32_t, uint32_t, uint32_t, bool, bool, uint32_t);
typedef RsAllocation (*AllocationCreateTypedFnPtr) (RsContext, RsType, RsAllocationMipmapControl, uint32_t, uintptr_t);
typedef RsAllocation (*AllocationCreateFromBitmapFnPtr) (RsContext, RsType, RsAllocationMipmapControl, const void*, size_t, uint32_t);
typedef RsAllocation (*AllocationCubeCreateFromBitmapFnPtr) (RsContext, RsType, RsAllocationMipmapControl, const void*, size_t, uint32_t);
typedef RsNativeWindow (*AllocationGetSurfaceFnPtr) (RsContext, RsAllocation);
typedef void (*AllocationSetSurfaceFnPtr) (RsContext, RsAllocation, RsNativeWindow);
typedef void (*ContextFinishFnPtr) (RsContext);
typedef void (*ContextDumpFnPtr) (RsContext, int32_t);
typedef void (*ContextSetPriorityFnPtr) (RsContext, int32_t);
typedef void (*AssignNameFnPtr) (RsContext, RsObjectBase, const char*, size_t);
typedef void (*ObjDestroyFnPtr) (RsContext, RsAsyncVoidPtr);
typedef RsElement (*ElementCreateFnPtr) (RsContext, RsDataType, RsDataKind, bool, uint32_t);
typedef RsElement (*ElementCreate2FnPtr) (RsContext, const RsElement*, size_t, const char**, size_t, const size_t*, const uint32_t*, size_t);
typedef void (*AllocationCopyToBitmapFnPtr) (RsContext, RsAllocation, void*, size_t);
typedef void (*Allocation1DDataFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, const void*, size_t);
typedef void (*Allocation1DElementDataFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, const void*, size_t, size_t);
typedef void (*AllocationElementDataFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, uint32_t, const void*, size_t, size_t);
typedef void (*Allocation2DDataFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, RsAllocationCubemapFace, uint32_t, uint32_t, const void*, size_t, size_t);
typedef void (*Allocation3DDataFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const void*, size_t, size_t);
typedef void (*AllocationGenerateMipmapsFnPtr) (RsContext, RsAllocation);
typedef void (*AllocationReadFnPtr) (RsContext, RsAllocation, void*, size_t);
typedef void (*Allocation1DReadFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, void*, size_t);
typedef void (*AllocationElementReadFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, uint32_t, void*, size_t, size_t);
typedef void (*Allocation2DReadFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, RsAllocationCubemapFace, uint32_t, uint32_t, void*, size_t, size_t);
typedef void (*Allocation3DReadFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void*, size_t, size_t);
typedef void (*AllocationSyncAllFnPtr) (RsContext, RsAllocation, RsAllocationUsageType);
typedef void (*AllocationResize1DFnPtr) (RsContext, RsAllocation, uint32_t);
typedef void (*AllocationCopy2DRangeFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, RsAllocation, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*AllocationCopy3DRangeFnPtr) (RsContext, RsAllocation, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, RsAllocation, uint32_t, uint32_t, uint32_t, uint32_t);
typedef RsSampler (*SamplerCreateFnPtr) (RsContext, RsSamplerValue, RsSamplerValue, RsSamplerValue, RsSamplerValue, RsSamplerValue, float);
typedef void (*ScriptBindAllocationFnPtr) (RsContext, RsScript, RsAllocation, uint32_t);
typedef void (*ScriptSetTimeZoneFnPtr) (RsContext, RsScript, const char*, size_t);
typedef void (*ScriptInvokeFnPtr) (RsContext, RsScript, uint32_t);
typedef void (*ScriptInvokeVFnPtr) (RsContext, RsScript, uint32_t, const void*, size_t);
typedef void (*ScriptForEachFnPtr) (RsContext, RsScript, uint32_t, RsAllocation, RsAllocation, const void*, size_t, const RsScriptCall*, size_t);
typedef void (*ScriptSetVarIFnPtr) (RsContext, RsScript, uint32_t, int);
typedef void (*ScriptSetVarObjFnPtr) (RsContext, RsScript, uint32_t, RsObjectBase);
typedef void (*ScriptSetVarJFnPtr) (RsContext, RsScript, uint32_t, int64_t);
typedef void (*ScriptSetVarFFnPtr) (RsContext, RsScript, uint32_t, float);
typedef void (*ScriptSetVarDFnPtr) (RsContext, RsScript, uint32_t, double);
typedef void (*ScriptSetVarVFnPtr) (RsContext, RsScript, uint32_t, const void*, size_t);
typedef void (*ScriptGetVarVFnPtr) (RsContext, RsScript, uint32_t, void*, size_t);
typedef void (*ScriptSetVarVEFnPtr) (RsContext, RsScript, uint32_t, const void*, size_t, RsElement, const uint32_t*, size_t);
typedef RsScript (*ScriptCCreateFnPtr) (RsContext, const char*, size_t, const char*, size_t, const char*, size_t);
typedef RsScript (*ScriptIntrinsicCreateFnPtr) (RsContext, uint32_t id, RsElement);
typedef RsScriptKernelID (*ScriptKernelIDCreateFnPtr) (RsContext, RsScript, int, int);
typedef RsScriptInvokeID (*ScriptInvokeIDCreateFnPtr) (RsContext, RsScript, int);
typedef RsScriptFieldID (*ScriptFieldIDCreateFnPtr) (RsContext, RsScript, int);
typedef RsScriptGroup (*ScriptGroupCreateFnPtr) (RsContext, RsScriptKernelID*, size_t, RsScriptKernelID*, size_t, RsScriptKernelID*, size_t, RsScriptFieldID*, size_t, const RsType*, size_t);
typedef RsScriptGroup2 (*ScriptGroup2CreateFnPtr)(RsContext, const char*, size_t, const char*, size_t, RsClosure*, size_t);
typedef void (*ScriptGroupSetOutputFnPtr) (RsContext, RsScriptGroup, RsScriptKernelID, RsAllocation);
typedef void (*ScriptGroupSetInputFnPtr) (RsContext, RsScriptGroup, RsScriptKernelID, RsAllocation);
typedef void (*ScriptGroupExecuteFnPtr) (RsContext, RsScriptGroup);
typedef void (*AllocationIoSendFnPtr) (RsContext, RsAllocation);
typedef void (*AllocationIoReceiveFnPtr) (RsContext, RsAllocation);
typedef void * (*AllocationGetPointerFnPtr) (RsContext, RsAllocation, uint32_t lod, RsAllocationCubemapFace face, uint32_t z, uint32_t array, size_t *stride, size_t stride_len);
struct dispatchTable {
    SetNativeLibDirFnPtr SetNativeLibDir;
    // inserted by hand from rs.h
    AllocationGetTypeFnPtr AllocationGetType;
    TypeGetNativeDataFnPtr TypeGetNativeData;
    ElementGetNativeDataFnPtr ElementGetNativeData;
    ElementGetSubElementsFnPtr ElementGetSubElements;
    DeviceCreateFnPtr DeviceCreate;
    DeviceDestroyFnPtr DeviceDestroy;
    DeviceSetConfigFnPtr DeviceSetConfig;
    ContextCreateFnPtr ContextCreate;
    GetNameFnPtr GetName;
    // generated from rs.spec
    ContextDestroyFnPtr ContextDestroy;
    ContextGetMessageFnPtr ContextGetMessage;
    ContextPeekMessageFnPtr ContextPeekMessage;
    ContextSendMessageFnPtr ContextSendMessage;
    ContextInitToClientFnPtr ContextInitToClient;
    ContextDeinitToClientFnPtr ContextDeinitToClient;
    TypeCreateFnPtr TypeCreate;
    AllocationCreateTypedFnPtr AllocationCreateTyped;
    AllocationCreateFromBitmapFnPtr AllocationCreateFromBitmap;
    AllocationCubeCreateFromBitmapFnPtr AllocationCubeCreateFromBitmap;
    AllocationGetSurfaceFnPtr AllocationGetSurface;
    AllocationSetSurfaceFnPtr AllocationSetSurface;
    ClosureCreateFnPtr ClosureCreate;
    InvokeClosureCreateFnPtr InvokeClosureCreate;
    ClosureSetArgFnPtr ClosureSetArg;
    ClosureSetGlobalFnPtr ClosureSetGlobal;
    ContextFinishFnPtr ContextFinish;
    ContextDumpFnPtr ContextDump;
    ContextSetPriorityFnPtr ContextSetPriority;
    AssignNameFnPtr AssignName;
    ObjDestroyFnPtr ObjDestroy;
    ElementCreateFnPtr ElementCreate;
    ElementCreate2FnPtr ElementCreate2;
    AllocationCopyToBitmapFnPtr AllocationCopyToBitmap;
    Allocation1DDataFnPtr Allocation1DData;
    Allocation1DElementDataFnPtr Allocation1DElementData;
    AllocationElementDataFnPtr AllocationElementData;
    Allocation2DDataFnPtr Allocation2DData;
    Allocation3DDataFnPtr Allocation3DData;
    AllocationGenerateMipmapsFnPtr AllocationGenerateMipmaps;
    AllocationReadFnPtr AllocationRead;
    Allocation1DReadFnPtr Allocation1DRead;
    AllocationElementReadFnPtr AllocationElementRead;
    Allocation2DReadFnPtr Allocation2DRead;
    Allocation3DReadFnPtr Allocation3DRead;
    AllocationSyncAllFnPtr AllocationSyncAll;
    AllocationResize1DFnPtr AllocationResize1D;
    AllocationCopy2DRangeFnPtr AllocationCopy2DRange;
    AllocationCopy3DRangeFnPtr AllocationCopy3DRange;
    SamplerCreateFnPtr SamplerCreate;
    ScriptBindAllocationFnPtr ScriptBindAllocation;
    ScriptSetTimeZoneFnPtr ScriptSetTimeZone;
    ScriptInvokeFnPtr ScriptInvoke;
    ScriptInvokeVFnPtr ScriptInvokeV;
    ScriptForEachFnPtr ScriptForEach;
    ScriptSetVarIFnPtr ScriptSetVarI;
    ScriptSetVarObjFnPtr ScriptSetVarObj;
    ScriptSetVarJFnPtr ScriptSetVarJ;
    ScriptSetVarFFnPtr ScriptSetVarF;
    ScriptSetVarDFnPtr ScriptSetVarD;
    ScriptSetVarVFnPtr ScriptSetVarV;
    ScriptGetVarVFnPtr ScriptGetVarV;
    ScriptSetVarVEFnPtr ScriptSetVarVE;
    ScriptCCreateFnPtr ScriptCCreate;
    ScriptIntrinsicCreateFnPtr ScriptIntrinsicCreate;
    ScriptKernelIDCreateFnPtr ScriptKernelIDCreate;
    ScriptInvokeIDCreateFnPtr ScriptInvokeIDCreate;
    ScriptFieldIDCreateFnPtr ScriptFieldIDCreate;
    ScriptGroupCreateFnPtr ScriptGroupCreate;
    ScriptGroup2CreateFnPtr ScriptGroup2Create;
    ScriptGroupSetOutputFnPtr ScriptGroupSetOutput;
    ScriptGroupSetInputFnPtr ScriptGroupSetInput;
    ScriptGroupExecuteFnPtr ScriptGroupExecute;
    AllocationIoSendFnPtr AllocationIoSend;
    AllocationIoReceiveFnPtr AllocationIoReceive;
    AllocationGetPointerFnPtr AllocationGetPointer;
};

// device_api set to 23 binds all functions available.
WEAK bool loadSymbols(void *handle, dispatchTable &dispatchTab,
                      int device_api = 23);
