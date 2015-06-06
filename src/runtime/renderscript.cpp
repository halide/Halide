#include "runtime_internal.h"
#include "device_interface.h"
#include "HalideRuntimeRenderscript.h"

extern "C" void *dlopen(const char *, int);
extern "C" void *dlsym(void *, const char *);
extern "C" char* *dlerror();

#define     RTLD_LAZY   0x1
#define     RTLD_LOCAL   0x4

#define LOG_API(...) ALOGV(__VA_ARGS__)

#define LOG_TAG "halide_rs"

#include "mini_renderscript.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Renderscript {

extern WEAK halide_device_interface renderscript_device_interface;

WEAK const char *get_error_name(RSError err);
WEAK int create_renderscript_context(void *user_context, RsDevice *dev, RsContext *ctx);

// An Renderscript context/device/synchronization lock defined in
// this module with weak linkage
RsContext WEAK context = 0;
RsDevice WEAK device = 0;
volatile int WEAK thread_lock = 0;
}
}
}
}  // namespace Halide::Runtime::Internal:Renderscript:

using namespace Halide::Runtime::Internal::Renderscript;

extern "C" {

extern void free(void *);
extern void *malloc(size_t);
extern const char *strstr(const char *, const char *);
extern char *strncpy(char *dst, const char *src, size_t n);
extern int atoi(const char *);
extern char *getenv(const char *);

#ifdef DEBUG_RUNTIME
extern int halide_start_clock(void *user_context);
extern int64_t halide_current_time_ns(void *user_context);
#endif

// The default implementation of halide_renderscript_acquire_context uses the global
// pointers above, and serializes access with a spin lock.
WEAK int halide_renderscript_acquire_context(void *user_context, RsDevice *dev,
                                   RsContext *ctx, bool create = true) {
    halide_assert(user_context, dev != NULL);
    halide_assert(user_context, ctx != NULL);
    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) {
    }

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, &context != NULL);
    if (context == NULL && create) {
        int error = create_renderscript_context(user_context, &device, &context);
        if (error != RS_SUCCESS) {
            __sync_lock_release(&thread_lock);
            return error;
        }
    }

    *dev = device;
    *ctx = context;
    return 0;
}

WEAK int halide_renderscript_release_context(void *user_context) {
    __sync_lock_release(&thread_lock);
    return 0;
}

extern int __system_property_get(const char *name, char *value);

// runtime stores the cache dir. It's a global, so it's initialized to null.
WEAK const char *halide_renderscript_cache_dir;

// Setter for the above. Can be called by the user.
WEAK void halide_set_renderscript_cache_dir(const char *c) {
    halide_renderscript_cache_dir = c;
}

// Getter. The user can alternatively override this to return a custom cache dir based on the user context.
WEAK const char *halide_get_renderscript_cache_dir(void *user_context) {
    // Prevent multiple threads both trying to initialize the trace
    // file at the same time.
    if (halide_renderscript_cache_dir) {
        return halide_renderscript_cache_dir;
    } else {
        const char *name = getenv("HL_RENDERSCRIPT_CACHE_DIR");
        if (name) {
            return name;
        } else {
            return "/mnt/sdcard";
        }
    }
}

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Renderscript {

WEAK int property_get(const char *key, char *value, const char *default_value) {
    int len;

    len = __system_property_get(key, value);
    if (len > 0) {
        return len;
    }

    if (default_value) {
        len = strlen(default_value);
        memcpy(value, default_value, len + 1);
    }
    return len;
}

static uint32_t getProp(const char *str) {
    char buf[256];
    property_get(str, buf, "0");
    return atoi(buf);
}

// Helper object to acquire and release the RsContext context.
class Context {
    void *user_context;

public:
    RsDevice mDev;
    RsContext mContext;
    int error;

    static dispatchTable *dispatch;

    static bool loadSO(const char *filename) {
        void *handle = dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
        if (handle == NULL) {
            ALOGV("couldn't dlopen %s, %s", filename, dlerror());
            return false;
        }

        if (loadSymbols(handle, *dispatch, getProp("ro.build.version.sdk")) == false) {
            ALOGV("%s init failed!", filename);
            return false;
        }
        ALOGV("Successfully loaded %s", filename);
        return true;
    }

    // Constructor sets 'error' if any occurs.
    Context(void *user_context)
        : user_context(user_context), mDev(NULL), mContext(NULL),
          error(RS_ERROR_RUNTIME_ERROR) {
#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif
        error = halide_renderscript_acquire_context(user_context, &mDev, &mContext);
        halide_assert(user_context, mDev != NULL);
        halide_assert(user_context, mContext != NULL);
    }

    ~Context() { halide_renderscript_release_context(user_context); }
};

WEAK dispatchTable *Context::dispatch;

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    void *module;
    module_state *next;
};
WEAK module_state *state_list = NULL;

WEAK int create_renderscript_context(void *user_context, RsDevice *dev, RsContext *ctx) {
    uint32_t flags = 0;

    int targetApi = RS_VERSION;
    Context::dispatch = new dispatchTable;
    memset(Context::dispatch, 0, sizeof(dispatchTable));

    bool usingNative = false;

    // attempt to load libRS, load libRSSupport on failure
    // if property is set, proceed directly to libRSSupport
    if (getProp("debug.rs.forcecompat") == 0) {
        usingNative = Context::loadSO("libRS.so");
    }
    if (usingNative == false) {
        if (Context::loadSO("libRSSupport.so") == false) {
            ALOGV("Failed to load libRS.so and libRSSupport.so");
            return -1;
        }
    }

    *dev = Context::dispatch->DeviceCreate();
    if (*dev == 0) {
        ALOGV("Device creation failed");
        return -1;
    }

    ALOGV("Created device %p", *dev);

    if (flags &
        ~(RS_CONTEXT_SYNCHRONOUS | RS_CONTEXT_LOW_LATENCY |
          RS_CONTEXT_LOW_POWER)) {
        ALOGE("Invalid flags passed");
        return -1;
    }

    *ctx = Context::dispatch->ContextCreate(*dev, 0, targetApi,
                                            RS_CONTEXT_TYPE_NORMAL, flags);
    if (*ctx == 0) {
        ALOGE("Context creation failed");
        return -1;
    }

    ALOGV("Created context %p", *ctx);
    return RS_SUCCESS;
}
}
}
}
}  // namespace Halide::Runtime::Internal:RS:

