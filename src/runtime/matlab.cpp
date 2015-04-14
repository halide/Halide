#include "HalideRuntime.h"

#define RTLD_DEFAULT ((void *)-2)
extern void *dlsym(void *, const char *);

extern void *alloca(int);

// Assume version 7.4 if not defined.
#ifndef MX_API_VER
#define MX_API_VER 0x07040000
#endif

// It is important to have the mex function pointer definitions in a
// namespace to avoid silently conflicting symbols with matlab at
// runtime.
namespace Halide {
namespace Runtime {
namespace mex {

struct mxArray;

enum { TMW_NAME_LENGTH_MAX = 64 };
enum { mxMAXNAM = TMW_NAME_LENGTH_MAX };

typedef bool mxLogical;
typedef int16_t mxChar;

enum mxClassID {
    mxUNKNOWN_CLASS = 0,
    mxCELL_CLASS,
    mxSTRUCT_CLASS,
    mxLOGICAL_CLASS,
    mxCHAR_CLASS,
    mxVOID_CLASS,
    mxDOUBLE_CLASS,
    mxSINGLE_CLASS,
    mxINT8_CLASS,
    mxUINT8_CLASS,
    mxINT16_CLASS,
    mxUINT16_CLASS,
    mxINT32_CLASS,
    mxUINT32_CLASS,
    mxINT64_CLASS,
    mxUINT64_CLASS,
    mxFUNCTION_CLASS,
    mxOPAQUE_CLASS,
    mxOBJECT_CLASS,
#if defined(_LP64) || defined(_WIN64)
    mxINDEX_CLASS = mxUINT64_CLASS,
#else
    mxINDEX_CLASS = mxUINT32_CLASS,
#endif

