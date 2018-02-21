#ifndef HALIDE_MINI_AMDGPU_H
#define HALIDE_MINI_AMDGPU_H

namespace Halide { namespace Runtime { namespace Internal { namespace Amdgpu {

#define HIPAPI

typedef void* hipDeviceptr_t;

typedef int hipDevice_t;
typedef struct ihipCtx_t *hipCtx_t;
typedef struct ihipModule_t *hipModule_t;
typedef struct ihipModuleSymbol_t *hipFunction_t;
typedef struct ihipStream_t *hipStream_t;
typedef struct ihipEvent_t *hipEvent_t;
typedef struct hipArray *hipArray_t;

typedef enum hipMemcpyKind {
    hipMemcpyHostToHost = 0,	///< Host-to-Host Copy
    hipMemcpyHostToDevice = 1,  ///< Host-to-Device Copy
    hipMemcpyDeviceToHost = 2,  ///< Device-to-Host Copy
    hipMemcpyDeviceToDevice =3, ///< Device-to-Device Copy
    hipMemcpyDefault = 4      	///< Runtime will automatically determine copy-kind based on virtual addresses.
} hipMemcpyKind;


typedef enum hipJitOption {
  hipJitOptionMaxRegisters = 0,
  hipJitOptionThreadsPerBlock,
  hipJitOptionWallTime,
  hipJitOptionInfoLogBuffer,
  hipJitOptionInfoLogBufferSizeBytes,
  hipJitOptionErrorLogBuffer,
  hipJitOptionErrorLogBufferSizeBytes,
  hipJitOptionOptimizationLevel,
  hipJitOptionTargetFromContext,
  hipJitOptionTarget,
  hipJitOptionFallbackStrategy,
  hipJitOptionGenerateDebugInfo,
  hipJitOptionLogVerbose,
  hipJitOptionGenerateLineInfo,
  hipJitOptionCacheMode,
  hipJitOptionSm3xOpt,
  hipJitOptionFastCompile,
  hipJitOptionNumOptions
} hipJitOption;

typedef enum hipError_t {
    hipSuccess                      = 0,
    hipErrorOutOfMemory             = 2,
    hipErrorNotInitialized          = 3,
    hipErrorDeinitialized           = 4,
    hipErrorProfilerDisabled        = 5,
    hipErrorProfilerNotInitialized  = 6,
    hipErrorProfilerAlreadyStarted  = 7,
    hipErrorProfilerAlreadyStopped  = 8,
    hipErrorInsufficientDriver      = 35,
    hipErrorInvalidImage            = 200,
    hipErrorInvalidContext          = 201,
    hipErrorContextAlreadyCurrent   = 202,
    hipErrorMapFailed               = 205,
    hipErrorUnmapFailed             = 206,
    hipErrorArrayIsMapped           = 207,
    hipErrorAlreadyMapped           = 208,
    hipErrorNoBinaryForGpu          = 209,
    hipErrorAlreadyAcquired         = 210,
    hipErrorNotMapped               = 211,
    hipErrorNotMappedAsArray        = 212,
    hipErrorNotMappedAsPointer      = 213,
    hipErrorECCNotCorrectable       = 214,
    hipErrorUnsupportedLimit        = 215,
    hipErrorContextAlreadyInUse     = 216,
    hipErrorPeerAccessUnsupported   = 217,
    hipErrorInvalidKernelFile       = 218,
    hipErrorInvalidGraphicsContext  = 219,
    hipErrorInvalidSource           = 300,
    hipErrorFileNotFound            = 301,
    hipErrorSharedObjectSymbolNotFound = 302,
    hipErrorSharedObjectInitFailed  = 303,
    hipErrorOperatingSystem         = 304,
    hipErrorSetOnActiveProcess      = 305,
    hipErrorInvalidHandle           = 400,
    hipErrorNotFound                = 500,
    hipErrorIllegalAddress          = 700,
    hipErrorInvalidSymbol           = 701,

    hipErrorMissingConfiguration    = 1001,
    hipErrorMemoryAllocation        = 1002,
    hipErrorInitializationError     = 1003,
    hipErrorLaunchFailure           = 1004,
    hipErrorPriorLaunchFailure      = 1005,
    hipErrorLaunchTimeOut           = 1006,
    hipErrorLaunchOutOfResources    = 1007,
    hipErrorInvalidDeviceFunction   = 1008,
    hipErrorInvalidConfiguration    = 1009,
    hipErrorInvalidDevice           = 1010,
    hipErrorInvalidValue            = 1011,
    hipErrorInvalidDevicePointer    = 1017,
    hipErrorInvalidMemcpyDirection  = 1021,
    hipErrorUnknown                 = 1030,
    hipErrorInvalidResourceHandle   = 1033,
    hipErrorNotReady                = 1034,

    hipErrorNoDevice                = 1038,
    hipErrorPeerAccessAlreadyEnabled = 1050,

    hipErrorPeerAccessNotEnabled    = 1051,
    hipErrorRuntimeMemory           = 1052,
    hipErrorRuntimeOther            = 1053,
    hipErrorHostMemoryAlreadyRegistered = 1061,
    hipErrorHostMemoryNotRegistered = 1062,
    hipErrorMapBufferObjectFailed = 1071,
    hipErrorTbd
} hipError_t;

typedef enum hipDeviceAttribute_t {
    hipDeviceAttributeMaxThreadsPerBlock,                   ///< Maximum number of threads per block.
    hipDeviceAttributeMaxBlockDimX,                         ///< Maximum x-dimension of a block.
    hipDeviceAttributeMaxBlockDimY,                         ///< Maximum y-dimension of a block.
    hipDeviceAttributeMaxBlockDimZ,                         ///< Maximum z-dimension of a block.
    hipDeviceAttributeMaxGridDimX,                          ///< Maximum x-dimension of a grid.
    hipDeviceAttributeMaxGridDimY,                          ///< Maximum y-dimension of a grid.
    hipDeviceAttributeMaxGridDimZ,                          ///< Maximum z-dimension of a grid.
    hipDeviceAttributeMaxSharedMemoryPerBlock,              ///< Maximum shared memory available per block in bytes.
    hipDeviceAttributeTotalConstantMemory,                  ///< Constant memory size in bytes.
    hipDeviceAttributeWarpSize,                             ///< Warp size in threads.
    hipDeviceAttributeMaxRegistersPerBlock,                 ///< Maximum number of 32-bit registers available to a thread block. This number is shared by all thread blocks simultaneously resident on a multiprocessor.
    hipDeviceAttributeClockRate,                            ///< Peak clock frequency in kilohertz.
    hipDeviceAttributeMemoryClockRate,                      ///< Peak memory clock frequency in kilohertz.
    hipDeviceAttributeMemoryBusWidth,                       ///< Global memory bus width in bits.
    hipDeviceAttributeMultiprocessorCount,                  ///< Number of multiprocessors on the device.
    hipDeviceAttributeComputeMode,                          ///< Compute mode that device is currently in.
    hipDeviceAttributeL2CacheSize,                          ///< Size of L2 cache in bytes. 0 if the device doesn't have L2 cache.
    hipDeviceAttributeMaxThreadsPerMultiProcessor,          ///< Maximum resident threads per multiprocessor.
    hipDeviceAttributeComputeCapabilityMajor,               ///< Major compute capability version number.
    hipDeviceAttributeComputeCapabilityMinor,               ///< Minor compute capability version number.
    hipDeviceAttributeConcurrentKernels,                    ///< Device can possibly execute multiple kernels concurrently.
    hipDeviceAttributePciBusId,                             ///< PCI Bus ID.
    hipDeviceAttributePciDeviceId,                          ///< PCI Device ID.
    hipDeviceAttributeMaxSharedMemoryPerMultiprocessor,     ///< Maximum Shared Memory Per Multiprocessor.
    hipDeviceAttributeIsMultiGpuBoard,                      ///< Multiple GPU devices.
} hipDeviceAttribute_t;

typedef enum hipMemoryType {
    hipMemoryTypeHost,    ///< Memory is physically located on host
    hipMemoryTypeDevice,  ///< Memory is physically located on device. (see deviceId for specific device)
    hipMemoryTypeArray,   ///< Array memory, physically located on device. (see deviceId for specific device)
    hipMemoryTypeUnified  ///< Not used currently
} hipMemoryType;

struct hipPos {
  size_t x, y, z;
};

struct hipPitchedPtr
{
    void   *ptr;
    size_t  pitch;
    size_t  xsize;
    size_t  ysize;
};

struct hipExtent {
    size_t width;
    size_t height;
    size_t depth;
};

struct hipMemcpy3DParms {
    hipArray_t            srcArray;
    struct hipPos         srcPos;
    struct hipPitchedPtr  srcPtr;
    hipArray_t            dstArray;
    struct hipPos         dstPos;
    struct hipPitchedPtr  dstPtr;

    struct hipExtent      extent;
    hipMemcpyKind    kind;

    size_t                Depth;
    size_t                Height;
    size_t                WidthInBytes;
    hipDeviceptr_t        dstDevice;
    size_t                dstHeight;
    void *                dstHost;
    size_t                dstLOD;
    hipMemoryType         dstMemoryType;
    size_t                dstPitch;
    size_t                dstXInBytes;
    size_t                dstY;
    size_t                dstZ;
    void *                reserved0;
    void *                reserved1;
    hipDeviceptr_t        srcDevice;
    size_t                srcHeight;
    const void *          srcHost;
    size_t                srcLOD;
    hipMemoryType          srcMemoryType;
    size_t                srcPitch;
    size_t                srcXInBytes;
    size_t                srcY;
    size_t                srcZ;
};

#define hipPointerAttributeContext 1

}}}}

#endif