//
// Function below is taken from https://android.googlesource.com/platform/frameworks/rs/+/master/cpp/rsDispatch.cpp
//
bool loadSymbols(void* handle, dispatchTable& dispatchTab, int device_api) {
    //fucntion to set the native lib path for 64bit compat lib.
#ifdef __LP64__
    dispatchTab.SetNativeLibDir = (SetNativeLibDirFnPtr)dlsym(handle, "rsaContextSetNativeLibDir");
    if (dispatchTab.SetNativeLibDir == NULL) {
        LOG_API("Couldn't initialize dispatchTab.SetNativeLibDir");
        return false;
    }
#endif
    dispatchTab.AllocationGetType = (AllocationGetTypeFnPtr)dlsym(handle, "rsaAllocationGetType");
    if (dispatchTab.AllocationGetType == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationGetType");
        return false;
    }
    dispatchTab.TypeGetNativeData = (TypeGetNativeDataFnPtr)dlsym(handle, "rsaTypeGetNativeData");
    if (dispatchTab.TypeGetNativeData == NULL) {
        LOG_API("Couldn't initialize dispatchTab.TypeGetNativeData");
        return false;
    }
    dispatchTab.ElementGetNativeData = (ElementGetNativeDataFnPtr)dlsym(handle, "rsaElementGetNativeData");
    if (dispatchTab.ElementGetNativeData == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ElementGetNativeData");
        return false;
    }
    dispatchTab.ElementGetSubElements = (ElementGetSubElementsFnPtr)dlsym(handle, "rsaElementGetSubElements");
    if (dispatchTab.ElementGetSubElements == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ElementGetSubElements");
        return false;
    }
    dispatchTab.DeviceCreate = (DeviceCreateFnPtr)dlsym(handle, "rsDeviceCreate");
    if (dispatchTab.DeviceCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.DeviceCreate");
        return false;
    }
    dispatchTab.DeviceDestroy = (DeviceDestroyFnPtr)dlsym(handle, "rsDeviceDestroy");
    if (dispatchTab.DeviceDestroy == NULL) {
        LOG_API("Couldn't initialize dispatchTab.DeviceDestroy");
        return false;
    }
    dispatchTab.DeviceSetConfig = (DeviceSetConfigFnPtr)dlsym(handle, "rsDeviceSetConfig");
    if (dispatchTab.DeviceSetConfig == NULL) {
        LOG_API("Couldn't initialize dispatchTab.DeviceSetConfig");
        return false;
    }
    dispatchTab.ContextCreate = (ContextCreateFnPtr)dlsym(handle, "rsContextCreate");;
    if (dispatchTab.ContextCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextCreate");
        return false;
    }
    dispatchTab.GetName = (GetNameFnPtr)dlsym(handle, "rsaGetName");;
    if (dispatchTab.GetName == NULL) {
        LOG_API("Couldn't initialize dispatchTab.GetName");
        return false;
    }
    dispatchTab.ContextDestroy = (ContextDestroyFnPtr)dlsym(handle, "rsContextDestroy");
    if (dispatchTab.ContextDestroy == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextDestroy");
        return false;
    }
    dispatchTab.ContextGetMessage = (ContextGetMessageFnPtr)dlsym(handle, "rsContextGetMessage");
    if (dispatchTab.ContextGetMessage == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextGetMessage");
        return false;
    }
    dispatchTab.ContextPeekMessage = (ContextPeekMessageFnPtr)dlsym(handle, "rsContextPeekMessage");
    if (dispatchTab.ContextPeekMessage == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextPeekMessage");
        return false;
    }
    dispatchTab.ContextSendMessage = (ContextSendMessageFnPtr)dlsym(handle, "rsContextSendMessage");
    if (dispatchTab.ContextSendMessage == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextSendMessage");
        return false;
    }
    dispatchTab.ContextInitToClient = (ContextInitToClientFnPtr)dlsym(handle, "rsContextInitToClient");
    if (dispatchTab.ContextInitToClient == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextInitToClient");
        return false;
    }
    dispatchTab.ContextDeinitToClient = (ContextDeinitToClientFnPtr)dlsym(handle, "rsContextDeinitToClient");
    if (dispatchTab.ContextDeinitToClient == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextDeinitToClient");
        return false;
    }
    dispatchTab.TypeCreate = (TypeCreateFnPtr)dlsym(handle, "rsTypeCreate");
    if (dispatchTab.TypeCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.TypeCreate");
        return false;
    }
    dispatchTab.AllocationCreateTyped = (AllocationCreateTypedFnPtr)dlsym(handle, "rsAllocationCreateTyped");
    if (dispatchTab.AllocationCreateTyped == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationCreateTyped");
        return false;
    }
    dispatchTab.AllocationCreateFromBitmap = (AllocationCreateFromBitmapFnPtr)dlsym(handle, "rsAllocationCreateFromBitmap");
    if (dispatchTab.AllocationCreateFromBitmap == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationCreateFromBitmap");
        return false;
    }
    dispatchTab.AllocationCubeCreateFromBitmap = (AllocationCubeCreateFromBitmapFnPtr)dlsym(handle, "rsAllocationCubeCreateFromBitmap");
    if (dispatchTab.AllocationCubeCreateFromBitmap == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationCubeCreateFromBitmap");
        return false;
    }
    dispatchTab.AllocationGetSurface = (AllocationGetSurfaceFnPtr)dlsym(handle, "rsAllocationGetSurface");
    if (dispatchTab.AllocationGetSurface == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationGetSurface");
        return false;
    }
    dispatchTab.AllocationSetSurface = (AllocationSetSurfaceFnPtr)dlsym(handle, "rsAllocationSetSurface");
    if (dispatchTab.AllocationSetSurface == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationSetSurface");
        return false;
    }
    dispatchTab.ContextFinish = (ContextFinishFnPtr)dlsym(handle, "rsContextFinish");
    if (dispatchTab.ContextFinish == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextFinish");
        return false;
    }
    dispatchTab.ContextDump = (ContextDumpFnPtr)dlsym(handle, "rsContextDump");
    if (dispatchTab.ContextDump == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextDump");
        return false;
    }
    dispatchTab.ContextSetPriority = (ContextSetPriorityFnPtr)dlsym(handle, "rsContextSetPriority");
    if (dispatchTab.ContextSetPriority == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ContextSetPriority");
        return false;
    }
    dispatchTab.AssignName = (AssignNameFnPtr)dlsym(handle, "rsAssignName");
    if (dispatchTab.AssignName == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AssignName");
        return false;
    }
    dispatchTab.ObjDestroy = (ObjDestroyFnPtr)dlsym(handle, "rsObjDestroy");
    if (dispatchTab.ObjDestroy == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ObjDestroy");
        return false;
    }
    dispatchTab.ElementCreate = (ElementCreateFnPtr)dlsym(handle, "rsElementCreate");
    if (dispatchTab.ElementCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ElementCreate");
        return false;
    }
    dispatchTab.ElementCreate2 = (ElementCreate2FnPtr)dlsym(handle, "rsElementCreate2");
    if (dispatchTab.ElementCreate2 == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ElementCreate2");
        return false;
    }
    dispatchTab.AllocationCopyToBitmap = (AllocationCopyToBitmapFnPtr)dlsym(handle, "rsAllocationCopyToBitmap");
    if (dispatchTab.AllocationCopyToBitmap == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationCopyToBitmap");
        return false;
    }
    dispatchTab.Allocation1DData = (Allocation1DDataFnPtr)dlsym(handle, "rsAllocation1DData");
    if (dispatchTab.Allocation1DData == NULL) {
        LOG_API("Couldn't initialize dispatchTab.Allocation1DData");
        return false;
    }
    dispatchTab.Allocation1DElementData = (Allocation1DElementDataFnPtr)dlsym(handle, "rsAllocation1DElementData");
    if (dispatchTab.Allocation1DElementData == NULL) {
        LOG_API("Couldn't initialize dispatchTab.Allocation1DElementData");
        return false;
    }
    dispatchTab.Allocation2DData = (Allocation2DDataFnPtr)dlsym(handle, "rsAllocation2DData");
    if (dispatchTab.Allocation2DData == NULL) {
        LOG_API("Couldn't initialize dispatchTab.Allocation2DData");
        return false;
    }
    dispatchTab.Allocation3DData = (Allocation3DDataFnPtr)dlsym(handle, "rsAllocation3DData");
    if (dispatchTab.Allocation3DData == NULL) {
        LOG_API("Couldn't initialize dispatchTab.Allocation3DData");
        return false;
    }
    dispatchTab.AllocationGenerateMipmaps = (AllocationGenerateMipmapsFnPtr)dlsym(handle, "rsAllocationGenerateMipmaps");
    if (dispatchTab.AllocationGenerateMipmaps == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationGenerateMipmaps");
        return false;
    }
    dispatchTab.AllocationRead = (AllocationReadFnPtr)dlsym(handle, "rsAllocationRead");
    if (dispatchTab.AllocationRead == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationRead");
        return false;
    }
    dispatchTab.Allocation1DRead = (Allocation1DReadFnPtr)dlsym(handle, "rsAllocation1DRead");
    if (dispatchTab.Allocation1DRead == NULL) {
        LOG_API("Couldn't initialize dispatchTab.Allocation1DRead");
        return false;
    }
    dispatchTab.Allocation2DRead = (Allocation2DReadFnPtr)dlsym(handle, "rsAllocation2DRead");
    if (dispatchTab.Allocation2DRead == NULL) {
        LOG_API("Couldn't initialize dispatchTab.Allocation2DRead");
        return false;
    }
    dispatchTab.AllocationSyncAll = (AllocationSyncAllFnPtr)dlsym(handle, "rsAllocationSyncAll");
    if (dispatchTab.AllocationSyncAll == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationSyncAll");
        return false;
    }
    dispatchTab.AllocationResize1D = (AllocationResize1DFnPtr)dlsym(handle, "rsAllocationResize1D");
    if (dispatchTab.AllocationResize1D == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationResize1D");
        return false;
    }
    dispatchTab.AllocationCopy2DRange = (AllocationCopy2DRangeFnPtr)dlsym(handle, "rsAllocationCopy2DRange");
    if (dispatchTab.AllocationCopy2DRange == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationCopy2DRange");
        return false;
    }
    dispatchTab.AllocationCopy3DRange = (AllocationCopy3DRangeFnPtr)dlsym(handle, "rsAllocationCopy3DRange");
    if (dispatchTab.AllocationCopy3DRange == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationCopy3DRange");
        return false;
    }
    dispatchTab.SamplerCreate = (SamplerCreateFnPtr)dlsym(handle, "rsSamplerCreate");
    if (dispatchTab.SamplerCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.SamplerCreate");
        return false;
    }
    dispatchTab.ScriptBindAllocation = (ScriptBindAllocationFnPtr)dlsym(handle, "rsScriptBindAllocation");
    if (dispatchTab.ScriptBindAllocation == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptBindAllocation");
        return false;
    }
    dispatchTab.ScriptSetTimeZone = (ScriptSetTimeZoneFnPtr)dlsym(handle, "rsScriptSetTimeZone");
    if (dispatchTab.ScriptSetTimeZone == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptSetTimeZone");
        return false;
    }
    dispatchTab.ScriptInvoke = (ScriptInvokeFnPtr)dlsym(handle, "rsScriptInvoke");
    if (dispatchTab.ScriptInvoke == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptInvoke");
        return false;
    }
    dispatchTab.ScriptInvokeV = (ScriptInvokeVFnPtr)dlsym(handle, "rsScriptInvokeV");
    if (dispatchTab.ScriptInvokeV == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptInvokeV");
        return false;
    }
    dispatchTab.ScriptForEach = (ScriptForEachFnPtr)dlsym(handle, "rsScriptForEach");
    if (dispatchTab.ScriptForEach == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptForEach");
        return false;
    }
    dispatchTab.ScriptSetVarI = (ScriptSetVarIFnPtr)dlsym(handle, "rsScriptSetVarI");
    if (dispatchTab.ScriptSetVarI == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptSetVarI");
        return false;
    }
    dispatchTab.ScriptSetVarObj = (ScriptSetVarObjFnPtr)dlsym(handle, "rsScriptSetVarObj");
    if (dispatchTab.ScriptSetVarObj == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptSetVarObj");
        return false;
    }
    dispatchTab.ScriptSetVarJ = (ScriptSetVarJFnPtr)dlsym(handle, "rsScriptSetVarJ");
    if (dispatchTab.ScriptSetVarJ == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptSetVarJ");
        return false;
    }
    dispatchTab.ScriptSetVarF = (ScriptSetVarFFnPtr)dlsym(handle, "rsScriptSetVarF");
    if (dispatchTab.ScriptSetVarF == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptSetVarF");
        return false;
    }
    dispatchTab.ScriptSetVarD = (ScriptSetVarDFnPtr)dlsym(handle, "rsScriptSetVarD");
    if (dispatchTab.ScriptSetVarD == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptSetVarD");
        return false;
    }
    dispatchTab.ScriptSetVarV = (ScriptSetVarVFnPtr)dlsym(handle, "rsScriptSetVarV");
    if (dispatchTab.ScriptSetVarV == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptSetVarV");
        return false;
    }
    dispatchTab.ScriptGetVarV = (ScriptGetVarVFnPtr)dlsym(handle, "rsScriptGetVarV");
    if (dispatchTab.ScriptGetVarV == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptGetVarV");
        return false;
    }
    dispatchTab.ScriptSetVarVE = (ScriptSetVarVEFnPtr)dlsym(handle, "rsScriptSetVarVE");
    if (dispatchTab.ScriptSetVarVE == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptSetVarVE");
        return false;
    }
    dispatchTab.ScriptCCreate = (ScriptCCreateFnPtr)dlsym(handle, "rsScriptCCreate");
    if (dispatchTab.ScriptCCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptCCreate");
        return false;
    }
    dispatchTab.ScriptIntrinsicCreate = (ScriptIntrinsicCreateFnPtr)dlsym(handle, "rsScriptIntrinsicCreate");
    if (dispatchTab.ScriptIntrinsicCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptIntrinsicCreate");
        return false;
    }
    dispatchTab.ScriptKernelIDCreate = (ScriptKernelIDCreateFnPtr)dlsym(handle, "rsScriptKernelIDCreate");
    if (dispatchTab.ScriptKernelIDCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptKernelIDCreate");
        return false;
    }
    dispatchTab.ScriptFieldIDCreate = (ScriptFieldIDCreateFnPtr)dlsym(handle, "rsScriptFieldIDCreate");
    if (dispatchTab.ScriptFieldIDCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptFieldIDCreate");
        return false;
    }
    dispatchTab.ScriptGroupCreate = (ScriptGroupCreateFnPtr)dlsym(handle, "rsScriptGroupCreate");
    if (dispatchTab.ScriptGroupCreate == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptGroupCreate");
        return false;
    }
    dispatchTab.ScriptGroupSetOutput = (ScriptGroupSetOutputFnPtr)dlsym(handle, "rsScriptGroupSetOutput");
    if (dispatchTab.ScriptGroupSetOutput == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptGroupSetOutput");
        return false;
    }
    dispatchTab.ScriptGroupSetInput = (ScriptGroupSetInputFnPtr)dlsym(handle, "rsScriptGroupSetInput");
    if (dispatchTab.ScriptGroupSetInput == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptGroupSetInput");
        return false;
    }
    dispatchTab.ScriptGroupExecute = (ScriptGroupExecuteFnPtr)dlsym(handle, "rsScriptGroupExecute");
    if (dispatchTab.ScriptGroupExecute == NULL) {
        LOG_API("Couldn't initialize dispatchTab.ScriptGroupExecute");
        return false;
    }
    dispatchTab.AllocationIoSend = (AllocationIoSendFnPtr)dlsym(handle, "rsAllocationIoSend");
    if (dispatchTab.AllocationIoSend == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationIoSend");
        return false;
    }
    dispatchTab.AllocationIoReceive = (AllocationIoReceiveFnPtr)dlsym(handle, "rsAllocationIoReceive");
    if (dispatchTab.AllocationIoReceive == NULL) {
        LOG_API("Couldn't initialize dispatchTab.AllocationIoReceive");
        return false;
    }
    // API_21 functions
    if (device_api >= 21) {
        dispatchTab.AllocationGetPointer = (AllocationGetPointerFnPtr)dlsym(handle, "rsAllocationGetPointer");
        if (dispatchTab.AllocationGetPointer == NULL) {
            LOG_API("Couldn't initialize dispatchTab.AllocationGetPointer");
            return false;
        }
    }
    // API_23 functions
    if (device_api >= 23) {
        //ScriptGroup V2 functions
        dispatchTab.ScriptInvokeIDCreate = (ScriptInvokeIDCreateFnPtr)dlsym(handle, "rsScriptInvokeIDCreate");
        if (dispatchTab.ScriptInvokeIDCreate == NULL) {
            LOG_API("Couldn't initialize dispatchTab.ScriptInvokeIDCreate");
            return false;
        }
        dispatchTab.ClosureCreate = (ClosureCreateFnPtr)dlsym(handle, "rsClosureCreate");
        if (dispatchTab.ClosureCreate == NULL) {
            LOG_API("Couldn't initialize dispatchTab.ClosureCreate");
            return false;
        }
        dispatchTab.InvokeClosureCreate = (InvokeClosureCreateFnPtr)dlsym(handle, "rsInvokeClosureCreate");
        if (dispatchTab.InvokeClosureCreate == NULL) {
            LOG_API("Couldn't initialize dispatchTab.InvokeClosureCreate");
            return false;
        }
        dispatchTab.ClosureSetArg = (ClosureSetArgFnPtr)dlsym(handle, "rsClosureSetArg");
        if (dispatchTab.ClosureSetArg == NULL) {
            LOG_API("Couldn't initialize dispatchTab.ClosureSetArg");
            return false;
        }
        dispatchTab.ClosureSetGlobal = (ClosureSetGlobalFnPtr)dlsym(handle, "rsClosureSetGlobal");
        if (dispatchTab.ClosureSetGlobal == NULL) {
            LOG_API("Couldn't initialize dispatchTab.ClosureSetGlobal");
            return false;
        }
        dispatchTab.ScriptGroup2Create = (ScriptGroup2CreateFnPtr)dlsym(handle, "rsScriptGroup2Create");
        if (dispatchTab.ScriptGroup2Create == NULL) {
            LOG_API("Couldn't initialize dispatchTab.ScriptGroup2Create");
            return false;
        }
        dispatchTab.AllocationElementData = (AllocationElementDataFnPtr)dlsym(handle, "rsAllocationElementData");
        if (dispatchTab.AllocationElementData == NULL) {
            LOG_API("Couldn't initialize dispatchTab.AllocationElementData");
            return false;
        }
        dispatchTab.AllocationElementRead = (AllocationElementReadFnPtr)dlsym(handle, "rsAllocationElementRead");
        if (dispatchTab.AllocationElementRead == NULL) {
            LOG_API("Couldn't initialize dispatchTab.AllocationElementRead");
            return false;
        }
    }
    // Function below is part of future API_23, but since it has not been released yet,
    // we are poking to see if that function is available even if we are
    // on device_api == 22.
    if (device_api >= 22) {
        dispatchTab.Allocation3DRead = (Allocation3DReadFnPtr)dlsym(handle, "rsAllocation3DRead");
        if (dispatchTab.Allocation3DRead == NULL) {
            LOG_API("Couldn't initialize dispatchTab.Allocation3DRead");
        }
    }
    return true;
}

