#include <sstream>
#include <iostream>
#include <limits>

#include "CodeGen_JavaScript.h"
#include "CodeGen_Internal.h"
#include "Deinterleave.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Param.h"
#include "Var.h"
#include "Lerp.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::ostream;
using std::endl;
using std::string;
using std::vector;
using std::ostringstream;
using std::map;

namespace {

// TODO: Fill in preamble...
const char * const preamble = R"INLINE_CODE(
// TODO: make this workable
// "use strict";

var halide_error_code_success = 0;
var halide_error_code_generic_error = -1;
var halide_error_code_explicit_bounds_too_small = -2;
var halide_error_code_bad_type = -3;
var halide_error_code_access_out_of_bounds = -4;
var halide_error_code_buffer_allocation_too_large = -5;
var halide_error_code_buffer_extents_too_large = -6;
var halide_error_code_constraints_make_required_region_smaller = -7;
var halide_error_code_constraint_violated = -8;
var halide_error_code_param_too_small = -9;
var halide_error_code_param_too_large = -10;
var halide_error_code_out_of_memory = -11;
var halide_error_code_buffer_argument_is_null = -12;
var halide_error_code_debug_to_file_failed = -13;
var halide_error_code_copy_to_host_failed = -14;
var halide_error_code_copy_to_device_failed = -15;
var halide_error_code_device_malloc_failed = -16;
var halide_error_code_device_sync_failed = -17;
var halide_error_code_device_free_failed = -18;
var halide_error_code_no_device_interface = -19;
var halide_error_code_matlab_init_failed = -20;
var halide_error_code_matlab_bad_param_type = -21;
var halide_error_code_internal_error = -22;
var halide_error_code_buffer_extents_negative = -28;
var halide_error_code_bad_dimensions = -43;
if (typeof(Math.fround) !== "function") { Math.fround = function (x) { return new Float32Array([x])[0]; } }
if (typeof(halide_print) !== "function") { halide_print = function (user_context, msg) { console.log(msg); } }
if (typeof(halide_error) !== "function") { halide_error = function (user_context, msg) { halide_print(user_context, msg); } }
if (typeof(halide_trace) !== "function") { var id = 0; halide_trace = function (user_context, event) { return id++; } }
if (typeof(halide_shutdown_trace) !== "function") { halide_shutdown_trace = function () { return 0; } }
if (typeof(halide_debug_to_file) !== "function") { halide_debug_to_file = function (user_context, filename, typecode, buffer) { halide_print(user_context, "halide_debug_to_file called. Implementation needed."); return 0; } }
if (typeof(fast_inverse_f32) !== "function") { fast_inverse_f32 = function(x) { return 1 / x; } }
if (typeof(fast_inverse_sqrt_f32) !== "function") { fast_inverse_sqrt_f32 = function(x) { return 1 / Math.sqrt(x); } }
if (typeof(halide_error_bounds_inference_call_failed) !== "function") { halide_error_bounds_inference_call_failed =
    function(user_context, extern_stage_name, result) {
        halide_error(user_context, "Bounds inference call to external stage " + extern_stage_name + " returned non-zero value: " + result);
        return result; } }
if (typeof(halide_error_extern_stage_failed) !== "function") { halide_error_extern_stage_failed =
    function(user_context, func_name, var_name) {
        halide_error(user_context, "Call to external stage " + extern_stage_name + " returned non-zero value: " + result);
        return result; } }
if (typeof(halide_error_explicit_bounds_too_small) !== "function") {  halide_error_explicit_bounds_too_small =
    function(user_context, func_name, var_name, min_bound, max_bound, min_required, max_required) {
         halide_error(user_context, "Bounds given for " + var_name + " in " + func_name + " (from " + min_bound + " to " + max_bound + ") do not cover required region (from " + min_required + " to " + max_required + ")");
         return halide_error_code_explicit_bounds_too_small; } }
if (typeof(halide_error_bad_type) !== "function") {  halide_error_bad_type =
    function(user_context, func_name, type_given_bits, correct_type_bits) {
     halide_error(user_context, func_name + " has type " + type_given_bits + " but type of the buffer passed in is " + correct_type_bits);
    return halide_error_code_bad_type; } }

if (typeof(halide_error_bad_dimensions) !== "function") {  halide_error_bad_dimensions =
    function(user_context, func_name, dimensions_given, correct_dimensions) {
     halide_error(user_context, func_name + " requires a buffer of exactly " + dimensions_given + " dimensions, but the buffer passed in has " + dimensions_given + " dimensions.");
    return halide_error_code_bad_dimensions; } }

if (typeof(halide_error_access_out_of_bounds) !== "function") {  halide_error_access_out_of_bounds =
    function(user_context, func_name, dimension, min_touched, max_touched, min_valid, max_valid) {
        if (min_touched < min_valid) {
            halide_error(user_context, func_name + " is accessed at " + min_touched + ", which is before the min (" + min_valid + ") in dimension " + dimension);
        } else if (max_touched > max_valid) {
            halide_error(user_context, func_name + " is accessed at " + max_touched + ", which is beyond the max (" + max_valid + ") in dimension " + dimension);
        }
        return halide_error_code_access_out_of_bounds; } }
if (typeof(halide_error_buffer_allocation_too_large) !== "function") {  halide_error_buffer_allocation_too_large =
    function(user_context, buffer_name, allocation_size, max_size) {
    halide_error(user_context, "Total allocation for buffer " + buffer_name + " is " + allocation_size + ", which exceeds the maximum size of " + max_size);
    return halide_error_code_buffer_allocation_too_large; } }
if (typeof(halide_error_buffer_extents_negative) !== "function") {  halide_error_buffer_extents_negative =
    function(user_context, buffer_name, dimension, extent) {
        halide_error(user_context, + "The extents for " + buffer_name + " dimension " + dimension + " is negative (" + extent + ")");
        return halide_error_code_buffer_extents_negative; } }
if (typeof(halide_error_buffer_extents_too_large) !== "function") {  halide_error_buffer_extents_too_large =
    function(user_context, buffer_name, actual_size, max_size) {
        halide_error(user_context, + "Product of extents for buffer " + buffer_name + " is " + actual_size + ", which exceeds the maximum size of " + max_size);
        return halide_error_code_buffer_extents_too_large; } }
if (typeof(halide_error_constraints_make_required_region_smaller) !== "function") {  halide_error_constraints_make_required_region_smaller =
    function(user_context, buffer_name, dimension, constrained_min, constrained_extent, required_min, required_extent) {
        var required_max = required_min + required_extent - 1;
        var constrained_max = constrained_min + required_extent - 1;
        halide_error(user_context, "Applying the constraints on " + buffer_name + " to the required region made it smaller. Required size: " + required_min + " to " + required_max + ". Constrained size: " + constrained_min + " to " + constrained_max + ".");
        return halide_error_code_constraints_make_required_region_smaller; } }
if (typeof(halide_error_constraint_violated) !== "function") {  halide_error_constraint_violated =
    function(user_context, var_name, value, constrained_var, constrained_val) {
        halide_error(user_context, "Constraint violated: " + var_name + " (" + value + ") == " + constrained_var + " (" + constrained_var + ")");
        return halide_error_code_constraint_violated; } }
if (typeof(halide_error_param_too_small_i64) !== "function") {  halide_error_param_too_small_i64 =
    function(user_context, param_name, value, min_val) {
        halide_error(user_context, "Parameter " + param_name + " is " + value + " but must be at least " + min_val);
        return halide_error_code_param_too_small; } }
if (typeof(halide_error_param_too_small_u64) !== "function") {  halide_error_param_too_small_u64 =
    function(user_context, param_name, value, min_val) {
        halide_error(user_context, "Parameter " + param_name + " is " + value + " but must be at least " + min_val);
        return halide_error_code_param_too_small; } }
if (typeof(halide_error_param_too_small_f64) !== "function") {  halide_error_param_too_small_f64 =
    function(user_context, param_name, value, min_val) {
        halide_error(user_context, "Parameter " + param_name + " is " + value + " but must be at least " + min_val);
        return halide_error_code_param_too_small; } }
if (typeof(halide_error_param_too_large_i64) !== "function") {  halide_error_param_too_large_i64 =
        function(user_context, param_name, value, max_val) {
        halide_error(user_context, "Parameter " + param_name + " is " + value + " but must be at most " + max_val);
        return halide_error_code_param_too_large; } }
if (typeof(halide_error_param_too_large_u64) !== "function") {  halide_error_param_too_large_u64 =
    function(user_context, param_name, value, max_val) {
        halide_error(user_context, "Parameter " + param_name + " is " + value + " but must be at most " + max_val);
        return halide_error_code_param_too_large; } }
if (typeof(halide_error_param_too_large_f64) !== "function") {  halide_error_param_too_large_f64 =
    function(user_context, param_name, value, max_val) {
        halide_error(user_context, "Parameter " + param_name + " is " + value + " but must be at most " + min_val);
        return halide_error_code_param_too_large; } }
if (typeof(halide_error_out_of_memory) !== "function") {  halide_error_out_of_memory =
    function (user_context) {
        halide_error(user_context, "Out of memory (halide_malloc returned NULL)");
        return halide_error_code_out_of_memory; } }
if (typeof(halide_error_buffer_argument_is_null) !== "function") {  halide_error_buffer_argument_is_null =
    function(user_context, buffer_name) {
        halide_error(user_context, "Buffer argument " + buffer_name + " is NULL");
        return halide_error_code_buffer_argument_is_null; } }
if (typeof(halide_error_debug_to_file_failed) !== "function") {  halide_error_debug_to_file_failed =
    function(user_context, func, filename, error_code) {
        halide_error(user_context, "Failed to dump function " + func + " to file " + filename + " with error " + error_code);
        return halide_error_code_debug_to_file_failed; } }

var halide_memoization_cache_lookup;
var halide_memoization_cache_store;
var halide_memoization_cache_release;
var halide_memoization_cache_cleanup;
var halide_memoization_cache_set_size;
if (typeof(halide_memoization_cache_lookup) !== "function" ||
    typeof(halide_memoization_cache_store) !== "function" ||
    typeof(halide_memoization_cache_release) !== "function" ||
    typeof(halide_memoization_cache_set_size) !== "function" ||
    typeof(halide_memoization_cache_cleanup) !== "function") {
    (function () {
        var max_cache_size = 1 << 20;
        var current_cache_size = 0;
        var entries = {};
        var most_recent = null;
        var least_recent = null;
        var prune_cache = function() {
            while (current_cache_size > max_cache_size && least_recent != NULL) {
                var entry = least_recent;
                least_recent = entry.more_recent;
                if (most_recent == entry) {
                    most_recent = null;
                }
                if (least_recent != null) {
                    least_recent.less_recent = null;
                }
                delete entries[entry.key];
                current_cache_size -= entry.size;
            }
        }
        halide_memoization_set_cache_size = function(size) {
            if (size == 0) {
                size = 1 << 20;
            }
            max_cache_size = size;
            prune_cache();
        }
        function memoization_full_cache_key(cache_key, size, computed_bounds) {
            var result = "";
            for (var c = 0; c < size; c++) {
                result += String.fromCharCode(cache_key[c]);
            }
            for (var i = 0; i < computed_bounds.extent.length; i++) {
                result += computed_bounds.min[i].toString() + computed_bounds.extent[i].toString() + computed_bounds.stride[i].toString();
            }
            return result;
        }
        function new_entry(buf) {
            var total_size = 1;
            for (var i = 0; i < buf.extent.length && buf.extent[i] != 0; i++) {
                var stride = buf.stride[i];
                if (stride < 0) stride = -stride;
                if (buf.extent[i] * stride > total_size) {
                    total_size = buf.extent[i] * stride;
                }
             }
             buf.host = new buf.array_constructor(total_size);
        }
        halide_memoization_cache_lookup = function(user_context, cache_key, size, computed_bounds, tuple_count, tuple_buffers) {
            var key = memoization_full_cache_key(cache_key, size, computed_bounds);
            if (key in entries) {
                var entry = entries[key];
                for (var i = 0; i < tuple_count; i++) {
                    tuple_buffers[i].host = entry[i].host;
                }

                return 0;
            }
            for (var i = 0; i < tuple_count; i++) {
                new_entry(tuple_buffers[i]);
            }
            return 1;
        }
        halide_memoization_cache_store = function(user_context, cache_key, size, computed_bounds, tuple_count, tuple_buffers) {
            var key = memoization_full_cache_key(cache_key, size, computed_bounds);
            if (key in entries) {
                return 0;
            } else {
                var entry = tuple_buffers.slice();
                entries[key] = entry;
            }
            return 0;
        }
        halide_memoization_cache_release = function(user_context, host) {
        }
        halide_memoization_cache_cleanup = function() {
            entries = {};
            current_cache_size = 0;
        }
    })();
}

if (typeof(halide_quiet_div) !== "function") {
    halide_quiet_div = function (a, b) { return b == 0 ? 0 : (a / b); }
}

if (typeof(halide_quiet_mod) !== "function") {
    halide_quiet_mod = function (a, b) { return b == 0 ? 0 : (a % b); }
}

if (typeof(halide_round) !== "function") {
    halide_round =  function (num) {
         var r = Math.round(num);
         if (r == num + 0.5 && (r % 2)) { r = Math.floor(num); }
         return r;
        }
}

if (typeof(halide_shuffle_vector) !== "function") {
    halide_shuffle_vector =  function (a, indices) {
        var r = []
        for (var i = 0; i < a.length; i++) {
            if (indices[i] < 0) {
                continue;
            }
            r.push(a[indices[i]]);
        }
        return r;
    }
}

if (typeof(halide_concat_vectors) !== "function") {
    halide_concat_vectors =  function (vecs) {
        var r = []
        for (var i = 0; i < vecs.length; i++) {
            r = r.concat(vecs[i])
        }
        return r;
    }
}

var _halide_buffer_get_dimensions;
var _halide_buffer_get_host;
var _halide_buffer_get_device;
var _halide_buffer_get_device_interface;
var _halide_buffer_get_min;
var _halide_buffer_get_max;
var _halide_buffer_get_extent;
var _halide_buffer_get_stride;
var _halide_buffer_set_host_dirty;
var _halide_buffer_set_device_dirty;
var _halide_buffer_get_host_dirty;
var _halide_buffer_get_device_dirty;
var _halide_buffer_get_shape;
var _halide_buffer_is_bounds_query;
var _halide_buffer_get_type;
var _halide_buffer_init;
var _halide_buffer_init_from_buffer;
var _halide_buffer_crop;
var _halide_buffer_set_bounds;
var _halide_buffer_retire_crop_after_extern_stage;
var _halide_buffer_retire_crops_after_extern_stage;

if (typeof(_halide_buffer_get_dimensions) !== "function" ||
  typeof(_halide_buffer_get_host) !== "function" ||
  typeof(_halide_buffer_get_device) !== "function" ||
  typeof(_halide_buffer_get_device_interface) !== "function" ||
  typeof(_halide_buffer_get_min) !== "function" ||
  typeof(_halide_buffer_get_max) !== "function" ||
  typeof(_halide_buffer_get_extent) !== "function" ||
  typeof(_halide_buffer_get_stride) !== "function" ||
  typeof(_halide_buffer_set_host_dirty) !== "function" ||
  typeof(_halide_buffer_set_device_dirty) !== "function" ||
  typeof(_halide_buffer_get_host_dirty) !== "function" ||
  typeof(_halide_buffer_get_device_dirty) !== "function" ||
  typeof(_halide_buffer_get_shape) !== "function" ||
  typeof(_halide_buffer_is_bounds_query) !== "function" ||
  typeof(_halide_buffer_get_type) !== "function" ||
  typeof(_halide_buffer_init) !== "function" ||
  typeof(_halide_buffer_init_from_buffer) !== "function" ||
  typeof(_halide_buffer_crop) !== "function" ||
  typeof(_halide_buffer_retire_crop_after_extern_stage) !== "function" ||
  typeof(_halide_buffer_retire_crops_after_extern_stage) !== "function" ||
  typeof(_halide_buffer_set_bounds) !== "function") {
  (function () {
    // TODO: these are intended to be adequate standalone replacements
    // for the ones baked into the JIT support, but have not been tested.
    _halide_buffer_create = function() {
        return {
            host: null,
            device: null,
            device_interface: null,
            type_code: 0,
            type_bits: 0,
            dim: null,
            flags: 0
        }
    }

    _halide_buffer_get_dimensions = function(buf) {
        return buf.dim.length;
    }

    _halide_buffer_get_host = function(buf) {
        return buf.host;
    }

    _halide_buffer_get_device = function(buf) {
        return buf.device;
    }

    _halide_buffer_get_device_interface = function(buf) {
        return buf.device_interface;
    }

    _halide_buffer_get_min = function(buf, d) {
        return buf.dim[d].min;
    }

    _halide_buffer_get_max = function(buf, d) {
        return buf.dim[d].min + buf.dim[d].extent - 1;
    }

    _halide_buffer_get_extent = function(buf, d) {
        return buf.dim[d].extent;
    }

    _halide_buffer_get_stride = function(buf, d) {
        return buf.dim[d].stride;
    }

    _halide_buffer_set_host_dirty = function(buf, val) {
        if (val)
            buf.flags |= 1;
        else
            buf.flags &= ~1;
        return 0;
    }

    _halide_buffer_set_device_dirty = function(buf, val) {
        if (val)
            buf.flags |= 2;
        else
            buf.flags &= ~2;
        return 0;
    }

    _halide_buffer_get_host_dirty = function(buf) {
        return (buf.flags & 1) != 0;
    }

    _halide_buffer_get_device_dirty = function(buf) {
        return (buf.flags & 2) != 0;
    }

    _halide_buffer_get_shape = function(buf) {
        return buf.dim;
    }

    _halide_buffer_is_bounds_query = function(buf) {
        return !buf.host && !buf.device;
    }

    _halide_buffer_get_type = function(buf) {
        return buf.type_code | (buf.type_bits << 8) | (1 << 16);
    }

    _halide_buffer_init_shape = function(buf, d) {
        // assert(buf.dim == null || buf.dim.length == d)
        if (!buf.dim || buf.dim.length != d) {
            buf.dim = []
            for (var i = 0; i < d; i++) {
                buf.dim.push({min:0, extent:0, stride:0, flags:0});
            }
        }
        return buf;
    }

    _halide_buffer_init = function(dst, dst_shape, host, device, device_interface,
                                         type_code, type_bits,
                                         dimensions,
                                         shape,
                                         flags) {
        // dst_shape is always ignored in JS
        // assert(dimensions == shape.length)
        dst.host = host;
        dst.device = device;
        dst.device_interface = device_interface;
        dst.type_code = type_code;
        dst.type_bits = type_bits;
        _halide_buffer_init_shape(dst, dimensions);
        for (var i = 0; i < dst.dim.length; i++) {
            dst.dim[i].min    = shape[i*4 + 0];
            dst.dim[i].extent = shape[i*4 + 1];
            dst.dim[i].stride = shape[i*4 + 2];
            dst.dim[i].flags  = shape[i*4 + 3];
        }
        dst.flags = flags;
        return dst;
    }

    _halide_buffer_init_from_buffer = function(dst, dst_shape, src) {
        // dst_shape is always ignored in JS
        // assert(src.dim.length == dst.dim.length)
        dst.host = src.host;
        dst.device = src.device;
        dst.device_interface = src.device_interface;
        dst.type_code = src.type_code;
        dst.type_bits = src.type_bits;
        _halide_buffer_init_shape(dst, src.dim.length);
        for (var i = 0; i < dst.dim.length; i++) {
            dst.dim[i].min    = src.dim[i].min;
            dst.dim[i].extent = src.dim[i].extent;
            dst.dim[i].stride = src.dim[i].stride;
            dst.dim[i].flags  = src.dim[i].flags;
        }
        dst.flags = src.flags;
        return dst;
    }

    _halide_buffer_crop = function(user_context, dst, dst_shape, src, min, extent) {
        halide_error(user_context, "TODO: _halide_buffer_crop is unimplemented");
        return dst;
    }

    _halide_buffer_retire_crop_after_extern_stage = function(user_context, b) {
        halide_error(user_context, "TODO: _halide_buffer_retire_crop_after_extern_stage is unimplemented");
        return dst;
    }

    _halide_buffer_retire_crops_after_extern_stage = function(user_context, b) {
        halide_error(user_context, "TODO: _halide_buffer_retire_crops_after_extern_stage is unimplemented");
        return dst;
    }

    _halide_buffer_set_bounds = function(buf, d, min, extent) {
        var stride = buf.dim[d].stride
        // Make a copy in case dim is shared
        buf.dim = buf.dim.slice(0)
        buf.dim[d] = { min: min, extent: extent, stride: stride }
        return buf;
    }
  })();
}

)INLINE_CODE";

