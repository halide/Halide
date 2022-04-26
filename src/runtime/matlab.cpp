#include "HalideRuntime.h"
#include "printer.h"

#ifndef MX_API_VER
#define MX_API_VER 0x07040000
#endif

struct mxArray;

// It is important to have the mex function pointer definitions in a
// namespace to avoid silently conflicting symbols with matlab at
// runtime.
namespace Halide {
namespace Runtime {
namespace mex {

// Define a few things from mex.h that we need to grab the mex APIs
// from matlab.

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
#ifdef BITS_32
    mxINDEX_CLASS = mxUINT32_CLASS,
#else
    mxINDEX_CLASS = mxUINT64_CLASS,
#endif

    mxSPARSE_CLASS = mxVOID_CLASS
};

enum mxComplexity {
    mxREAL = 0,
    mxCOMPLEX
};

#ifdef BITS_32
typedef int mwSize;
typedef int mwIndex;
typedef int mwSignedIndex;
#else
typedef size_t mwSize;
typedef size_t mwIndex;
typedef ptrdiff_t mwSignedIndex;
#endif

typedef void (*mex_exit_fn)();

// Declare function pointers for the mex APIs.
#define MEX_FN(ret, func, args) ret(*func) args;  // NOLINT(bugprone-macro-parentheses)
#include "mex_functions.h"
#undef MEX_FN

// Given a halide type code and bit width, find the equivalent matlab class ID.
WEAK mxClassID get_class_id(int32_t type_code, int32_t type_bits) {
    switch (type_code) {
    case halide_type_int:
        switch (type_bits) {
        case 1:
            return mxLOGICAL_CLASS;
        case 8:
            return mxINT8_CLASS;
        case 16:
            return mxINT16_CLASS;
        case 32:
            return mxINT32_CLASS;
        case 64:
            return mxINT64_CLASS;
        }
        return mxUNKNOWN_CLASS;
    case halide_type_uint:
        switch (type_bits) {
        case 1:
            return mxLOGICAL_CLASS;
        case 8:
            return mxUINT8_CLASS;
        case 16:
            return mxUINT16_CLASS;
        case 32:
            return mxUINT32_CLASS;
        case 64:
            return mxUINT64_CLASS;
        }
        return mxUNKNOWN_CLASS;
    case halide_type_float:
        switch (type_bits) {
        case 32:
            return mxSINGLE_CLASS;
        case 64:
            return mxDOUBLE_CLASS;
        }
        return mxUNKNOWN_CLASS;
    }
    return mxUNKNOWN_CLASS;
}

// Convert a matlab class ID to a string.
WEAK const char *get_class_name(mxClassID id) {
    switch (id) {
    case mxCELL_CLASS:
        return "cell";
    case mxSTRUCT_CLASS:
        return "struct";
    case mxLOGICAL_CLASS:
        return "logical";
    case mxCHAR_CLASS:
        return "char";
    case mxVOID_CLASS:
        return "void";
    case mxDOUBLE_CLASS:
        return "double";
    case mxSINGLE_CLASS:
        return "single";
    case mxINT8_CLASS:
        return "int8";
    case mxUINT8_CLASS:
        return "uint8";
    case mxINT16_CLASS:
        return "int16";
    case mxUINT16_CLASS:
        return "uint16";
    case mxINT32_CLASS:
        return "int32";
    case mxUINT32_CLASS:
        return "uint32";
    case mxINT64_CLASS:
        return "int64";
    case mxUINT64_CLASS:
        return "uint64";
    case mxFUNCTION_CLASS:
        return "function";
    case mxOPAQUE_CLASS:
        return "opaque";
    case mxOBJECT_CLASS:
        return "object";
    default:
        return "unknown";
    }
}

// Get the real data pointer from an mxArray.
template<typename T>
ALWAYS_INLINE T *get_data(mxArray *a) {
    return (T *)mxGetData(a);
}
template<typename T>
ALWAYS_INLINE const T *get_data(const mxArray *a) {
    return (const T *)mxGetData(a);
}

// Search for a symbol in the calling process (i.e. matlab).
template<typename T>
ALWAYS_INLINE T get_mex_symbol(void *user_context, const char *name, bool required) {
    T s = (T)halide_get_symbol(name);
    if (required && s == nullptr) {
        error(user_context) << "mex API not found: " << name << "\n";
        return nullptr;
    }
    return s;
}

// Provide Matlab API version agnostic wrappers for version specific APIs.
ALWAYS_INLINE size_t get_number_of_dimensions(const mxArray *a) {
    if (mxGetNumberOfDimensions_730) {
        return mxGetNumberOfDimensions_730(a);
    } else {
        return mxGetNumberOfDimensions_700(a);
    }
}

ALWAYS_INLINE size_t get_dimension(const mxArray *a, size_t n) {
    if (mxGetDimensions_730) {
        return mxGetDimensions_730(a)[n];
    } else {
        return mxGetDimensions_700(a)[n];
    }
}

ALWAYS_INLINE mxArray *create_numeric_matrix(size_t M, size_t N, mxClassID type, mxComplexity complexity) {
    if (mxCreateNumericMatrix_730) {
        return mxCreateNumericMatrix_730(M, N, type, complexity);
    } else {
        return mxCreateNumericMatrix_700(M, N, type, complexity);
    }
}

}  // namespace mex
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::mex;

extern "C" {

WEAK void halide_matlab_describe_pipeline(stringstream &desc, const halide_filter_metadata_t *metadata) {
    desc << "int " << metadata->name << "(";
    for (int i = 0; i < metadata->num_arguments; i++) {
        const halide_filter_argument_t *arg = &metadata->arguments[i];
        if (i > 0) {
            desc << ", ";
        }
        if (arg->kind == halide_argument_kind_output_buffer) {
            desc << "out ";
        }
        if (arg->kind == halide_argument_kind_output_buffer ||
            arg->kind == halide_argument_kind_input_buffer) {
            desc << arg->dimensions << "d ";
        } else if (arg->kind == halide_argument_kind_input_scalar) {
            desc << "scalar ";
        }
        desc << get_class_name(get_class_id(arg->type.code, arg->type.bits));
        desc << " '" << arg->name << "'";
    }
    desc << ")";
}

WEAK void halide_matlab_note_pipeline_description(void *user_context, const halide_filter_metadata_t *metadata) {
    stringstream desc(user_context);
    desc << "Note pipeline definition:\n";
    halide_matlab_describe_pipeline(desc, metadata);
    halide_print(user_context, desc.str());
}

WEAK void halide_matlab_error(void *user_context, const char *msg) {
    // Note that mexErrMsg/mexErrMsgIdAndTxt crash Matlab. It seems to
    // be a common problem, those APIs seem to be very fragile.
    stringstream error_msg(user_context);
    error_msg << "\nHalide Error: " << msg;
    mexWarnMsgTxt(error_msg.str());
}

WEAK void halide_matlab_print(void *, const char *msg) {
    mexWarnMsgTxt(msg);
}

WEAK int halide_matlab_init(void *user_context) {
    // Assume that if mexWarnMsgTxt exists, we've already attempted initialization.
    if (mexWarnMsgTxt != nullptr) {
        return halide_error_code_success;
    }

// clang-format off
#define MEX_FN(ret, func, args)                 func = get_mex_symbol<ret(*) args>(user_context, #func, true);          // NOLINT(bugprone-macro-parentheses)
#define MEX_FN_700(ret, func, func_700, args)   func_700 = get_mex_symbol<ret(*) args>(user_context, #func, false);     // NOLINT(bugprone-macro-parentheses)
#define MEX_FN_730(ret, func, func_730, args)   func_730 = get_mex_symbol<ret(*) args>(user_context, #func_730, false); // NOLINT(bugprone-macro-parentheses)
#include "mex_functions.h"
#undef MEX_FN_730
#undef MEX_FN_700
#undef MEX_FN
    // clang-format on

    if (!mexWarnMsgTxt) {
        return halide_error_code_matlab_init_failed;
    }

    // Set up Halide's printing to go through Matlab. Also, don't exit
    // on error. We don't just replace halide_error/halide_printf,
    // because they'd have to be weak here, and there would be no
    // guarantee that we would get this version (and not the standard
    // one).
    halide_set_custom_print(halide_matlab_print);
    halide_set_error_handler(halide_matlab_error);

    return halide_error_code_success;
}

// Convert a matlab mxArray to a Halide halide_buffer_t, with a specific number of dimensions.
WEAK int halide_matlab_array_to_halide_buffer_t(void *user_context,
                                                const mxArray *arr,
                                                const halide_filter_argument_t *arg,
                                                halide_buffer_t *buf) {

    if (mxIsComplex(arr)) {
        error(user_context) << "Complex argument not supported for parameter " << arg->name << ".\n";
        return halide_error_code_matlab_bad_param_type;
    }

    int dim_count = get_number_of_dimensions(arr);
    int expected_dims = arg->dimensions;

    // Validate that the data type of a buffer matches exactly.
    mxClassID arg_class_id = get_class_id(arg->type.code, arg->type.bits);
    mxClassID class_id = mxGetClassID(arr);
    if (class_id != arg_class_id) {
        error(user_context) << "Expected type of class " << get_class_name(arg_class_id)
                            << " for argument " << arg->name
                            << ", got class " << get_class_name(class_id) << ".\n";
        return halide_error_code_matlab_bad_param_type;
    }
    // Validate that the dimensionality matches. Matlab is wierd
    // because matrices always have at least 2 dimensions, and it
    // truncates trailing dimensions of extent 1. So, the only way
    // to have an error here is to have more dimensions with
    // extent != 1 than the Halide pipeline expects.
    while (dim_count > 0 && get_dimension(arr, dim_count - 1) == 1) {
        dim_count--;
    }
    if (dim_count > expected_dims) {
        error(user_context) << "Expected array of rank " << expected_dims
                            << " for argument " << arg->name
                            << ", got array of rank " << dim_count << ".\n";
        return halide_error_code_matlab_bad_param_type;
    }

    buf->host = (uint8_t *)mxGetData(arr);
    buf->type = arg->type;
    buf->dimensions = arg->dimensions;
    buf->set_host_dirty(true);

    for (int i = 0; i < dim_count && i < expected_dims; i++) {
        buf->dim[i].extent = static_cast<int32_t>(get_dimension(arr, i));
    }

    // Add back the dimensions with extent 1.
    for (int i = 2; i < expected_dims; i++) {
        if (buf->dim[i].extent == 0) {
            buf->dim[i].extent = 1;
        }
    }

    // Compute dense strides.
    buf->dim[0].stride = 1;
    for (int i = 1; i < expected_dims; i++) {
        buf->dim[i].stride = buf->dim[i - 1].extent * buf->dim[i - 1].stride;
    }

    return halide_error_code_success;
}

// Convert a matlab mxArray to a scalar.
WEAK int halide_matlab_array_to_scalar(void *user_context,
                                       const mxArray *arr, const halide_filter_argument_t *arg, void *scalar) {
    if (mxIsComplex(arr)) {
        error(user_context) << "Complex argument not supported for parameter " << arg->name << ".\n";
        return halide_error_code_generic_error;
    }

    // Validate that the mxArray has all dimensions of extent 1.
    int dim_count = get_number_of_dimensions(arr);
    for (int i = 0; i < dim_count; i++) {
        if (get_dimension(arr, i) != 1) {
            error(user_context) << "Expected scalar argument for parameter " << arg->name << ".\n";
            return halide_error_code_matlab_bad_param_type;
        }
    }
    if (!mxIsLogical(arr) && !mxIsNumeric(arr)) {
        error(user_context) << "Expected numeric argument for scalar parameter " << arg->name
                            << ", got " << get_class_name(mxGetClassID(arr)) << ".\n";
        return halide_error_code_matlab_bad_param_type;
    }

    double value = mxGetScalar(arr);
    int32_t type_code = arg->type.code;
    int32_t type_bits = arg->type.bits;

    if (type_code == halide_type_int) {
        switch (type_bits) {
        case 1:
            *reinterpret_cast<bool *>(scalar) = value != 0;
            return halide_error_code_success;
        case 8:
            *reinterpret_cast<int8_t *>(scalar) = static_cast<int8_t>(value);
            return halide_error_code_success;
        case 16:
            *reinterpret_cast<int16_t *>(scalar) = static_cast<int16_t>(value);
            return halide_error_code_success;
        case 32:
            *reinterpret_cast<int32_t *>(scalar) = static_cast<int32_t>(value);
            return halide_error_code_success;
        case 64:
            *reinterpret_cast<int64_t *>(scalar) = static_cast<int64_t>(value);
            return halide_error_code_success;
        }
    } else if (type_code == halide_type_uint) {
        switch (type_bits) {
        case 1:
            *reinterpret_cast<bool *>(scalar) = value != 0;
            return halide_error_code_success;
        case 8:
            *reinterpret_cast<uint8_t *>(scalar) = static_cast<uint8_t>(value);
            return halide_error_code_success;
        case 16:
            *reinterpret_cast<uint16_t *>(scalar) = static_cast<uint16_t>(value);
            return halide_error_code_success;
        case 32:
            *reinterpret_cast<uint32_t *>(scalar) = static_cast<uint32_t>(value);
            return halide_error_code_success;
        case 64:
            *reinterpret_cast<uint64_t *>(scalar) = static_cast<uint64_t>(value);
            return halide_error_code_success;
        }
    } else if (type_code == halide_type_float) {
        switch (type_bits) {
        case 32:
            *reinterpret_cast<float *>(scalar) = static_cast<float>(value);
            return halide_error_code_success;
        case 64:
            *reinterpret_cast<double *>(scalar) = static_cast<double>(value);
            return halide_error_code_success;
        }
    } else if (type_code == halide_type_handle) {
        error(user_context) << "Parameter " << arg->name << " is of a type not supported by Matlab.\n";
        return halide_error_code_matlab_bad_param_type;
    }
    error(user_context) << "Halide metadata for " << arg->name << " contained invalid or unrecognized type description.\n";
    return halide_error_code_internal_error;
}

WEAK int halide_matlab_call_pipeline(void *user_context,
                                     int (*pipeline)(void **args), const halide_filter_metadata_t *metadata,
                                     int nlhs, mxArray **plhs, int nrhs, const mxArray **prhs) {

    int init_result = halide_matlab_init(user_context);
    if (init_result != 0) {
        return init_result;
    }

    int32_t result_storage;
    int32_t *result_ptr = &result_storage;
    if (nlhs > 0) {
        plhs[0] = create_numeric_matrix(1, 1, mxINT32_CLASS, mxREAL);
        result_ptr = get_data<int32_t>(plhs[0]);
    }
    int32_t &result = *result_ptr;

    // Set result to failure until proven otherwise.
    result = halide_error_code_generic_error;

    // Validate the number of arguments is correct.
    if (nrhs != metadata->num_arguments) {
        if (nrhs > 0) {
            // Only report an actual error if there were any arguments at all.
            error(user_context) << "Expected " << metadata->num_arguments
                                << " arguments for Halide pipeline " << metadata->name
                                << ", got " << nrhs << ".\n";
        }
        halide_matlab_note_pipeline_description(user_context, metadata);
        return result;
    }

    // Validate the LHS has zero or one argument.
    if (nlhs > 1) {
        error(user_context) << "Expected zero or one return value for Halide pipeline " << metadata->name
                            << ", got " << nlhs << ".\n";
        halide_matlab_note_pipeline_description(user_context, metadata);
        return result;
    }

    void **args = (void **)__builtin_alloca(nrhs * sizeof(void *));
    for (int i = 0; i < nrhs; i++) {
        const mxArray *arg = prhs[i];
        const halide_filter_argument_t *arg_metadata = &metadata->arguments[i];

        if (arg_metadata->kind == halide_argument_kind_input_buffer ||
            arg_metadata->kind == halide_argument_kind_output_buffer) {
            halide_buffer_t *buf = (halide_buffer_t *)__builtin_alloca(sizeof(halide_buffer_t));
            memset(buf, 0, sizeof(halide_buffer_t));
            buf->dim = (halide_dimension_t *)__builtin_alloca(sizeof(halide_dimension_t) * arg_metadata->dimensions);
            memset(buf->dim, 0, sizeof(halide_dimension_t) * arg_metadata->dimensions);
            result = halide_matlab_array_to_halide_buffer_t(user_context, arg, arg_metadata, buf);
            if (result != 0) {
                halide_matlab_note_pipeline_description(user_context, metadata);
                return result;
            }
            args[i] = buf;
        } else {
            size_t size_bytes = max(8, (arg_metadata->type.bits + 7) / 8);
            void *scalar = __builtin_alloca(size_bytes);
            memset(scalar, 0, size_bytes);
            result = halide_matlab_array_to_scalar(user_context, arg, arg_metadata, scalar);
            if (result != 0) {
                halide_matlab_note_pipeline_description(user_context, metadata);
                return result;
            }
            args[i] = scalar;
        }
    }

    result = pipeline(args);

    // Copy any GPU resident output buffers back to the CPU before returning.
    for (int i = 0; i < nrhs; i++) {
        const halide_filter_argument_t *arg_metadata = &metadata->arguments[i];

        if (arg_metadata->kind == halide_argument_kind_output_buffer) {
            halide_buffer_t *buf = (halide_buffer_t *)args[i];
            if ((result = halide_copy_to_host(user_context, buf)) != 0) {
                error(user_context) << "halide_matlab_call_pipeline: halide_copy_to_host failed.\n";
                return result;
            }
        }
        if (arg_metadata->kind == halide_argument_kind_input_buffer ||
            arg_metadata->kind == halide_argument_kind_output_buffer) {
            halide_buffer_t *buf = (halide_buffer_t *)args[i];
            if ((result = halide_device_free(user_context, buf)) != 0) {
                error(user_context) << "halide_matlab_call_pipeline: halide_device_free failed.\n";
                return result;
            }
        }
    }

    return result;
}

}  // extern "C"
