// Note that this header intentionally does not use include
// guards. The intended usage of this file is to define the meaning of
// the CUDA_FN macros, and then include this file, sometimes
// repeatedly within the same compilation unit.

#ifndef CUDA_FN
#define CUDA_FN(ret, fn, args)
#endif
#ifndef CUDA_FN_OPTIONAL
#define CUDA_FN_OPTIONAL(ret, fn, args)
#endif
#ifndef CUDA_FN_3020
#define CUDA_FN_3020(ret, fn, fn_3020, args) CUDA_FN(ret, fn, args)
#endif
#ifndef CUDA_FN_4000
#define CUDA_FN_4000(ret, fn, fn_4000, args) CUDA_FN(ret, fn, args)
#endif

CUDA_FN(CUresult, cuInit, (unsigned int Flags));
CUDA_FN(CUresult, cuDeviceGetCount, (int *count));
CUDA_FN(CUresult, cuDeviceGet, (CUdevice *device, int ordinal));
CUDA_FN(CUresult, cuDeviceGetAttribute, (int *, CUdevice_attribute attrib, CUdevice dev));
CUDA_FN(CUresult, cuDeviceGetName, (char *, int len, CUdevice dev));
CUDA_FN(CUresult, cuDeviceTotalMem, (size_t *, CUdevice dev));
CUDA_FN_3020(CUresult, cuCtxCreate, cuCtxCreate_v2, (CUcontext *pctx, unsigned int flags, CUdevice dev));
CUDA_FN_4000(CUresult, cuCtxDestroy, cuCtxDestroy_v2, (CUcontext pctx));
CUDA_FN(CUresult, cuProfilerStop, ());
CUDA_FN(CUresult, cuCtxGetApiVersion, (CUcontext ctx, unsigned int *version));
CUDA_FN(CUresult, cuCtxGetDevice, (CUdevice *));
CUDA_FN(CUresult, cuModuleLoadData, (CUmodule *module, const void *image));
CUDA_FN(CUresult, cuModuleLoadDataEx, (CUmodule *module, const void *image, unsigned int numOptions, CUjit_option* options, void** optionValues));
CUDA_FN(CUresult, cuModuleUnload, (CUmodule module));
CUDA_FN(CUresult, cuModuleGetFunction, (CUfunction *hfunc, CUmodule hmod, const char *name));
CUDA_FN_3020(CUresult, cuMemAlloc, cuMemAlloc_v2, (CUdeviceptr *dptr, size_t bytesize));
CUDA_FN_3020(CUresult, cuMemFree, cuMemFree_v2, (CUdeviceptr dptr));
CUDA_FN_3020(CUresult, cuMemcpyHtoD, cuMemcpyHtoD_v2, (CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount));
CUDA_FN_3020(CUresult, cuMemcpyDtoH, cuMemcpyDtoH_v2, (void *dstHost, CUdeviceptr srcDevice, size_t ByteCount));
CUDA_FN_3020(CUresult, cuMemcpyDtoD, cuMemcpyDtoD_v2, (CUdeviceptr dstHost, CUdeviceptr srcDevice, size_t ByteCount));
CUDA_FN_3020(CUresult, cuMemcpy3D, cuMemcpy3D_v2, (const CUDA_MEMCPY3D *pCopy));
CUDA_FN(CUresult, cuLaunchKernel, (CUfunction f,
                                   unsigned int gridDimX,
                                   unsigned int gridDimY,
                                   unsigned int gridDimZ,
                                   unsigned int blockDimX,
                                   unsigned int blockDimY,
                                   unsigned int blockDimZ,
                                   unsigned int sharedMemBytes,
                                   CUstream hStream,
                                   void **kernelParams,
                                   void **extra));
CUDA_FN(CUresult, cuCtxSynchronize, ());

CUDA_FN_4000(CUresult, cuCtxPushCurrent, cuCtxPushCurrent_v2, (CUcontext ctx));
CUDA_FN_4000(CUresult, cuCtxPopCurrent, cuCtxPopCurrent_v2, (CUcontext *pctx));

CUDA_FN(CUresult, cuPointerGetAttribute, (void *result, int query, CUdeviceptr ptr));

CUDA_FN_OPTIONAL(CUresult, cuStreamSynchronize, (CUstream hStream));
CUDA_FN_OPTIONAL(CUresult, cuLaunchHostFunc, (CUstream hStream, void (*fn)(void *), void *));

#undef CUDA_FN
#undef CUDA_FN_OPTIONAL
#undef CUDA_FN_3020
#undef CUDA_FN_4000