template<typename T>
static std::string with_sep(const std::vector<T> &v, const std::string &sep) {
    std::ostringstream o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) {
            o << sep;
        }
        o << v[i];
    }
    return o.str();
}

template<typename T>
static std::string with_commas(const std::vector<T> &v) {
    return with_sep<T>(v, ", ");
}

}  // namespace

CodeGen_JavaScript::CodeGen_JavaScript(ostream &s) : IRPrinter(s), id("$$ BAD ID $$"),
                                                     use_simd_js(false) {
}

CodeGen_JavaScript::~CodeGen_JavaScript() {
}

string CodeGen_JavaScript::make_js_int_cast(const string &value, const Type &src, const Type &dst) {
    // TODO: Do we use print_assignment to cache constants?
    if (src.bits() <= dst.bits() && src.is_uint() == dst.is_uint()) {
        return value;
    }

    internal_assert(dst.bits() != 64) << "Unknown bit width (" << dst.bits() << ") making JavaScript cast.\n";
    const uint64_t mask = (1LL << dst.bits()) - 1;

    string shift_op = dst.is_uint() ? ">>>" : ">>";
    string shift_amount = std::to_string(32 - dst.bits());
    ostringstream rhs;
    rhs << "(" << "(" << value << " & 0x" << std::hex << mask << ")" << " << " << shift_amount << ") " << shift_op << " " << shift_amount;
    return print_assignment(dst, rhs.str());
}