    mxSPARSE_CLASS = mxVOID_CLASS
};

enum mxComplexity {
    mxREAL,
    mxCOMPLEX
};

#ifdef MX_COMPAT_32
typedef int mwSize;
typedef int mwIndex;
typedef int mwSignedIndex;
#else
typedef size_t mwSize;
typedef size_t mwIndex;
typedef ptrdiff_t mwSignedIndex;
#endif

typedef void (*mex_exit_fn)(void);
typedef void (*mxFunctionPtr)(int, mxArray **, int, const mxArray **);

mxClassID get_class_id(int32_t type_code, int32_t type_bits) {
    switch (type_code) {
    case halide_type_int:
        switch (type_bits) {
        case 1: return mxLOGICAL_CLASS;
        case 8: return mxINT8_CLASS;
        case 16: return mxINT16_CLASS;
        case 32: return mxINT32_CLASS;
        case 64: return mxINT64_CLASS;
        }
        return mxUNKNOWN_CLASS;
    case halide_type_uint:
        switch (type_bits) {
        case 1: return mxLOGICAL_CLASS;
        case 8: return mxUINT8_CLASS;
        case 16: return mxUINT16_CLASS;
        case 32: return mxUINT32_CLASS;
        case 64: return mxUINT64_CLASS;
        }
        return mxUNKNOWN_CLASS;
    case halide_type_float:
        switch (type_bits) {
        case 32: return mxSINGLE_CLASS;
        case 64: return mxDOUBLE_CLASS;
        }
        return mxUNKNOWN_CLASS;
    }
    return mxUNKNOWN_CLASS;
}

const char *get_class_name(mxClassID id) {
    switch (id) {
    case mxCELL_CLASS: return "cell";
    case mxSTRUCT_CLASS: return "struct";
    case mxLOGICAL_CLASS: return "logical";
    case mxCHAR_CLASS: return "char";
    case mxVOID_CLASS: return "void";
    case mxDOUBLE_CLASS: return "double";
    case mxSINGLE_CLASS: return "single";
    case mxINT8_CLASS: return "int8";
    case mxUINT8_CLASS: return "uint8";
    case mxINT16_CLASS: return "int16";
    case mxUINT16_CLASS: return "uint16";
    case mxINT32_CLASS: return "int32";
    case mxUINT32_CLASS: return "uint32";
    case mxINT64_CLASS: return "int64";
    case mxUINT64_CLASS: return "uint64";
    case mxFUNCTION_CLASS: return "function";
    case mxOPAQUE_CLASS: return "opaque";
    case mxOBJECT_CLASS: return "object";
    default: return "unknown";
    }
}

// Declare function pointers for the mex APIs.
#define MEX_FN(ret, func, args) ret (*func)args;
#ifndef BITS_32
# define MEX_FN_730(ret, func, func_730, args) MEX_FN(ret, func, args)
#else
# define MEX_FN_700(ret, func, func_700, args) MEX_FN(ret, func, args)
#endif
#include "mex_functions.h"

template <typename T>
inline T* get_data(mxArray *a) { return (T *)mxGetData(a); }
template <typename T>
inline const T* get_data(const mxArray *a) { return (const T *)mxGetData(a); }

template <typename T>
inline T get_scalar(const mxArray *a) { return *get_data<T>(a); }

template <typename T>
T get_symbol(void *user_context, const char *name) {
    T s = (T)dlsym(RTLD_DEFAULT, name);
    if (s == NULL) {
        error(user_context) << "Matlab API not found: " << name << "\n";
    }
    return s;
}

}  // namespace mex
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::mex;

extern "C" {

WEAK int halide_mex_init(void *user_context) {
    // Assume that if mexPrintf exists, we've already attempted initialization.
    if (mexPrintf != NULL) {
        return 0;
    }

    #define MEX_FN(ret, func, args) func = get_symbol<ret (*)args>(user_context, #func); if (!func) { return -1; }
    #ifndef BITS_32
    # define MEX_FN_730(ret, func, func_730, args) func = get_symbol<ret (*)args>(user_context, #func_730); if (!func) { return -1; }
    #else
    # define MEX_FN_700(ret, func, func_700, args) func = get_symbol<ret (*)args>(user_context, #func_700); if (!func) { return -1; }
    #endif
    #include "mex_functions.h"

    return 0;
}

// Do only as much validation as is necessary to avoid accidentally
// reinterpreting casting data.
WEAK int halide_mex_validate_argument(void *user_context, const halide_filter_argument_t *arg, const mxArray *arr) {
    if (mxIsComplex(arr)) {
        error(user_context) << "Complex argument not supported for parameter " << arg->name << ".\n";
        return -1;
    }
    int dim_count = mxGetNumberOfDimensions(arr);
    if (arg->kind == halide_argument_kind_input_scalar) {
        for (int i = 0; i < dim_count; i++) {
            if (mxGetDimensions(arr)[i] != 1) {
                error(user_context) << "Expected scalar argument for parameter " << arg->name << ".\n";
                return -1;
            }
        }
        if (arg->type_bits == 1) {
            if (!mxIsLogical(arr)) {
                error(user_context) << "Expected logical argument for scalar parameter " << arg->name
                                    << ", got " << mxGetClassName(arr) << ".\n";
                return -1;
            }
        } else {
            if (!mxIsNumeric(arr)) {
                error(user_context) << "Expected numeric argument for scalar parameter " << arg->name
                                    << ", got " << mxGetClassName(arr) << ".\n";
                return -1;
            }
        }
    } else if (arg->kind == halide_argument_kind_input_buffer ||
               arg->kind == halide_argument_kind_output_buffer) {
        mxClassID arg_class_id = get_class_id(arg->type_code, arg->type_bits);
        if (mxGetClassID(arr) != arg_class_id) {
            error(user_context) << "Expected type of class " << get_class_name(arg_class_id)
                                << ", got class " << mxGetClassName(arr) << ".\n";
            return -1;
        }
        if (dim_count > 2 && dim_count > arg->dimensions) {
            error(user_context) << "Expected array of rank " << arg->dimensions
                                << ", got array of rank " << dim_count << ".\n";
            return -1;
        }
    }
    return 0;
}

WEAK int halide_mex_validate_arguments(void *user_context, const halide_filter_metadata_t *metadata,
                                       int nlhs, mxArray **plhs, int nrhs, const mxArray **prhs) {
    if (nlhs + nrhs != metadata->num_arguments) {
        error(user_context) << "Expected " << metadata->num_arguments
                            << " for Halide pipeline " << metadata->name
                            << ", got " << nlhs + nrhs << ".\n";
        return -1;
    }

    for (int i = 0; i < nlhs + nrhs; i++) {
        const mxArray *arg = i < nrhs ? prhs[i] : plhs[i - nrhs];
        int ret = halide_mex_validate_argument(user_context, &metadata->arguments[i], arg);
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

// Convert a matlab mxArray to a Halide buffer_t, with a specific number of dimensions.
WEAK int halide_mex_array_to_buffer_t(const mxArray *arr, int expected_dims, buffer_t *buf) {
    memset(buf, 0, sizeof(buffer_t));
    buf->host = (uint8_t *)mxGetData(arr);
    buf->elem_size = mxGetElementSize(arr);

    int dim_count = mxGetNumberOfDimensions(arr);
    for (int i = 0; i < dim_count && i < expected_dims; i++) {
        buf->extent[i] = static_cast<int32_t>(mxGetDimensions(arr)[i]);
    }

    // Add back the least significant dimensions with extent 1.
    for (int i = 1; i < expected_dims; i++) {
        if (buf->extent[i] == 0) {
            buf->extent[i] = 1;
        }
    }

    // Compute dense strides.
    buf->stride[0] = 1;
    for (int i = 1; i < expected_dims; i++) {
        buf->stride[i] = buf->extent[i - 1] * buf->stride[i - 1];
    }

    return 0;
}

// Convert a matlab mxArray to a scalar.
WEAK int halide_mex_array_to_scalar(const mxArray *arr, int32_t type_code, int32_t type_bits, void *scalar) {
    switch (type_code) {
    case halide_type_int:
        switch (type_bits) {
        case 1: *reinterpret_cast<bool *>(scalar) = get_scalar<uint8_t>(arr) != 0; return 0;
        case 8: *reinterpret_cast<int8_t *>(scalar) = get_scalar<int8_t>(arr); return 0;
        case 16: *reinterpret_cast<int16_t *>(scalar) = get_scalar<int16_t>(arr); return 0;
        case 32: *reinterpret_cast<int32_t *>(scalar) = get_scalar<int32_t>(arr); return 0;
        case 64: *reinterpret_cast<int64_t *>(scalar) = get_scalar<int64_t>(arr); return 0;
        }
        return -1;
    case halide_type_uint:
        switch (type_bits) {
        case 1: *reinterpret_cast<bool *>(scalar) = get_scalar<uint8_t>(arr) != 0; return 0;
        case 8: *reinterpret_cast<uint8_t *>(scalar) = get_scalar<uint8_t>(arr); return 0;
        case 16: *reinterpret_cast<uint16_t *>(scalar) = get_scalar<uint16_t>(arr); return 0;
        case 32: *reinterpret_cast<uint32_t *>(scalar) = get_scalar<uint32_t>(arr); return 0;
        case 64: *reinterpret_cast<uint64_t *>(scalar) = get_scalar<uint64_t>(arr); return 0;
        }
        return -1;
    case halide_type_float:
        switch (type_bits) {
        case 32: *reinterpret_cast<float *>(scalar) = get_scalar<float>(arr); return 0;
        case 64: *reinterpret_cast<double *>(scalar) = get_scalar<double>(arr); return 0;
        }
        return -1;
    }
    return -1;
}

WEAK int halide_mex_call_pipeline(void *user_context, const halide_filter_metadata_t *metadata,
                                  int (*pipeline)(void **args),
                                  int nlhs, mxArray **plhs, int nrhs, const mxArray **prhs) {
    int result;

    result = halide_mex_init(user_context);
    if (result != 0) {
        return result;
    }

    result = halide_mex_validate_arguments(user_context, metadata, nlhs, plhs, nrhs, prhs);
    if (result != 0) {
        return result;
    }

    void **args = (void **)alloca((nlhs + nrhs) * sizeof(void *));
    for (int i = 0; i < nrhs + nlhs; i++) {
        const mxArray *arg = i < nrhs ? prhs[i] : plhs[i - nrhs];
        const halide_filter_argument_t *arg_metadata = &metadata->arguments[i];

        if (arg_metadata->kind == halide_argument_kind_input_buffer ||
            arg_metadata->kind == halide_argument_kind_output_buffer) {
            buffer_t *buf = (buffer_t *)alloca(sizeof(buffer_t));
            result = halide_mex_array_to_buffer_t(arg, arg_metadata->dimensions, buf);
            if (result != 0) {
                return result;
            }
            args[i] = buf;
        } else {
            result = halide_mex_array_to_scalar(arg, arg_metadata->type_code, arg_metadata->type_bits, args[i]);
            if (result != 0) {
                return result;
            }
        }
    }

    return pipeline(args);
}

}  // extern "C"