extern "C" {

WEAK int halide_renderscript_initialize_kernels(void *user_context, void **state_ptr,
                                      const char *src, int size) {
    debug(user_context) << "RS: halide_renderscript_initialize_kernels (user_context: "
                        << user_context << ", state_ptr: " << state_ptr
                        << ", program: " << (void *)src << ", size: " << size
                        << "\n";

    Context ctx(user_context);
    if (ctx.error != RS_SUCCESS) {
        debug(user_context) << "RS: halide_renderscript_init_kernels failed "
                            << "to create RSContext. error is " << ctx.error
                            << "\n";
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // Create the state object if necessary. This only happens once, regardless
    // of how many times halide_initialize_kernels/halide_release is called.
    // halide_release traverses this list and releases the module objects, but
    // it does not modify the list nodes created/inserted here.
    module_state **state = (module_state **)state_ptr;
    if (!(*state)) {
        *state = (module_state *)malloc(sizeof(module_state));
        (*state)->module = NULL;
        (*state)->next = state_list;
        state_list = *state;
    }

    // Create the module itself if necessary.
    if (!(*state)->module) {
        const char *cacheDir = halide_get_renderscript_cache_dir(user_context);
        debug(user_context) << "RS:halide_renderscript_init_kernels cacheDir is " << cacheDir << "\n";
        // TODO(aam): Figure out good "cachedName" we can use. The one below
        // is chosen randomly.
        const char *cachedName = "halide_renderscript_kernel";
        (*state)->module = Context::dispatch->ScriptCCreate(
            ctx.mContext,
            cachedName,
            strlen(cachedName),
            cacheDir,
            strlen(cacheDir),
            (const char *)src,
            size  // length of src, code size
            );

        debug(user_context) << "RS:halide_renderscript_init_kernels created script "
                            << (void *)((*state)->module) << "\n";
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return RS_SUCCESS;
}

WEAK int halide_renderscript_device_free(void *user_context, buffer_t *buf) {
    if (buf->dev == 0) {
        return 0;
    }

    void *dev_ptr = (void *)halide_get_device_handle(buf->dev);

    debug(user_context) << "RS: halide_renderscript_device_free (user_context: "
                        << user_context << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != RS_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    debug(user_context) << "    non-implemented RS device free "
                        << (void *)(dev_ptr) << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return RS_SUCCESS;
}

WEAK int halide_renderscript_device_release(void *user_context) {
    debug(user_context) << "RS: halide_renderscript_device_release (user_context: "
                        << user_context << ")\n";

    Context ctx(user_context);
    if (ctx.error != RS_SUCCESS) {
        debug(user_context) << "RS: halide_renderscript_device_release failed to "
                            << "create Context. error is " << ctx.error << "\n";
        return ctx.error;
    }

    // Unload the modules attached to this context. Note that the list
    // nodes themselves are not freed, only the module objects are
    // released. Subsequent calls to halide_init_kernels might re-create
    // the program object using the same list node to store the module
    // object.
    module_state *state = state_list;
    while (state) {
        if (state->module) {
            debug(user_context) << "    non-implemented RS ModuleUnload "
                                << state->module << "\n";
            // TOOD: Unload renderscript modules?

            // err = cuModuleUnload(state->module);
            // halide_assert(user_context, err == CUDA_SUCCESS || err ==
            // CUDA_ERROR_DEINITIALIZED);
            state->module = 0;
        }
        state = state->next;
    }

    // Only destroy the context if we own it
    if (ctx.mContext == context) {
        debug(user_context) << "    non-implemented RS CtxDestroy " << context
                            << "\n";
        // err = cuCtxDestroy(context);
        // halide_assert(user_context, err == CUDA_SUCCESS || err ==
        // CUDA_ERROR_DEINITIALIZED);
        context = NULL;
    }

    halide_renderscript_release_context(user_context);

    return RS_SUCCESS;
}

WEAK size_t buf_size(void *user_context, buffer_t *buf) {
    size_t size = buf->elem_size;
    for (size_t i = 0; i < sizeof(buf->stride) / sizeof(buf->stride[0]); i++) {
        size_t total_dim_size =
            buf->elem_size * buf->extent[i] * buf->stride[i];
        if (total_dim_size > size) {
            size = total_dim_size;
        }
    }
    halide_assert(user_context, size);
    return size;
}

namespace {
WEAK bool is_interleaved_rgba_buffer_t(buffer_t *buf) {
    return (buf->stride[2] == 1 && buf->extent[2] == 4);
}
}

WEAK int halide_renderscript_device_malloc(void *user_context, buffer_t *buf) {
    debug(user_context) << "RS: halide_renderscript_device_malloc (user_context: "
                        << user_context << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != RS_SUCCESS) {
        return ctx.error;
    }

    size_t size = buf_size(user_context, buf);

    if (buf->dev) {
        // This buffer already has a device allocation
        debug(user_context) << "renderscript_device_malloc: This buffer already has a "
                               "device allocation\n";
        return 0;
    }

    halide_assert(user_context, buf->stride[0] >= 0 && buf->stride[1] >= 0 &&
                                    buf->stride[2] >= 0 && buf->stride[3] >= 0);

    debug(user_context) << "    allocating "
                        << (is_interleaved_rgba_buffer_t(buf)? "interleaved": "plain")
                        << " buffer of " << (int64_t)size << " bytes, "
                        << "extents: " << buf->extent[0] << "x"
                        << buf->extent[1] << "x" << buf->extent[2] << "x"
                        << buf->extent[3] << " "
                        << "strides: " << buf->stride[0] << "x"
                        << buf->stride[1] << "x" << buf->stride[2] << "x"
                        << buf->stride[3] << " "
                        << "(" << buf->elem_size << " bytes per element)\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    void *typeID = NULL;
    //
    //  Support two element types:
    //  - single unsigned 8-bit;
    //  - 32-bit float;
    //
    //  Support two types of buffers:
    //  - 2-dimension interleaved, where every element is 4-element wide(RGBA).
    //    Assumption is that Halide schedule for this buffer is vectorized along that 3-rd dimension(c).
    //  - 3-dimension one-byte element.
    //    Assumption is that there is no vectorization in Halide schedule for this data type.
    //
    RsDataType datatype;
    switch(buf->elem_size) {
        case 1:
            datatype = RS_TYPE_UNSIGNED_8;
            break;
        case 4:
            datatype = RS_TYPE_FLOAT_32;
            break;
        default:
            error(user_context) << "RS: Unsupported element type of size " << buf->elem_size << "\n";
    }

    if (is_interleaved_rgba_buffer_t(buf)) {
        //
        // 4-channel type:
        //
        void *elementID_RGBA_8888 = Context::dispatch->ElementCreate(
            ctx.mContext, datatype, RS_KIND_PIXEL_RGBA, true,
            4 /* vecSize */);
        typeID = Context::dispatch->TypeCreate(
            ctx.mContext, elementID_RGBA_8888, buf->extent[0], buf->extent[1],
            0, false
            /*mDimMipmaps*/,
            false /*mDimFaces*/, 0);
    } else {
        void *elementID_A = Context::dispatch->ElementCreate(
            ctx.mContext, datatype, RS_KIND_PIXEL_A, true,
            1 /* vecSize */);
        typeID = Context::dispatch->TypeCreate(ctx.mContext, elementID_A,
                                               buf->extent[0], buf->extent[1],
                                               buf->extent[2], false
                                               /*mDimMipmaps*/,
                                               false /*mDimFaces*/, 0);
    }
    debug(user_context) << "Created type " << typeID << "\n";

    void *p = Context::dispatch->AllocationCreateTyped(
        ctx.mContext, typeID, RS_ALLOCATION_MIPMAP_NONE /*mipmaps*/,
        RS_ALLOCATION_USAGE_SCRIPT /*usage*/, 0);
    if (p == NULL) {
        error(user_context) << "RS: AllocationCreateTyped failed\n";
        return -1;
    } else {
        debug(user_context) << (void *)p << "\n";
    }
    buf->dev = halide_new_device_wrapper((uint64_t)p, &renderscript_device_interface);
    if (buf->dev == 0) {
        error(user_context) << "RS: out of memory allocating device wrapper.\n";
        return -1;
    }
    debug(user_context) << "Allocated dev_buffer " << p << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return RS_SUCCESS;
}

WEAK int halide_renderscript_copy_to_device(void *user_context, buffer_t *buf) {
    debug(user_context) << "RS: halide_renderscript_copy_to_device (user_context: "
                        << user_context << ", "
                        << (is_interleaved_rgba_buffer_t(buf)? "interleaved": "plain")
                        << " buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != RS_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    halide_assert(user_context, buf->host && buf->dev);

    if (is_interleaved_rgba_buffer_t(buf)) {
        Context::dispatch->Allocation2DData(
            ctx.mContext, (void *)halide_get_device_handle(buf->dev), 0 /*xoff*/,
            0 /*yoff*/, 0 /*mSelectedLOD*/, RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_X,
            buf->extent[0] /*w*/, buf->extent[1] /*h*/, buf->host,
            buf->extent[0] * buf->extent[1] * buf->extent[2] * buf->elem_size,
            buf->extent[0] * buf->extent[2] * buf->elem_size); /* hosts' stride */
    } else {
        Context::dispatch->Allocation3DData(
            ctx.mContext, (void *)halide_get_device_handle(buf->dev), 0 /*xoff*/,
            0 /*yoff*/, 0 /*zoff*/, 0 /*mSelectedLOD*/, buf->extent[0] /* w */,
            buf->extent[1] /*h*/, buf->extent[2] /*d*/, buf->host,
            buf->extent[0] * buf->extent[1] * buf->extent[2] * buf->elem_size,
            buf->extent[0] * buf->elem_size); /* hosts' stride */
    }

    debug(user_context) << "RS: copied to device buf->dev="
                        << ((void *)halide_get_device_handle(buf->dev)) << " "
                        << buf->extent[0] << "x" << buf->extent[1] << "x"
                        << buf->extent[2] << "*" << buf->elem_size
                        << " bytes from " << buf->host << ": "
                        << (*(int8_t *)buf->host) << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return RS_SUCCESS;
}

WEAK int halide_renderscript_copy_to_host(void *user_context, buffer_t *buf) {
    debug(user_context) << "RS: halide_copy_to_host user_context: "
                        << user_context << ", "
                        << (is_interleaved_rgba_buffer_t(buf)? "interleaved": "plain")
                        << " buf: " << buf << " interface: "
                        << halide_get_device_interface(buf->dev) << " dev_buf: "
                        << (void *)halide_get_device_handle(buf->dev) << ")\n";

    Context ctx(user_context);
    if (ctx.error != RS_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    halide_assert(user_context, buf->host && buf->dev);

    debug(user_context) << "RS: trying to copy from device buf->dev = "
                        << ((void *)halide_get_device_handle(buf->dev)) << " "
                        << buf->extent[0] << "x" << buf->extent[1] << "x"
                        << buf->extent[2] << "*" << buf->elem_size
                        << " bytes into " << buf->host << "\n";

    Context::dispatch->AllocationSyncAll(
        ctx.mContext, (void *)halide_get_device_handle(buf->dev),
        RS_ALLOCATION_USAGE_SCRIPT);

    debug(user_context) << "AllocationSyncAll done\n";

    if (is_interleaved_rgba_buffer_t(buf)) {
        halide_assert(user_context, Context::dispatch->Allocation2DRead != NULL);
        Context::dispatch->Allocation2DRead(
            ctx.mContext, (void *)halide_get_device_handle(buf->dev), 0 /*xoff*/,
            0 /*yoff*/, 0 /*mSelectedLOD*/, RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_X,
            buf->extent[0] /*w*/, buf->extent[1] /*h*/, buf->host,
            buf->extent[0] * buf->extent[1] * buf->extent[2] * buf->elem_size /* byte size */,
            buf->extent[0] * buf->extent[2] * buf->elem_size /* hosts' stride */);
    } else {
        debug(user_context) << "staring Allocation3DRead("
            << (void*)Context::dispatch->Allocation3DRead
            << ")(w=" << buf->extent[0]
            << " h=" << buf->extent[1]
            << " d=" << buf->extent[2]
            << " buf->host=" << buf->host
            << " bytes=" << buf->extent[0] * buf->extent[1] * buf->extent[2] * buf->elem_size
            << " stride=" << (buf->extent[0] * buf->elem_size) << "...\n";

        // per rsdAllocationRead3D in frameworks/rs/driver/rsdAllocation.cpp
        // data has to be planar layout.
        halide_assert(user_context, Context::dispatch->Allocation3DRead != NULL);
        Context::dispatch->Allocation3DRead(
            ctx.mContext, (void *)halide_get_device_handle(buf->dev), 0 /*xoff*/,
            0 /*yoff*/, 0 /*zoff*/, 0 /*mSelectedLOD*/, buf->extent[0] /* w */,
            buf->extent[1] /*h*/, buf->extent[2] /*d*/, buf->host,
            buf->extent[0] * buf->extent[1] * buf->extent[2] * buf->elem_size /* byte size */,
            buf->extent[0] * buf->elem_size /* hosts' stride */);

        debug(user_context) << "Allocation3DRead done\n";
    }

    debug(user_context) << "RS: copied from device " << buf->extent[0] << "x"
                        << buf->extent[1] << "x" << buf->extent[2] << "*"
                        << buf->elem_size << " dev handle: "
                        << (void *)halide_get_device_handle(buf->dev)
                        << " into " << buf->host
                        << " bytes: " << (*(int8_t *)buf->host) << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return RS_SUCCESS;
}

WEAK int halide_renderscript_device_sync(void *user_context, struct buffer_t *) {
    debug(user_context) << "RS: halide_renderscript_device_sync (user_context: "
                        << user_context << ")\n";

    Context ctx(user_context);
    if (ctx.error != RS_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    Context::dispatch->ContextFinish(ctx.mContext);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return RS_SUCCESS;
}

WEAK int halide_renderscript_run(void *user_context, void *state_ptr,
                       const char *entry_name, int blocksX, int blocksY,
                       int blocksZ, int threadsX, int threadsY, int threadsZ,
                       int shared_mem_bytes, size_t arg_sizes[], void *args[],
                       int8_t arg_is_buffer[], int num_attributes,
                       float *vertex_buffer, int num_coords_dim0,
                       int num_coords_dim1) {
    debug(user_context) << "RS: halide_renderscript_run (user_context: " << user_context
                        << ", "
                        << "entry: " << entry_name << ", "
                        << "blocks: " << blocksX << "x" << blocksY << "x"
                        << blocksZ << ", "
                        << "threads: " << threadsX << "x" << threadsY << "x"
                        << threadsZ << ", "
                        << "shmem: " << shared_mem_bytes << "\n";

    Context ctx(user_context);
    if (ctx.error != RS_SUCCESS) {
        return ctx.error;
    }

    debug(user_context) << "Got context.\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    halide_assert(user_context, state_ptr);
    void *module = ((module_state *)state_ptr)->module;
    debug(user_context) << "Got module " << module << "\n";
    halide_assert(user_context, module);

    size_t num_args = 0;

    uint64_t input_arg = 0;
    uint64_t output_arg = 0;
    while (arg_sizes[num_args] != 0) {
        debug(user_context) << "RS:    halide_renderscript_run " << (int64_t)num_args << " "
                            << (int64_t)arg_sizes[num_args] << " ["
                            << (*((void **)args[num_args])) << " ...] "
                            << arg_is_buffer[num_args] << "\n";
        if (arg_is_buffer[num_args] == 0) {
            int32_t arg_value = *((int32_t *)args[num_args]);
            Context::dispatch->ScriptSetVarV(ctx.mContext, module, num_args,
                                             &arg_value, sizeof(arg_value));
        } else {
            uint64_t arg_value = *(uint64_t *)args[num_args];
            Context::dispatch->ScriptSetVarObj(
                ctx.mContext, module, num_args,
                (void *)halide_get_device_handle(arg_value));

            if (input_arg == 0) {
                input_arg = arg_value;
            } else {
                output_arg = arg_value;
            }
        }
        num_args++;
    }

    int slot = atoi(entry_name);
    debug(user_context) << "RS: halide_renderscript_run starting script at slot " << slot
                        << " now with " << module
                        << " script "
                        << " input: "
                        << ((void *)halide_get_device_handle(input_arg))
                        << " output: "
                        << ((void *)halide_get_device_handle(output_arg))
                        << "\n";

    Context::dispatch->ScriptForEach(
        ctx.mContext, module,
        slot,  // slot corresponding to entry point
        (void *)halide_get_device_handle(input_arg),  // in_id
        (void *)halide_get_device_handle(output_arg),  // out_id
        NULL,  // usr
        0,  // usrLen
        NULL, 0);

    debug(user_context) << "ScriptForEach completed\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif
    return 0;
}

WEAK const struct halide_device_interface *halide_renderscript_device_interface() {
    return &renderscript_device_interface;
}

namespace {
__attribute__((destructor)) WEAK void halide_renderscript_cleanup() {
    halide_renderscript_device_release(NULL);
}
}

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Renderscript {

WEAK const char *get_error_name(RSError error) {
    switch (error) {
    case RS_SUCCESS:
        return "RS_SUCCESS";
    case RS_ERROR_INVALID_PARAMETER:
        return "RS_ERROR_INVALID_PARAMETER";
    case RS_ERROR_RUNTIME_ERROR:
        return "RS_ERROR_RUNTIME_ERROR";
    case RS_ERROR_INVALID_ELEMENT:
        return "RS_ERROR_INVALID_ELEMENT";
    default:
        return "RS_ERROR";
    }
}

WEAK halide_device_interface renderscript_device_interface = {
    halide_use_jit_module,  halide_release_jit_module,
    halide_renderscript_device_malloc,
    halide_renderscript_device_free,  halide_renderscript_device_sync,
    halide_renderscript_device_release,
    halide_renderscript_copy_to_host, halide_renderscript_copy_to_device,
};
}
}
}
}  // namespace Halide::Runtime::Internal::Renderscript