inline constexpr int halide_type_code(halide_type_code_t code, int bits) {
  return ((int) code) | (bits << 8);
}

const char *javascript_type_array_name_fragment(const Type &type) {

    #define HANDLE_CASE(CODE, BITS, NAME) \
        case halide_type_code(CODE, BITS): return NAME;

    switch (halide_type_code(type.code(), type.bits())) {
        HANDLE_CASE(halide_type_float, 32, "Float32")
        HANDLE_CASE(halide_type_float, 64, "Float64")
        HANDLE_CASE(halide_type_int, 8, "Int8")
        HANDLE_CASE(halide_type_int, 16, "Int16")
        HANDLE_CASE(halide_type_int, 32, "Int32")
        HANDLE_CASE(halide_type_uint, 1, "Uint8")
        HANDLE_CASE(halide_type_uint, 8, "Uint8")
        HANDLE_CASE(halide_type_uint, 16, "Uint16")
        HANDLE_CASE(halide_type_uint, 32, "Uint32")
        default:
            user_error << "Unsupported array type:" << type << "\n";
            return "";
    }

    #undef HANDLE_CASE
}

Expr conditionally_extract_lane(Expr e, int lane) {
    internal_assert(lane < e.type().lanes()) << "Bad lane in conditionally_extract_lane\n";
    if (e.type().lanes() != 1) {
        return extract_lane(e, lane);
    } else {
        return e;
    }
}

string CodeGen_JavaScript::print_reinterpret(Type type, Expr e) {
    string from_simd_type;
    string to_simd_type;
    // Both vector length and bit length are required to be the same.
    if (e.type().element_of() == type.element_of()) {
        return print_expr(e);
    } else if (simd_js_type_for_type(e.type(), from_simd_type, false) &&
               simd_js_type_for_type(type, to_simd_type)) {
        return to_simd_type + ".from" + from_simd_type + "Bits(" + print_expr(e) + ")";
    } else {
        if (type.is_handle() && is_zero(e)) {
            return "null";
        }
        ostringstream rhs;
        rhs << literal_may_be_vector_start(type);
        const char *lead = "";

        for (int lane = 0; lane < type.lanes(); lane++) {
            if ((type.is_int() || type.is_uint()) && (e.type().is_int() || e.type().is_uint())) {
                rhs << lead << make_js_int_cast(print_expr(conditionally_extract_lane(e, lane)), e.type(), type);
                lead = ", ";
            } else {
                int32_t bytes_needed = (std::max(type.bits(), e.type().bits()) + 7) / 8;
                string dataview = unique_name('_');
                do_indent();
                stream << "var " << dataview << " = new DataView(new ArrayBuffer(" << bytes_needed << "));\n";
                string setter = string("set") + javascript_type_array_name_fragment(e.type());
                string getter = string("get") + javascript_type_array_name_fragment(type);
                string val = print_expr(conditionally_extract_lane(e, lane));
                do_indent();
                stream << dataview << "." << setter << "(0, " << val << ", true);\n";
                return dataview + "." + getter + "(0, true)";
            }
        }
        rhs << literal_may_be_vector_end(type);
        return rhs.str();
    }
}

string CodeGen_JavaScript::print_name(const string &name) {
    ostringstream oss;

    // Prefix an underscore to avoid reserved words (e.g. a variable named "while")
    if (isalpha(name[0])) {
        oss << '_';
    }

    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '.') {
            oss << '_';
// $ is allowed in JS, but this may be reserved for internal use. I still think it should be passed literally.
//      } else if (name[i] == '$') {
//          oss << "__";
        } else if (name[i] != '_' && !isalnum(name[i])) {
            oss << "___";
        }
        else oss << name[i];
    }
    return oss.str();
}

void CodeGen_JavaScript::compile(const Module &input) {
    if (!input.target().has_feature(Target::NoRuntime)) {
        stream << preamble;
    }

    bool old_use_simd_js = use_simd_js;
    use_simd_js = input.target().has_feature(Target::JavaScript_SIMD);

    for (const auto &b : input.buffers()) {
        compile(b);
    }
    for (const auto &f : input.functions()) {
        compile(f);
    }

    use_simd_js = old_use_simd_js;
}

void CodeGen_JavaScript::compile(const LoweredFunc &f) {
    have_user_context = false;
    for (size_t i = 0; i < f.args.size(); i++) {
        have_user_context |= (f.args[i].name == "__user_context");
    }

    // Emit the function prototype
    stream << "function " << f.name << "(";
    for (size_t i = 0; i < f.args.size(); i++) {
        if (f.args[i].is_buffer()) {
            stream << print_name(f.args[i].name)
                   << "_buffer";
        } else {
            stream << print_name(f.args[i].name);
        }

        if (i < f.args.size() - 1) stream << ", ";
    }

    stream << ") {\n";

    // Emit the body
    print(f.body);

    stream << "return 0;\n"
           << "}\n";
}

void CodeGen_JavaScript::compile(const Buffer<> &buffer) {
    user_assert(!buffer.device_dirty()) << "Can't embed image: " << buffer.name() << "because it has a dirty device pointer.\n";

    string name = print_name(buffer.name());

    // Emit the data in little-endian form
    const uint8_t *src = reinterpret_cast<const uint8_t*>(buffer.data());
    user_assert(src) << "Can't embed image: " << buffer.name() << " because it has a null host pointer.\n";

    string buffer_name = print_assignment(Type(), "_halide_buffer_create()");

    do_indent();
    ostringstream data;
    data << "new Uint8Array([";
    data << (int) src[0];
    const size_t count = buffer.size_in_bytes();
    for (size_t i = 1; i < count; i++) {
        data << "," << (int) src[i];
        if (!(i % 32)) {
            data << "\n";
            do_indent();
        }
    }
    data << "])";
    string data_name = print_assignment(Type(), data.str());

    ostringstream shape;
    shape << "[ ";
    for (int d = 0; d < buffer.dimensions(); ++d) {
        if (d != 0) shape << ", ";
        shape << buffer.dim(d).min() << ", "
               << buffer.dim(d).extent() << ", "
               << buffer.dim(d).stride() << ", "
               << 0;
    }
    shape << " ]";
    string shape_name = print_assignment(Type(), shape.str());

    do_indent();
    stream << "var " << name << " = _halide_buffer_init("
        << buffer_name << ", "
        << "_halide_buffer_get_shape(" << buffer_name << "), "
        << data_name << ", "
        << "null, "  // device
        << "null, "  // device_interface
        << (int) buffer.type().code() << ", "
        << (int) buffer.type().bits() << ", "
        << buffer.dimensions() << ", "
        << shape_name << ", "
        << "0"  // flags
        << ");\n";
}

string CodeGen_JavaScript::print_expr(Expr e) {
    id = "$$ BAD ID $$";
    e.accept(this);
    return id;
}

string CodeGen_JavaScript::print_expr_array(const std::vector<Expr> &exprs) {
    vector<string> values;
    for (size_t i = 0; i < exprs.size(); i++) {
        values.push_back(print_expr(exprs[i]));
    }
    ostringstream rhs;
    rhs << "[ ";
    const char *separator = "";
    for (size_t i = 0; i < exprs.size(); i++) {
        rhs << separator << values[i];
        separator = ", ";
    }
    rhs << " ]";

    return print_assignment(Type(), rhs.str());
}

void CodeGen_JavaScript::print_stmt(Stmt s) {
    s.accept(this);
}

string CodeGen_JavaScript::buffer_host_as_typed_array(const Type &t, const string &buffer_name) {
  string host = print_assignment(Type(), "_halide_buffer_get_host(" + buffer_name + "_buffer)");

  ostringstream array;
  array << "new " << javascript_type_array_name_fragment(t) << "Array(" << host << ".buffer)";
  return print_assignment(Type(), array.str());
}

void CodeGen_JavaScript::clear_cache() {
    rhs_to_id_cache.clear();
    valid_ids_cache.clear();
}

