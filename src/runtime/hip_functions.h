#ifndef HIP_FN
#define HIP_FN(ret, fn, args)
#endif
#ifndef HIP_FN_OPTIONAL
#define HIP_FN_OPTIONAL(ret, fn, args)
#endif

HIP_FN(hipError_t, hipInit, (unsigned int Flags));
HIP_FN(hipError_t, hipGetDeviceCount, (int *count));
HIP_FN(hipError_t, hipDeviceGet, (hipDevice_t *device, int ordinal));
HIP_FN(hipError_t, hipDeviceGetAttribute, (int*, hipDeviceAttribute_t, hipDevice_t));
HIP_FN(hipError_t, hipDeviceGetName, (char*, int len, hipDevice_t dev));
HIP_FN(hipError_t, hipDeviceTotalMem, (size_t *, hipDevice_t));
HIP_FN(hipError_t, hipCtxCreate, (hipCtx_t *pctx, unsigned int flags, hipDevice_t dev));
HIP_FN(hipError_t, hipCtxDestroy, (hipCtx_t pctx));
HIP_FN(hipError_t, hipModuleLoadData, (hipModule_t *module, const void *image));
HIP_FN(hipError_t, hipModuleLoadDataEx, (hipModule_t *module, const void *image, unsigned int numOptions, hipJitOption *options, void **optionValues));
HIP_FN(hipError_t, hipModuleUnload, (hipModule_t module));
HIP_FN(hipError_t, hipModuleGetFunction, (hipFunction_t *hfunc, hipModule_t hmod, const char *name));
HIP_FN(hipError_t, hipMalloc, (hipDeviceptr_t *dptr, size_t bytesize));
HIP_FN(hipError_t, hipFree, (hipDeviceptr_t dptr));
HIP_FN(hipError_t, hipMemcpyHtoD, (hipDeviceptr_t dstDevice, const void *srcHost, size_t BytesCount));
HIP_FN(hipError_t, hipMemcpyDtoH, (void *dstHost, hipDeviceptr_t dstDevice, size_t BytesCount));
HIP_FN(hipError_t, hipMemcpyDtoD, (hipDeviceptr_t dstDevice, hipDeviceptr_t srcDevice, size_t BytesCount));
HIP_FN(hipError_t, hipMemcpy3D, (const hipMemcpy3DParms *pCopy));
HIP_FN(hipError_t, hipModuleLaunchKernel, (hipFunction_t f,
unsigned int gridDimX,
unsigned int gridDimY,
unsigned int gridDimZ,
unsigned int blockDimX,
unsigned int blockDimY,
unsigned int blockDimZ,
unsigned int sharedMemBytes,
hipStream_t hStream,
void** kernelParams,
void** extra));

HIP_FN(hipError_t, hipCtxSynchronize, ());
HIP_FN(hipError_t, hipCtxPushCurrent, (hipCtx_t ctx));
HIP_FN(hipError_t, hipCtxPopCurrent, (hipCtx_t *pctx));
HIP_FN(hipError_t, hipPointerGetAttributes, (void *result, int query, hipDeviceptr_t ptr));
HIP_FN(hipError_t, hipStreamSynchronize, (hipStream_t hStream));
HIP_FN(hipError_t, hipCtxGetApiVersion, (hipCtx_t ctx, int *));
HIP_FN(hipError_t, hipProfilerStop, ());

#undef HIP_FN
