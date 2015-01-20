
/* Python naive wrapper for Halide C++ implementation, resulting in module cHalide. */

%module(naturalvar=1) cHalide
%include "stdint.i"

%ignore halide_debug_to_file;
%ignore halide_malloc;
%ignore halide_free;
%ignore halide_start_clock;
%ignore halide_current_time_ns;
%ignore halide_profiling_timer;
%ignore halide_printf;
%ignore halide_error;
%ignore halide_error_varargs;
%ignore halide_do_par_for;
%ignore halide_shutdown_thread_pool;
%ignore halide_trace;
%ignore halide_shutdown_trace;
%ignore halide_set_random_seed;
%ignore halide_printf;
%ignore halide_print;
%ignore get_scalar;
%ignore set_scalar;
%ignore get;

%ignore halide_release;
%ignore halide_copy_to_dev;
%ignore halide_copy_to_host;
%ignore halide_dev_sync;
%ignore halide_init_kernels;
%ignore halide_dev_run;
%ignore halide_dev_malloc;
%ignore halide_dev_free;
%ignore halide_get_trace_file;
%ignore halide_set_trace_file;

%ignore halide_set_num_threads;

%ignore halide_get_ocl_platform_name;
%ignore halide_set_ocl_platform_name;
%ignore halide_get_ocl_device_type;
%ignore halide_set_ocl_device_type;
%ignore halide_get_gpu_device;
%ignore halide_set_gpu_device;
%ignore halide_memoization_cache_set_size;
%ignore halide_memoization_cache_lookup;
%ignore halide_memoization_cache_store;
%ignore halide_memoization_cache_cleanup;

%ignore halide_opengl_context_lost;
%ignore halide_opengl_output_client_bound;

%ignore halide_mutex_lock;
%ignore halide_mutex_unlock;
%ignore halide_mutex_cleanup;

#define BUILDING_PYTHON 1

%{
#include "Halide.h"
#include "py_util.h"
using namespace Halide;
%}

// ~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~
%typemap(out) std::string {
%#if PY_VERSION_HEX >= 0x03000000
  $result = PyBytes_FromStringAndSize($1.data(), $1.size());
%#else
  $result = PyString_FromStringAndSize($1.data(), $1.size());
%#endif
}

#define DEFINE_TYPE(T) std::string image_to_string(const Image<T> &a);
DEFINE_TYPE(uint8_t)
DEFINE_TYPE(uint16_t)
DEFINE_TYPE(uint32_t)
DEFINE_TYPE(int8_t)
DEFINE_TYPE(int16_t)
DEFINE_TYPE(int32_t)
DEFINE_TYPE(float)
DEFINE_TYPE(double)
#undef DEFINE_TYPE

%typemap(out) std::string; // Reset the typemap
// ~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~

namespace Halide {
%ignore Internal;
}
%ignore HalideIntrospectionCanary;

%include "std_string.i"
%include "std_vector.i"

%naturalvar;
%naturalvar Func;
%naturalvar ScheduleHandle;
%naturalvar Expr;
%naturalvar RVar;
%naturalvar RDom;
%naturalvar VarOrRVar;
%naturalvar Argument;

%include "Halide.h"
%include "py_util.h"

%template(Image_uint8) Image<uint8_t>;
%template(Image_uint16) Image<uint16_t>;
%template(Image_uint32) Image<uint32_t>;
%template(Image_int8) Image<int8_t>;
%template(Image_int16) Image<int16_t>;
%template(Image_int32) Image<int32_t>;
%template(Image_float32) Image<float>;
%template(Image_float64) Image<double>;

%template(Param_uint8) Param<uint8_t>;
%template(Param_uint16) Param<uint16_t>;
%template(Param_uint32) Param<uint32_t>;
%template(Param_int8) Param<int8_t>;
%template(Param_int16) Param<int16_t>;
%template(Param_int32) Param<int32_t>;
%template(Param_float32) Param<float>;
%template(Param_float64) Param<double>;

//%template(Image_uint64) Image<uint64_t>;
//%template(Image_int64) Image<int64_t>;

namespace std {
   %template(ListExpr) vector<Expr>;
   %template(ListVar) vector<Var>;
   %template(ListFunc) vector<Func>;
   %template(ListInt) vector<int>;
   %template(ListString) vector<std::string>;
   %template(ListArgument) vector<Argument>;
   %template(ListBuffer) vector<Buffer>;
};