string CodeGen_JavaScript::print_assignment(Type /* t */, const std::string &rhs) {
    internal_assert(!rhs.empty());

    // For some simple constants we never need to bother with assignment.
    if (rhs == "null" || rhs == "0") {
        id = rhs;
        return rhs;
    }

    if (valid_ids_cache.count(rhs)) {
        // No need to do a redundant assignment.
        return rhs;
    }

    // TODO: t is ignored and I expect casts will be required for value correctness.
    // TODO: this could be a lot smarter about caching, but JS has different scoping rules
    auto cached = rhs_to_id_cache.find(rhs);

    if (cached == rhs_to_id_cache.end()) {
        id = unique_name('_');
        do_indent();
        stream << "var " << id << " = " << rhs << ";\n";
        rhs_to_id_cache[rhs] = id;
        valid_ids_cache.insert(id);
    } else {
        id = cached->second;
    }
    return id;
}

void CodeGen_JavaScript::open_scope() {
    clear_cache();
    do_indent();
    indent++;
    stream << "{\n";
}

void CodeGen_JavaScript::close_scope(const std::string &comment) {
    clear_cache();
    indent--;
    do_indent();
    if (!comment.empty()) {
        stream << "} // " << comment << "\n";
    } else {
        stream << "}\n";
    }
}

bool CodeGen_JavaScript::simd_js_type_for_type(Type t, std::string &result, bool include_prefix) {
    if (!use_simd_js) {
        return false;
    }

    result = include_prefix ? "SIMD." : "";

    if (t.is_float() && t.bits() == 32 && t.lanes() == 4) {
        result += "Float32x4";
        return true;
    } else if (t.is_int()) {
        if (t.bits() == 32 && t.lanes() == 4) {
            result += "Int32x4";
            return true;
        } else if (t.bits() == 16 && t.lanes() == 8) {
            result += "Int16x8";
            return true;
        } else if (t.bits() == 8 && t.lanes() == 16) {
            result += "Int8x16";
            return true;
        }
    } else if (t.is_bool()) { // Has to be before uint case because is_uint is true for Bool.
        if (t.lanes() == 4) {
            result += "Bool32x4";
            return true;
        } else if (t.lanes() == 8) {
            result += "Bool8x16";
            return true;
        } else if (t.lanes() == 16) {
            result += "Bool16x8";
            return true;
        }
    } else if (t.is_uint()) {
        if (t.bits() == 32 && t.lanes() == 4) {
            result += "Uint32x4";
            return true;
        } else if (t.bits() == 16 && t.lanes() == 8) {
            result += "Uint16x8";
            return true;
        } else if (t.bits() == 8 && t.lanes() == 16) {
            result += "Uint8x16";
            return true;
        }
    }

    return false;
}

std::string CodeGen_JavaScript::literal_may_be_vector_start(Type t) {
    if (t.lanes() > 1) {
        string simd_js_type;
        if (simd_js_type_for_type(t, simd_js_type)) {
            return simd_js_type + "(";
        } else {
            return "[";
        }
    } else {
        return "";
    }
}

std::string CodeGen_JavaScript::literal_may_be_vector_end(Type t) {
    if (t.lanes() > 1) {
        string simd_js_type;
        if (simd_js_type_for_type(t, simd_js_type)) {
            return ")";
        } else {
            return "]";
        }
    } else {
        return "";
    }
}

void CodeGen_JavaScript::visit(const Variable *op) {
    id = print_name(op->name);
}

std::string CodeGen_JavaScript::fround_start_if_needed(const Type &t) const {
    return (t.is_float() && t.bits() == 32 && (!use_simd_js || t.lanes() != 4)) ? "Math.fround(" : "";
}

std::string CodeGen_JavaScript::fround_end_if_needed(const Type &t) const {
    return (t.is_float() && t.bits() == 32 && (!use_simd_js || t.lanes() != 4)) ? ")" : "";
}

void CodeGen_JavaScript::visit(const Cast *op) {
    Halide::Type src = op->value.type();
    Halide::Type dst = op->type;

    ostringstream rhs;
    rhs << literal_may_be_vector_start(dst);
    const char *lead_char = "";
    for (int lane = 0; lane < dst.lanes(); lane++) {
        string value = print_expr(conditionally_extract_lane(op->value, lane));

        if (dst.is_handle() && src.is_handle()) {
            // My, that was easy
        } else if (dst.is_handle() || src.is_handle()) {
            internal_error << "Can't cast from " << src << " to " << dst << "\n";
        } else if (!src.is_float() && !dst.is_float()) {
            value = make_js_int_cast(value, src, dst);
        } else if (src.is_float() && (dst.is_int() || dst.is_uint())) {
            value = make_js_int_cast("Math.trunc(" + value + ")", Float(64), dst);
        } else {
            internal_assert(dst.is_float());
            value = fround_start_if_needed(op->type) + value + fround_end_if_needed(op->type);
        }
        rhs << lead_char << value;
        lead_char = ", ";
    }
    rhs << literal_may_be_vector_end(dst);
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit_binop(const Type &t, Expr a, Expr b,
                                     const char *op, const char *simd_js_op, const Type &op_result_type) {
    ostringstream rhs;
    std::string simd_js_type;
    if (simd_js_type_for_type(t, simd_js_type)) {
        string sa = print_expr(a);
        string sb = print_expr(b);

        rhs << simd_js_type << "." << simd_js_op << "(" << sa << ", " << sb << ")";
    } else {
        const char *lead_char = (t.lanes() != 1) ? "[" : "";

        internal_assert(t.lanes() > 0);
        const Type element_type = t.element_of();
        for (int lane = 0; lane < t.lanes(); lane++) {
            string sa = print_expr(conditionally_extract_lane(a, lane));
            string sb = print_expr(conditionally_extract_lane(b, lane));
            string val = print_assignment(element_type, sa + " " + op + " " + sb);
            if (!op_result_type.is_handle() && (element_type.is_int() || element_type.is_uint())) {
                val = make_js_int_cast(val, op_result_type, element_type);
            }
            rhs << lead_char << fround_start_if_needed(t) << val << fround_end_if_needed(t);
            lead_char = ", ";
        }
        if (t.lanes() > 1) {
            rhs << "]";
        }
    }
    print_assignment(t, rhs.str());
}

void CodeGen_JavaScript::visit(const Add *op) {
    visit_binop(op->type, op->a, op->b, "+", "add", Float(64));
}

void CodeGen_JavaScript::visit(const Sub *op) {
    std::string simd_js_type;
    if (is_zero(op->a) && simd_js_type_for_type(op->type, simd_js_type)) {
        string arg = print_expr(op->b);
        print_assignment(op->type, simd_js_type + ".neg(" + arg + ")");
    } else {
        visit_binop(op->type, op->a, op->b, "-", "sub", Float(64));
    }
}

void CodeGen_JavaScript::visit(const Mul *op) {
    std::string simd_js_type;
    if (op->type.is_float() || simd_js_type_for_type(op->type, simd_js_type)) {
        visit_binop(op->type, op->a, op->b, "*", "mul", Float(64));
    } else {
        ostringstream rhs;
        const char *lead_char = (op->type.lanes() != 1) ? "[" : "";
        for (int lane = 0; lane < op->type.lanes(); lane++) {
            string a = print_expr(conditionally_extract_lane(op->a, lane));
            string b = print_expr(conditionally_extract_lane(op->b, lane));
            rhs << lead_char << make_js_int_cast("Math.imul(" + a + ", " + b + ")", Int(32), op->type.element_of());
            lead_char = ", ";
        }
        if (op->type.lanes() > 1) {
            rhs << "]";
        }
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_JavaScript::visit(const Div *op) {
    ostringstream rhs;
    std::string simd_js_type;
    if (simd_js_type_for_type(op->type, simd_js_type) &&
        op->type.is_float()) { // SIMD.js only supports vector divide on floating-point types.
        string a = print_expr(op->a);
        string b = print_expr(op->b);
        rhs << simd_js_type << ".div(" << a << ", " << b << ")";
    } else {
        const char *lead_char = "";
        rhs << literal_may_be_vector_start(op->type);
        for (int lane = 0; lane < op->type.lanes(); lane++) {
            int bits;
            if (is_const_power_of_two_integer(conditionally_extract_lane(op->b, lane), &bits)) {
                // JavaScript distinguishes signed vs. unsigned shift using >> vs >>>
                string shift_op = op->type.is_uint() ? " >>> " : " >> ";
                rhs << lead_char << print_expr(conditionally_extract_lane(op->a, lane)) << shift_op << bits;
            } else {
                string a = print_expr(conditionally_extract_lane(op->a, lane));
                string b = print_expr(conditionally_extract_lane(op->b, lane));
                rhs << lead_char << fround_start_if_needed(op->type);
                if (!op->type.is_float()) {
                    rhs << make_js_int_cast("Math.floor(" + a + " / " + b + ")", Float(64), op->type);
                } else {
                    rhs << a << " / " << b;
                }
                rhs << fround_end_if_needed(op->type);
            }
            lead_char = ", ";
        }
        rhs << literal_may_be_vector_end(op->type);
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Mod *op) {
    ostringstream rhs;
    // SIMD.js doesn't seem to have vectorized floor, even for floats,
    // so this is basically a no go for vectorization.
    rhs << literal_may_be_vector_start(op->type);
    const char *lead_char = "";
    for (int lane = 0; lane < op->type.lanes(); lane++) {
        int bits;
        if (is_const_power_of_two_integer(conditionally_extract_lane(op->b, lane), &bits)) {
          rhs << lead_char << fround_start_if_needed(op->type)
              << print_expr(conditionally_extract_lane(op->a, lane)) << " & " << ((1 << bits) - 1)
              << fround_end_if_needed(op->type);
        } else {
            string var_name(unique_name('_'));
            string a = print_expr(conditionally_extract_lane(op->a, lane));
            string b = print_expr(conditionally_extract_lane(op->b, lane));
            if (!op->type.is_float()) {
                do_indent();
                stream << "var " << var_name << " = Math.floor(" << a << " % " << b << ");\n";
                if (op->type.is_int()) {
                    do_indent();
                    stream << "if (" << var_name << " < 0) { " << var_name << " += (" << b << " < 0) ? -" << b << " : " << b << ";}\n";
                }
            } else {
                do_indent();
                stream << "var " << var_name << " = " << a << " - " << b << " * Math.floor(" << a << " / " << b << "); ";
            }
            rhs << lead_char << fround_start_if_needed(op->type) << var_name << fround_end_if_needed(op->type);
        }
        lead_char = ", ";
    }
    rhs << literal_may_be_vector_end(op->type);
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Max *op) {
    ostringstream rhs;
    std::string simd_js_type;
    if (simd_js_type_for_type(op->type, simd_js_type) && op->type.is_float()) {
        string a = print_expr(op->a);
        string b = print_expr(op->b);
        rhs << simd_js_type << ".max(" << a << ", " << b << ")";
     } else {
        call_scalar_function(rhs, op->type, "Math.max", false, {op->a, op->b});
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Min *op) {
    ostringstream rhs;
    std::string simd_js_type;
    if (simd_js_type_for_type(op->type, simd_js_type) && op->type.is_float()) {
        string a = print_expr(op->a);
        string b = print_expr(op->b);
        rhs << simd_js_type << ".min(" << a << ", " << b << ")";
    } else {
        call_scalar_function(rhs, op->type, "Math.min", false, {op->a, op->b});
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const EQ *op) {
    visit_binop(op->a.type(), op->a, op->b, "==", "equal");
}

void CodeGen_JavaScript::visit(const NE *op) {
    visit_binop(op->a.type(), op->a, op->b, "!=", "notEqual");
}

void CodeGen_JavaScript::visit(const LT *op) {
    visit_binop(op->a.type(), op->a, op->b, "<", "lessThan");
}

void CodeGen_JavaScript::visit(const LE *op) {
    visit_binop(op->a.type(), op->a, op->b, "<=", "lessThanOrEqual");
}

void CodeGen_JavaScript::visit(const GT *op) {
    visit_binop(op->a.type(), op->a, op->b, ">", "greaterThan");
}

void CodeGen_JavaScript::visit(const GE *op) {
    visit_binop(op->a.type(), op->a, op->b, ">=", "greaterThanOrEqual");
}

void CodeGen_JavaScript::visit(const Or *op) {
    // TODO(zalman): Is this correct?
    visit_binop(op->type, op->a, op->b, "||", "or");
}

void CodeGen_JavaScript::visit(const And *op) {
    // TODO(zalman): Is this correct?
    visit_binop(op->type, op->a, op->b, "&&", "and");
}

void CodeGen_JavaScript::visit(const Not *op) {
    print_assignment(op->type, "!(" + print_expr(op->a) + ")");
}

void CodeGen_JavaScript::visit(const IntImm *op) {
    ostringstream oss;
    oss << op->value;
    id = oss.str();
}

void CodeGen_JavaScript::visit(const UIntImm *op) {
    ostringstream oss;
    oss << op->value;
    id = oss.str();
}

void CodeGen_JavaScript::visit(const StringImm *op) {
    ostringstream oss;
    oss << Expr(op);
    id = oss.str();
}

void CodeGen_JavaScript::visit(const FloatImm *op) {
    if (std::isnan(op->value)) {
        id = "Number.NaN";
    } else if (std::isinf(op->value)) {
        if (op->value > 0) {
            id = "Number.POSITIVE_INFINITY";
        } else {
            id = "Number.NEGATIVE_INFINITY";
        }
    } else {
#if 0
        // TODO: Figure out if there is a way to write a floating-point hex literal in JS
        // Write the constant as reinterpreted uint to avoid any bits lost in conversion.
        union {
            uint32_t as_uint;
            float as_float;
        } u;
        u.as_float = op->value;


        ostringstream oss;
        oss << "float_from_bits(" << u.as_uint << " /* " << u.as_float << " */)";
        id = oss.str();
#else
        ostringstream oss;
        oss.precision(10);
        oss << op->value;
        id = oss.str();
#endif
    }
}

namespace {

std::map<string, string > js_math_values {
    { "inf_f32", "Number.INFINITY" },
    { "maxval_f32", "3.4028234663852885981e+38" },
    { "maxval_f64", "Number.MAX_VALUE" },
    { "minval_f32", "-3.4028234663852885981e+38" },
    { "minval_f64", "Number.MIN_VALUE" },
    { "nan_f32", "Number.NaN" },
    { "nan_f64", "Number.NaN" },
    { "neg_inf_f32", "Number.NEGATIVE_INFINITY" },
};

std::map<string, std::pair<string, int> > js_math_functions {
  {
    { "abs_f32", { "Math.abs", 1 } },
    { "abs_f64", { "Math.abs", 1 } },
    { "acos_f32", { "Math.acos", 1 } },
    { "acos_f64", { "Math.acos", 1 } },
    { "acosh_f32", { "Math.acosh", 1 } },
    { "acosh_f64", { "Math.acosh", 1 } },
    { "asin_f32", { "Math.asin", 1 } },
    { "asin_f64", { "Math.asin", 1 } },
    { "asinh_f32", { "Math.asinh", 1 } },
    { "asinh_f64", { "Math.asinh", 1 } },
    { "atan2_f32", { "Math.atan2", 2 } },
    { "atan2_f64", { "Math.atan2", 2 } },
    { "atan_f32", { "Math.atan", 1 } },
    { "atan_f64", { "Math.atan", 1 } },
    { "atanh_f32", { "Math.atanh", 1 } },
    { "atanh_f64", { "Math.atanh", 1 } },
    { "ceil_f32", { "Math.ceil", 1 } },
    { "ceil_f64", { "Math.ceil", 1 } },
    { "cos_f32", { "Math.cos", 1 } },
    { "cos_f64", { "Math.cos", 1 } },
    { "cosh_f32", { "Math.cosh", 1 } },
    { "cosh_f64", { "Math.cosh", 1 } },
    { "exp_f32", { "Math.exp", 1 } },
    { "exp_f64", { "Math.exp", 1 } },
    { "floor_f32", { "Math.floor", 1 } },
    { "floor_f64", { "Math.floor", 1 } },
    { "is_nan_f32", { "Number.isNaN", 1 } },
    { "is_nan_f64", { "Number.isNaN", 1 } },
    { "log_f32", { "Math.log", 1 } },
    { "log_f64", { "Math.log", 1 } },
    { "pow_f32", { "Math.pow", 2 } },
    { "pow_f64", { "Math.pow", 2 } },
    { "round_f32", { "halide_round", 1 } }, // TODO: Figure out if we want a Halide top-level object.
    { "round_f64", { "halide_round", 1 } },
    { "sin_f32", { "Math.sin", 1 } },
    { "sin_f64", { "Math.sin", 1 } },
    { "sinh_f32", { "Math.sinh", 1 } },
    { "sinh_f64", { "Math.sinh", 1 } },
    { "sqrt_f32", { "Math.sqrt", 1 } },
    { "sqrt_f64", { "Math.sqrt", 1 } },
    { "tan_f32", { "Math.tan", 1 } },
    { "tan_f64", { "Math.tan", 1 } },
    { "tanh_f32", { "Math.tanh", 1 } },
    { "tanh_f64", { "Math.tanh", 1 } },
    { "trunc_f32", { "Math.trunc", 1 } },
    { "trunc_f64", { "Math.trunc", 1 } },
  }
};

}  // namespace

void CodeGen_JavaScript::call_scalar_function(std::ostream &rhs, Type type, const string &name,
                                              bool is_operator, const std::vector<Expr> &arg_exprs) {
    std::string lead = literal_may_be_vector_start(type);

    for (int lane = 0; lane < type.lanes(); lane++) {
        vector<string> args(arg_exprs.size());
        for (size_t i = 0; i < arg_exprs.size(); i++) {
            // // Ugly: convert the
            // if (i == 3 && name == "_halide_buffer_init") {
            //     internal_assert(is_zero(arg_exprs[i]));
            //     args[i] = "null";
            //     continue;
            // }
            args[i] = print_expr(conditionally_extract_lane(arg_exprs[i], lane));
        }

        if (is_operator) {
            internal_assert(args.size() == 2);
            rhs << lead << fround_start_if_needed(type) << "(" <<
                args[0] << " " << name << " " << args[1] <<
                ")" << fround_end_if_needed(type);
        } else {
            rhs << lead << fround_start_if_needed(type) << name << "(";

            const char *separator = "";
            if (function_takes_user_context(name)) {
                rhs << (have_user_context ? "__user_context" : "null");
                separator = ", ";
            }

            for (size_t i = 0; i < args.size(); i++) {
                rhs << separator << args[i];
                separator = ", ";
            }
            rhs << ")" << fround_end_if_needed(type);
        }
        lead = ",";
    }

    rhs << literal_may_be_vector_end(type);
}

void CodeGen_JavaScript::visit(const Call *op) {
    // TODO: It is probably possible to add support for name mangling
    // here and make this go away for calling into native code.
    internal_assert(op->call_type != Call::ExternCPlusPlus) <<
        "C++ extern calls not allowed in JavaScript.\n";

    internal_assert(op->call_type == Call::Extern ||
                    op->call_type == Call::PureExtern ||
                    op->call_type == Call::Intrinsic ||
                    op->call_type == Call::PureIntrinsic)
        << "Can only codegen extern calls and intrinsics\n";

    ostringstream rhs;

    if (op->is_intrinsic(Call::debug_to_file)) {
        internal_assert(op->args.size() == 3);
        const StringImm *string_imm = op->args[0].as<StringImm>();
        internal_assert(string_imm);
        string filename = string_imm->value;
        string typecode = print_expr(op->args[1]);
        string buffer = print_name(print_expr(op->args[2]));

        rhs << "halide_debug_to_file(";
        rhs << (have_user_context ? "__user_context" : "null");
        rhs << ", \"" + filename + "\", " + typecode;
        rhs << ", " << buffer << ")";
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        internal_assert(op->args.size() == 2);
        visit_binop(op->type, op->args[0], op->args[1], "&", "and", Int(32));
        rhs << id;
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        internal_assert(op->args.size() == 2);
        visit_binop(op->type, op->args[0], op->args[1], "^", "xor", Int(32));
        rhs << id;
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        internal_assert(op->args.size() == 2);
        visit_binop(op->type, op->args[0], op->args[1], "|", "or", Int(32));
        rhs << id;
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        internal_assert(op->args.size() == 1);
        string a = print_expr(op->args[0]);
        string simd_js_type;
        if (simd_js_type_for_type(op->type, simd_js_type)) {
            rhs << simd_js_type << ".not(" << a << ")";
        } else {
            rhs << make_js_int_cast("~" + a, Int(32), op->type);
        }
    } else if (op->is_intrinsic(Call::reinterpret)) {
        internal_assert(op->args.size() == 1);
        rhs << print_reinterpret(op->type, op->args[0]);
    } else if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        string simd_js_type;
        // SIMD.js only supports shifts by a scalar.
        if (simd_js_type_for_type(op->type, simd_js_type)) {
            const Broadcast *broadcast = op->args[1].as<Broadcast>();
            if (broadcast != nullptr) {
                string a0 = print_expr(op->args[0]);
                string shift_amount = print_expr(broadcast->value);

                rhs << simd_js_type << ".leftShiftByScalar(" << a0 << ", " << shift_amount << ")";
            } else {
                call_scalar_function(rhs, op->type, "<<", true, op->args);
            }
        } else {
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " << " << a1;
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        const char *shift_op = op->type.is_uint() ? " >>> " : " >> ";
        string simd_js_type;
        // SIMD.js only supports shifts by a scalar.
        if (simd_js_type_for_type(op->type, simd_js_type)) {
            const Broadcast *broadcast = op->args[1].as<Broadcast>();
            if (broadcast != nullptr) {
                string a0 = print_expr(op->args[0]);
                string shift_amount = print_expr(broadcast->value);

                rhs << simd_js_type << ".rightShiftByScalar(" << a0 << ", " << shift_amount << ")";
            } else {
                call_scalar_function(rhs, op->type, shift_op, true, op->args);
            }
        } else {
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            // JavaScript distinguishes signed vs. unsigned shift using >> vs >>>
            rhs << a0 << shift_op << a1;
        }
    } else if (op->is_intrinsic(Call::trace)) {
        // TODO(srj): is this correct?
        int int_args = (int)(op->args.size()) - 5;
        internal_assert(int_args >= 0);

        Type type = op->type;

        ostringstream value_stream;
        const char *lead_char = "[";
        for (int32_t v_index = 0; v_index < type.lanes(); v_index++) {
            value_stream << lead_char << print_expr(conditionally_extract_lane(op->args[4], v_index));
            lead_char = ", ";
        }
        value_stream << "]";

        ostringstream coordinates_stream;
        coordinates_stream << "[";
        for (int32_t c_index = 0; c_index < int_args; c_index++) {
            if (c_index != 0) {
              coordinates_stream << ", ";
            }
            coordinates_stream << print_expr(op->args[5 + c_index]);
        }
        coordinates_stream << "]";

        string event_name = unique_name('_');
        do_indent();
        std::vector<string> str_args(op->args.size());
        for (size_t i = 0; i < str_args.size(); i++) {
            str_args[i] = print_expr(op->args[i]);
        }

        stream << "var " << event_name << " = { func: " << str_args[0] << ", ";
        stream << "event: " << str_args[1] << ", ";
        stream << "parent_id: " << str_args[2] << ", ";
        stream << "type_code: " << type.code() << ", bits: " << type.bits() << ", vector_width: " << type.lanes() << ", ";
        stream << "value_index: " << str_args[3] << ", ";
        stream << "value: " << value_stream.str() << ", ";
        stream << "dimensions: " << int_args * type.lanes() << ", ";
        stream << "coordinates: " << coordinates_stream.str() << " }\n";
        rhs << "halide_trace(__user_context, " << event_name << ")";
    } else if (op->is_intrinsic(Call::lerp)) {
        // JavaScript doesn't support 64-bit ints, which are used for 32-bit interger lerps.
        // Handle this by converting to double instead, which will be as efficient in JS unless
        // SIMD.js or asm.js are being used.
        Expr e;
        if (!op->type.is_float() && op->type.bits() >= 32) {
            e = Cast::make(op->type, round(lower_lerp(Cast::make(Float(64), op->args[0]),
                                                      Cast::make(Float(64), op->args[1]), op->args[2])));
        } else {
            e = lower_lerp(op->args[0], op->args[1], op->args[2]);
        }
        rhs << print_expr(e);
    } else if (op->is_intrinsic(Call::popcount)) {
        Expr e = cast<uint32_t>(op->args[0]);
        e = e - ((e >> 1) & 0x55555555);
        e = (e & 0x33333333) + ((e >> 2) & 0x33333333);
        e = (e & 0x0f0f0f0f) + ((e >> 4) & 0x0f0f0f0f);
        e = (e * 0x1010101) >> 24;
        rhs << print_expr(e);
    } else if (op->is_intrinsic(Call::count_leading_zeros)) {
        string e = print_expr(op->args[0]);
        int bits = op->args[0].type().bits();
        internal_assert(bits <= 32);
        rhs << "(Math.clz32(" << e << ") - " << (32 - bits) << ")";
    } else if (op->is_intrinsic(Call::count_trailing_zeros)) {
        Expr e = op->args[0];
        int32_t bits = op->args[0].type().bits();

        e = e & -e;
        Expr ctz = bits;
        if (bits > 16) {
            ctz = ctz - select((e & 0x0000ffff) != 0, 16, 0);
            ctz = ctz - select((e & 0x00ff00ff) != 0, 8, 0);
            ctz = ctz - select((e & 0x0f0f0f0f) != 0, 4, 0);
            ctz = ctz - select((e & 0x33333333) != 0, 2, 0);
            ctz = ctz - select((e & 0x55555555) != 0, 1, 0);
            ctz = ctz - select(e != 0, 1, 0);
        } else if (bits > 8) {
            ctz = ctz - select((e & 0x00ff) != 0, 8, 0);
            ctz = ctz - select((e & 0x0f0f) != 0, 4, 0);
            ctz = ctz - select((e & 0x3333) != 0, 2, 0);
            ctz = ctz - select((e & 0x5555) != 0, 1, 0);
            ctz = ctz - select(e != 0, 1, 0);
        } else if (bits > 1) {
            ctz = ctz - select((e & 0x0f) != 0, 4, 0);
            ctz = ctz - select((e & 0x33) != 0, 2, 0);
            ctz = ctz - select((e & 0x55) != 0, 1, 0);
            ctz = ctz - select(e != 0, 1, 0);
        } else {
            ctz = ctz - select(e, 1, 0);
        }
        rhs << print_expr(ctz);
    } else if (op->is_intrinsic(Call::return_second)) {
        internal_assert(op->args.size() == 2);
        string arg0 = print_expr(op->args[0]);
        string arg1 = print_expr(op->args[1]);
        rhs << "(" << arg0 << ", " << arg1 << ")";
    } else if (op->is_intrinsic(Call::if_then_else)) {
        internal_assert(op->args.size() == 3);

        string result_id = unique_name('_');

        do_indent();
        stream << "var " << result_id << ";\n";

        string cond_id = print_expr(op->args[0]);

        do_indent();
        stream << "if (" << cond_id << ")\n";
        open_scope();
        string true_case = print_expr(op->args[1]);
        do_indent();
        stream << result_id << " = " << true_case << ";\n";
        close_scope("if " + cond_id);
        do_indent();
        stream << "else\n";
        open_scope();
        string false_case = print_expr(op->args[2]);
        do_indent();
        stream << result_id << " = " << false_case << ";\n";
        close_scope("if " + cond_id + " else");

        rhs << result_id;
    } else if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);
        string simd_js_type_arg;
        string simd_js_type_result;
        if (simd_js_type_for_type(op->args[0].type(), simd_js_type_arg) &&
            simd_js_type_for_type(op->type, simd_js_type_result)) {
            string arg = print_expr(op->args[0]);
            // SIMD.js doesn't support "abs" on integer types.
            if (op->type.is_float()) {
                rhs << simd_js_type_arg << ".abs(" << arg << ")";
            } else {
                Expr abs_expr =
                    reinterpret(op->type, select(op->args[0] < 0, 0 - op->args[0], op->args[0]));
                rhs << print_expr(abs_expr);
            }
        } else {
            call_scalar_function(rhs, op->type, "Math.abs", false, op->args);
        }
    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);

        Expr absd_expr =
            reinterpret(op->type, select(op->args[0] < op->args[1], op->args[1] - op->args[0], op->args[0] - op->args[1]));
        rhs << print_expr(absd_expr);
    } else if (op->is_intrinsic(Call::memoize_expr)) {
        internal_assert(op->args.size() >= 1);
        string arg = print_expr(op->args[0]);
        rhs << "(" << arg << ")";
    } else if (op->is_intrinsic(Call::make_struct)) {
        if (op->args.empty()) {
            rhs << "null";
        } else {
            print_expr_array(op->args);
            return;
        }
    } else if (op->is_intrinsic(Call::stringify)) {
        string buf_name = unique_name('_');

        // Print all args that are general Exprs before starting output on stream.
        std::vector<string> printed_args(op->args.size());
        for (size_t i = 0; i < op->args.size(); i++) {
          if (op->args[i].type().is_float()) {
              do_indent();
              string temp = unique_name('_');
              string e = print_expr(op->args[i]);
              string format_function = (op->args[i].type().bits() == 32) ? "toFixed" : "toScientific";
              stream << "var " << temp << " = (" << e << ")." << format_function << "(6);\n";
              printed_args[i] = temp;
            } else if (op->args[i].as<StringImm>() == NULL && !op->args[i].type().is_handle()) {
                printed_args[i] = print_expr(op->args[i]);
            }
        }
        do_indent();
        stream << "var " << buf_name << " = \"\";\n";
        for (size_t i = 0; i < op->args.size(); i++) {
            Type t = op->args[i].type();

            do_indent();

            if (op->args[i].type().is_float()) {
                stream << buf_name << " = " << buf_name << ".concat(" << printed_args[i] << ");\n";
            } else if (op->args[i].as<StringImm>()) {
                stream << buf_name << " = " << buf_name << ".concat(" << op->args[i] << ");\n";
            } else if (t.is_handle()) {
                stream << buf_name << " = " << buf_name << ".concat(\"<Object>\");\n";
            } else {
                stream << buf_name << " = " << buf_name << ".concat((" << printed_args[i] << ").toString());\n";
            }
        }
        rhs << buf_name;
    } else if (op->is_intrinsic(Call::quiet_div)) {
        internal_assert(op->args.size() == 2);
        // Don't bother checking for zero denominator here; the quiet_div
        // implementation will always do a runtime check and return zero
        // (rather than failing at runtime).
        string a = print_expr(op->args[0]);
        string b = print_expr(op->args[1]);
        rhs << "halide_quiet_div(" << a << ", " << b << ")";
    } else if (op->is_intrinsic(Call::quiet_mod)) {
        internal_assert(op->args.size() == 2);
        // Don't bother checking for zero denominator here; the quiet_mod
        // implementation will always do a runtime check and return zero
        // (rather than failing at runtime).
        string a = print_expr(op->args[0]);
        string b = print_expr(op->args[1]);
        rhs << "halide_quiet_mod(" << a << ", " << b << ")";
    } else if (op->is_intrinsic(Call::alloca)) {
        internal_assert(op->args.size() == 1);
        internal_assert(op->type.is_handle());
        const Call *call = op->args[0].as<Call>();
        if (op->type == type_of<struct halide_buffer_t *>() &&
            call && call->is_intrinsic(Call::size_of_halide_buffer_t)) {
            rhs << "_halide_buffer_create()";
        } else {
            string alloc_size = print_expr(simplify(op->args[0]));
            rhs << "new Uint8Array(" << alloc_size << ")";
        }

        // don't fall thru and call print_assignment: it could re-use
        // a cached, value, which is never appopriate for alloca calls.
        id = unique_name('_');
        do_indent();
        stream << "var " << id << " = " << rhs.str() << ";\n";
        return;

    } else if (op->is_intrinsic(Call::size_of_halide_buffer_t)) {
        internal_assert(op->args.size() == 0);
        rhs << sizeof(halide_buffer_t);
    } else if (op->call_type == Call::Intrinsic ||
               op->call_type == Call::PureIntrinsic) {
        // TODO: other intrinsics
        internal_error << "Unhandled intrinsic in JavaScript backend: " << op->name << '\n';
    } else {
        // Generic calls

        auto js_math_value = js_math_values.find(op->name);
        if (js_math_value != js_math_values.end()) {
            rhs << fround_start_if_needed(op->type)
                << js_math_value->second
                << fround_end_if_needed(op->type);
        } else {
            // Map math functions to JS names.
            string js_name = op->name;
            auto js_math_fn = js_math_functions.find(op->name);
            if (js_math_fn != js_math_functions.end()) {
                js_name = js_math_fn->second.first;
            }

            call_scalar_function(rhs, op->type, js_name, false, op->args);
        }
    }

    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::lane_by_lane_load(std::ostringstream &rhs, const Load *op,
    const std::string &typed_name, const std::string &open, const std::string &close, bool type_cast_needed) {
    Type t = op->type;
    std::vector<string> indices;

    for (int32_t i = 0; i < t.lanes(); i++) {
        indices.push_back(print_expr(extract_lane(op->index, i)));
    }
    rhs << open;
    for (int32_t i = 0; i < t.lanes(); i++) {
        std::string possibly_casted = typed_name;
        if (type_cast_needed) {
            possibly_casted = buffer_host_as_typed_array(t, typed_name);
        }
        if (i != 0) {
            rhs << ", ";
        }
        rhs << possibly_casted << "[" << indices[i] << "]";
    }
    rhs << close;
}

void CodeGen_JavaScript::visit(const Load *op) {
    Type t = op->type;

    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name).type.element_of() == t.element_of());
// stream<<"//name:"<<op->name<<"\n";
// stream<<"//type:"<<t<<"\n";
// stream<<"//type_cast_needed:"<<type_cast_needed<<"\n";

    std::string typed_name = print_name(op->name);

    ostringstream rhs;
    if (t.is_scalar()) {
        std::string index_expr = print_expr(op->index);
        std::string temp = typed_name;
        if (type_cast_needed) {
            temp = buffer_host_as_typed_array(t, typed_name);
        }
        rhs << temp << "[" << index_expr << "]";
    } else {
        std::string simd_js_type;
        if (simd_js_type_for_type(op->type, simd_js_type)) {
            const Ramp *ramp = op->index.as<Ramp>();

            if (ramp && is_one(ramp->stride)) {
                string base = print_expr(ramp->base);
                rhs << simd_js_type << ".load(" << print_name(op->name) << ", " << base << ")";
            } else {
                lane_by_lane_load(rhs, op, typed_name, simd_js_type + "(", ")", type_cast_needed);
            }
        } else {
            // TODO: Handle dense ramps. See if above code for SIMD.js can give an advantage for
            // non-SIMD.js case.
            lane_by_lane_load(rhs, op, typed_name, "[", "]", type_cast_needed);
        }
    }

    print_assignment(t, rhs.str());
}

void CodeGen_JavaScript::visit(const Ramp *op) {
    ostringstream rhs;
    string base = print_expr(op->base);
    string stride = print_expr(op->stride);

    rhs << literal_may_be_vector_start(op->type);
    for (int32_t i = 0; i < op->lanes; i++) {
        if (i != 0) {
            rhs << ", ";
        }
        rhs << base << " + " << stride << " * " << i;
    }
    rhs << literal_may_be_vector_end(op->type);

    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Broadcast *op) {
    ostringstream rhs;
    string value = print_expr(op->value);
    std::string simd_js_type;
    if (simd_js_type_for_type(op->type, simd_js_type)) {
        rhs << simd_js_type << ".splat(" << value << ")";
    } else  {
        const char *lead_char = (op->type.lanes() != 1) ? "[" : "";
        for (int32_t i = 0; i < op->lanes; i++) {
            rhs << lead_char << value;
            lead_char = ", ";
        }
        if (op->type.lanes() != 1) {
            rhs << "]";
        }
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::lane_by_lane_store(const Store *op, const std::string &typed_name, bool type_cast_needed) {
    Type t = op->value.type();
    std::vector<string> indices;
    std::vector<string> values;

    for (int32_t i = 0; i < t.lanes(); i++) {
        indices.push_back(print_expr(extract_lane(op->index, i)));
        values.push_back(print_expr(extract_lane(op->value, i)));
    }
    for (int32_t i = 0; i < t.lanes(); i++) {
        std::string temp = typed_name;
        if (type_cast_needed) {
            temp = buffer_host_as_typed_array(t, typed_name);
        }
        do_indent();
        stream << temp << "[" << indices[i] << "] = " << values[i] << ";\n";
    }
}

void CodeGen_JavaScript::visit(const Store *op) {
    Type t = op->value.type();

    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name).type.element_of() == t.element_of());
stream<<"//store:"<<op->name<<"\n";
stream<<"//type:"<<t<<"\n";
stream<<"//type_cast_needed:"<<type_cast_needed<<"\n";
    std::string typed_name = print_name(op->name);

    if (op->value.type().is_scalar()) {
        string id_index = print_expr(op->index);
        string id_value = print_expr(op->value);
        string lhs = typed_name;
        if (type_cast_needed) {
            lhs = buffer_host_as_typed_array(t, typed_name);
        }
        do_indent();
        stream << lhs << "[" << id_index << "] = " << id_value << ";\n";
    } else {
        string simd_js_type;
        if (simd_js_type_for_type(t, simd_js_type)) {
            const Ramp *ramp = op->index.as<Ramp>();
            if (ramp && is_one(ramp->stride)) {
                string base = print_expr(ramp->base);
                string value = print_expr(op->value);
                do_indent();
                stream << simd_js_type << ".store(" << print_name(op->name) << ", " << base << ", " << value << ");";
            } else {
                lane_by_lane_store(op, typed_name, type_cast_needed);
            }
        } else {
            // TODO: Handle dense ramps. See if above code for SIMD.js can give an advantage for
            // non-SIMD.js case.
            lane_by_lane_store(op, typed_name, type_cast_needed);
        }
    }
    clear_cache();
}

void CodeGen_JavaScript::visit(const Let *op) {
    string id_value = print_expr(op->value);
    Expr new_var = Variable::make(op->value.type(), id_value);
    Expr body = substitute(op->name, new_var, op->body);
    print_expr(body);
}

void CodeGen_JavaScript::visit(const Select *op) {
    ostringstream rhs;
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);

    std::string simd_js_type;
    if (simd_js_type_for_type(op->type, simd_js_type)) {
        rhs << simd_js_type << ".select(";
        if (op->condition.type().is_scalar()) {
            string simd_js_bool_type;
            bool has_bool_type = simd_js_type_for_type(Bool(op->true_value.type().lanes()), simd_js_bool_type);
            internal_assert(has_bool_type) << "SIMD.js does not have a boolean type corresponding to " << op->true_value.type() << "\n";
            rhs << simd_js_bool_type << ".splat(" << cond << ")";
        } else {
            rhs << cond;
        }
        rhs << ", " << true_val << ", " << false_val << ")";
    } else {
        rhs << "(" << cond
            << " ? " << true_val
            << " : " << false_val
            << ")";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const LetStmt *op) {
    string id_value = print_expr(op->value);
    Expr new_var = Variable::make(op->value.type(), id_value);
    Stmt body = substitute(op->name, new_var, op->body);
    body.accept(this);
}

void CodeGen_JavaScript::visit(const AssertStmt *op) {
    string id_cond = print_expr(op->condition);

    do_indent();
    stream << "if (!" << id_cond << ")\n";
    open_scope();
    string id_msg = print_expr(op->message);
    do_indent();
    stream << "return " << id_msg << ";\n";
    close_scope("");
}

void CodeGen_JavaScript::visit(const ProducerConsumer *op) {
    do_indent();
    stream << "// produce " << op->name << '\n';
    print_stmt(op->body);
}

void CodeGen_JavaScript::visit(const For *op) {
    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    do_indent();
    stream << "for (var "
           << print_name(op->name)
           << " = " << id_min
           << "; "
           << print_name(op->name)
           << " < " << id_min
           << " + " << id_extent
           << "; "
           << print_name(op->name)
           << "++)\n";

    open_scope();
    op->body.accept(this);
    close_scope("for " + print_name(op->name));

}

void CodeGen_JavaScript::visit(const Provide *op) {
    internal_error << "Cannot emit Provide statements as JavaScript\n";
}

void CodeGen_JavaScript::visit(const Allocate *op) {
    open_scope(); // TODO: this probably doesn't work right as JavaScript has ridiculous scoping. Either use Let or explicitly deallocate...

    string op_name = print_name(op->name);

    allocations.push(op->name, {op->type, op->free_function});

    internal_assert(op->type.is_float() || op->type.is_int() || op->type.is_uint()) << "Cannot allocate non-numeric type in JavaScript codegen.\n";

    if (op->new_expr.defined()) {
        std::string alloc_expr = print_expr(op->new_expr);
        do_indent();
        stream << "var " << op_name << " = (" << alloc_expr << ");\n";
    } else {
        string typed_array_name = javascript_type_array_name_fragment(op->type) + string("Array");
        std::string allocation_size;
        int32_t constant_size = op->constant_allocation_size();
        // This both potentially does strength reduction at compile time, but also handles the zero extents case.
        if (constant_size > 0) {
            allocation_size = print_expr(static_cast<int32_t>(constant_size));
        } else {
            // TODO: Verify overflow is not a concern.
            allocation_size = print_expr(op->extents[0]);
            for (size_t i = 1; i < op->extents.size(); i++) {
                allocation_size = print_assignment(Float(64), allocation_size + " * " + print_expr(op->extents[i]));
            }
            if (op->type.lanes() > 1) {
                allocation_size = print_assignment(Float(64), allocation_size + " * " + print_expr(op->type.lanes()));
            }
        }

        do_indent();
        stream << "var " << op_name << " = new " << typed_array_name << "(" << allocation_size << ");\n";
    }

    op->body.accept(this);

    close_scope("alloc " + print_name(op->name));
}

void CodeGen_JavaScript::visit(const Free *op) {
    string free_function = allocations.get(op->name).free_function;
    if (free_function.empty()) {
        do_indent();
        stream << print_name(op->name) << " = null;\n";
    } else {
        do_indent();
        stream << free_function << "("
               << (have_user_context ? "__user_context, " : "null, ")
               << print_name(op->name)
               << ");\n";
    }
    allocations.pop(op->name);
}

void CodeGen_JavaScript::visit(const Realize *op) {
    internal_error << "Cannot emit realize statements to JavaScript\n";
}

void CodeGen_JavaScript::visit(const IfThenElse *op) {
    string cond_id = print_expr(op->condition);

    do_indent();
    stream << "if (" << cond_id << ")\n";
    open_scope();
    op->then_case.accept(this);
    close_scope("if " + cond_id);

    if (op->else_case.defined()) {
        do_indent();
        stream << "else\n";
        open_scope();
        op->else_case.accept(this);
        close_scope("if " + cond_id + " else");
    }
}

void CodeGen_JavaScript::visit(const Evaluate *op) {
    string id = print_expr(op->value);
    if (id == "0") {
        // Skip evaluate(0) nodes. They're how we represent no-ops.
        return;
    }
    do_indent();
}

void CodeGen_JavaScript::visit(const Shuffle *op) {
    internal_assert(op->vectors.size() >= 1);
    internal_assert(op->vectors[0].type().is_vector());
    for (size_t i = 1; i < op->vectors.size(); i++) {
        internal_assert(op->vectors[0].type() == op->vectors[i].type());
    }
    internal_assert(op->type.lanes() == (int) op->indices.size());
    const int max_index = (int) (op->vectors[0].type().lanes() * op->vectors.size());
    for (int i : op->indices) {
        internal_assert(i >= -1 && i < max_index);
    }

    std::vector<string> vecs;
    for (Expr v : op->vectors) {
        vecs.push_back(print_expr(v));
    }
    string src = vecs[0];
    if (op->vectors.size() > 1) {
        ostringstream rhs;
        rhs << "halide_concat_vectors([" << with_commas(vecs) << "])";
        src = print_assignment(Type(), rhs.str());
    }
    ostringstream rhs;
    if (op->type.is_scalar()) {
        rhs << src << "[" << op->indices[0] << "]";
    } else {
        string indices = print_assignment(op->type, "[" + with_commas(op->indices) + "]");
        rhs << "halide_shuffle_vector(" << src << ", " << indices << ")";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::test() {
#if 0
    Argument buffer_arg("buf", true, Int(32));
    Argument float_arg("alpha", false, Float(32));
    Argument int_arg("beta", false, Int(32));
    Argument user_context_arg("__user_context", false, Handle());
    vector<Argument> args(4);
    args[0] = buffer_arg;
    args[1] = float_arg;
    args[2] = int_arg;
    args[3] = user_context_arg;
    Var x("x");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = Select::make(alpha > 4.0f, print_when(x < 1, 3), 2);
    Stmt s = Store::make("buf", e, x);
    s = LetStmt::make("x", beta+1, s);
    s = Block::make(s, Free::make("tmp.stack"));
    s = Allocate::make("tmp.stack", Int(32), vec(Expr(127)), const_true(), s);
    s = Block::make(s, Free::make("tmp.heap"));
    s = Allocate::make("tmp.heap", Int(32), vec(Expr(43), Expr(beta)), const_true(), s);

    ostringstream source;
    CodeGen_JavaScript cg(source);
    cg.compile(s, "test1", args, vector<Buffer<>>());

    string src = source.str();
    string correct_source = preamble +
        "\n\n"
        "extern \"C\" int test1(buffer_t *_buf_buffer, const float _alpha, const int32_t _beta, const void * __user_context) {\n"
        "int32_t *_buf = (int32_t *)(_buf_buffer->host);\n"
        "const bool _buf_host_and_dev_are_null = (_buf_buffer->host == null) && (_buf_buffer->dev == 0);\n"
        "(void)_buf_host_and_dev_are_null;\n"
        "const int32_t _buf_min_0 = _buf_buffer->min[0];\n"
        "(void)_buf_min_0;\n"
        "const int32_t _buf_min_1 = _buf_buffer->min[1];\n"
        "(void)_buf_min_1;\n"
        "const int32_t _buf_min_2 = _buf_buffer->min[2];\n"
        "(void)_buf_min_2;\n"
        "const int32_t _buf_min_3 = _buf_buffer->min[3];\n"
        "(void)_buf_min_3;\n"
        "const int32_t _buf_extent_0 = _buf_buffer->extent[0];\n"
        "(void)_buf_extent_0;\n"
        "const int32_t _buf_extent_1 = _buf_buffer->extent[1];\n"
        "(void)_buf_extent_1;\n"
        "const int32_t _buf_extent_2 = _buf_buffer->extent[2];\n"
        "(void)_buf_extent_2;\n"
        "const int32_t _buf_extent_3 = _buf_buffer->extent[3];\n"
        "(void)_buf_extent_3;\n"
        "const int32_t _buf_stride_0 = _buf_buffer->stride[0];\n"
        "(void)_buf_stride_0;\n"
        "const int32_t _buf_stride_1 = _buf_buffer->stride[1];\n"
        "(void)_buf_stride_1;\n"
        "const int32_t _buf_stride_2 = _buf_buffer->stride[2];\n"
        "(void)_buf_stride_2;\n"
        "const int32_t _buf_stride_3 = _buf_buffer->stride[3];\n"
        "(void)_buf_stride_3;\n"
        "const int32_t _buf_elem_size = _buf_buffer->elem_size;\n"
        "{\n"
        " int64_t _0 = 43;\n"
        " int64_t _1 = _0 * _beta;\n"
        " if ((_1 > ((int64_t(1) << 31) - 1)) || ((_1 * sizeof(int32_t)) > ((int64_t(1) << 31) - 1)))\n"
        " {\n"
        "  halide_error(__user_context, \"32-bit signed overflow computing size of allocation tmp.heap\\n\");\n"
        "  return -1;\n"
        " } // overflow test tmp.heap\n"
        " int64_t _2 = _1;\n"
        " int32_t *_tmp_heap = (int32_t *)halide_malloc(__user_context, sizeof(int32_t)*_2);\n"
        " {\n"
        "  int32_t _tmp_stack[127];\n"
        "  int32_t _3 = _beta + 1;\n"
        "  int32_t _4;\n"
        "  bool _5 = _3 < 1;\n"
        "  if (_5)\n"
        "  {\n"
        "   char b0[1024];\n"
        "   snprintf(b0, 1024, \"%lld%s\", (long long)(3), \"\\n\");\n"
        "   void * _6 = b0;\n"
        "   int32_t _7 = halide_print(__user_context, _6);\n"
        "   int32_t _8 = (_7, 3);\n"
        "   _4 = _8;\n"
        "  } // if _5\n"
        "  else\n"
        "  {\n"
        "   _4 = 3;\n"
        "  } // if _5 else\n"
        "  int32_t _9 = _4;\n"
        "  bool _10 = _alpha > float_from_bits(1082130432 /* 4 */);\n"
        "  int32_t _11 = (int32_t)(_10 ? _9 : 2);\n"
        "  _buf[_3] = _11;\n"
        " } // alloc _tmp_stack\n"
        " halide_free(__user_context, _tmp_heap);\n"
        "} // alloc _tmp_heap\n"
        "return 0;\n"
        "}\n";
    if (src != correct_source) {
        int diff = 0;
        while (src[diff] == correct_source[diff]) diff++;
        int diff_end = diff + 1;
        while (diff > 0 && src[diff] != '\n') diff--;
        while (diff_end < (int)src.size() && src[diff_end] != '\n') diff_end++;

        internal_error
            << "Correct source code:\n" << correct_source
            << "Actual source code:\n" << src
            << "\nDifference starts at: " << src.substr(diff, diff_end - diff) << "\n";

    }


    std::cout << "CodeGen_JavaScript test passed\n";
#endif
}

}
}
