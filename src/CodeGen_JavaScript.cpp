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
const string preamble =
    "var halide_error_code_success = 0;\n"
    "var halide_error_code_generic_error = -1;\n"
    "var halide_error_code_explicit_bounds_too_small = -2;\n"
    "var halide_error_code_bad_elem_size = -3;\n"
    "var halide_error_code_access_out_of_bounds = -4;\n"
    "var halide_error_code_buffer_allocation_too_large = -5;\n"
    "var halide_error_code_buffer_extents_too_large = -6;\n"
    "var halide_error_code_constraints_make_required_region_smaller = -7;\n"
    "var halide_error_code_constraint_violated = -8;\n"
    "var halide_error_code_param_too_small = -9;\n"
    "var halide_error_code_param_too_large = -10;\n"
    "var halide_error_code_out_of_memory = -11;\n"
    "var halide_error_code_buffer_argument_is_null = -12;\n"
    "var halide_error_code_debug_to_file_failed = -13;\n"
    "var halide_error_code_copy_to_host_failed = -14;\n"
    "var halide_error_code_copy_to_device_failed = -15;\n"
    "var halide_error_code_device_malloc_failed = -16;\n"
    "var halide_error_code_device_sync_failed = -17;\n"
    "var halide_error_code_device_free_failed = -18;\n"
    "var halide_error_code_no_device_interface = -19;\n"
    "var halide_error_code_matlab_init_failed = -20;\n"
    "var halide_error_code_matlab_bad_param_type = -21;\n"
    "var halide_error_code_internal_error = -22;\n"
    "if (typeof(halide_print) != \"function\") { halide_print = function (user_context, msg) { Console.log(msg); } }\n"
    "if (typeof(halide_error) != \"function\") { halide_error = function (user_context, msg) { halide_print(user_context, msg); } }\n"
    "if (typeof(halide_trace) != \"function\") { var id = 0; halide_trace = function (user_context, event) { return id++; } }\n"
    "if (typeof(halide_shutdown_trace) != \"function\") { halide_shutdown_trace = function () { return 0; } }\n"
    "if (typeof(halide_debug_to_file) != \"function\") { halide_debug_to_file = function (user_context, filename, data, s0, s1, s2, s3, type_code, bytes_per_element) { halide_print(user_context, \"halide_debug_to_file called. Implementation needed.\\n\"); return 0; } }\n"
    "if (typeof(fast_inverse_f32) != \"function\") { fast_inverse_f32 = function(x) { return 1 / x; } }\n"
    "if (typeof(fast_inverse_sqrt_f32) != \"function\") { fast_inverse_f32 = function(x) { return 1 / Math.sqrt(x); } }\n"
    "if (typeof(halide_error_bounds_inference_call_failed) !=\"function\") { halide_error_bounds_inference_call_failed = \n"
    "    function(user_context, extern_stage_name, result) {\n"
    "        halide_error(user_context, \"Bounds inference call to external stage \" + extern_stage_name + \" returned non-zero value: \" + result);\n"
    "        return result; } }\n"
    "if (typeof(halide_error_extern_stage_failed) !=\"function\") { halide_error_extern_stage_failed = \n"
    "    function(user_context, func_name, var_name) {\n"
    "        halide_error(user_context, \"Call to external stage \" + extern_stage_name + \" returned non-zero value: \" + result);\n"
    "        return result; } }\n"
    "if (typeof(halide_error_explicit_bounds_too_small) != \"function\") {  halide_error_explicit_bounds_too_small = \n"
    "    function(user_context, func_name, var_name, min_bound, max_bound, min_required, max_required) {\n"
    "         halide_error(user_context, \"Bounds given for \" + var_name + \" in \" + func_name + \" (from \" + min_bound + \" to \" + max_bound + \") do not cover required region (from \" + min_required + \" to \" + max_required + \")\");\n"
    "         return halide_error_code_explicit_bounds_too_small; } }\n"
    "if (typeof(halide_error_bad_elem_size) != \"function\") {  halide_error_bad_elem_size = \n"
    "    function(user_context, func_name, type_name, elem_size_given, correct_elem_size) {\n"
    "     halide_error(user_context, func_name + \" has type \" + type_name + \" but elem_size of the buffer passed in is \" + elem_size_given + \" instead of \" + correct_elem_size);\n"
    "    return halide_error_code_bad_elem_size; } }\n"
    "if (typeof(halide_error_access_out_of_bounds) != \"function\") {  halide_error_access_out_of_bounds = \n"
    "    function(user_context, func_name, dimension, min_touched, max_touched, min_valid, max_valid) {\n"
    "        if (min_touched < min_valid) {\n"
    "            halide_error(user_context, func_name + \" is accessed at \" + min_touched + \", which is before the min (\" + min_valid + \") in dimension \" + dimension);\n"
    "        } else if (max_touched > max_valid) {\n"
    "            halide_error(user_context, func_name + \" is accessed at \" + max_touched + \", which is beyond the max (\" + max_valid + \") in dimension \" + dimension);\n"
    "        }\n"
    "        return halide_error_code_access_out_of_bounds; } }\n"
    "if (typeof(halide_error_buffer_allocation_too_large) != \"function\") {  halide_error_buffer_allocation_too_large = \n"
    "    function(user_context, buffer_name, allocation_size, max_size) {\n"
    "    halide_error(user_context, \"Total allocation for buffer \" + buffer_name + \" is \" + allocation_size + \", which exceeds the maximum size of \" + max_size);\n"
    "    return halide_error_code_buffer_allocation_too_large; } }\n"
    "if (typeof(halide_error_buffer_extents_too_large) != \"function\") {  halide_error_buffer_extents_too_large = \n"
    "    function(user_context, buffer_name, actual_size, max_size) {\n"
    "        halide_error(user_context, + \"Product of extents for buffer \" + buffer_name + \" is \" + actual_size + \", which exceeds the maximum size of \" + max_size);\n"
    "        return halide_error_code_buffer_extents_too_large; } }\n"
    "if (typeof(halide_error_constraints_make_required_region_smaller) != \"function\") {  halide_error_constraints_make_required_region_smaller = \n"
    "    function(user_context, buffer_name, dimension, constrained_min, constrained_extent, required_min, required_extent) {\n"
    "        var required_max = required_min + required_extent - 1;\n"
    "        var constrained_max = constrained_min + required_extent - 1;\n"
    "        halide_error(user_context, \"Applying the constraints on \" + buffer_name + \" to the required region made it smaller. Required size: \" + required_min + \" to \" + required_max + \". Constrained size: \" + constrained_min + \" to \" + constrained_max + \".\");\n"
    "        return halide_error_code_constraints_make_required_region_smaller; } }\n"
    "if (typeof(halide_error_constraint_violated) != \"function\") {  halide_error_constraint_violated = \n"
    "    function(user_context, var_name, value, constrained_var, constrained_val) {\n"
    "        halide_error(user_context, \"Constraint violated: \" + var_name + \" (\" + value + \") == \" + constrained_var + \" (\" + constrained_var + \")\");\n"
    "        return halide_error_code_constraint_violated; } }\n"
    "if (typeof(halide_error_param_too_small_i64) != \"function\") {  halide_error_param_too_small_i64 = \n"
    "    function(user_context, param_name, value, min_val) {\n"
    "        halide_error(user_context, \"Parameter \" + param_name + \" is \" + value + \" but must be at least \" + min_val);\n"
    "        return halide_error_code_param_too_small; } }\n"
    "if (typeof(halide_error_param_too_small_u64) != \"function\") {  halide_error_param_too_small_u64 = \n"
    "    function(user_context, param_name, value, min_val) {\n"
    "        halide_error(user_context, \"Parameter \" + param_name + \" is \" + value + \" but must be at least \" + min_val);\n"
    "        return halide_error_code_param_too_small; } }\n"
    "if (typeof(halide_error_param_too_small_f64) != \"function\") {  halide_error_param_too_small_f64 = \n"
    "    function(user_context, param_name, value, min_val) { \n"
    "        halide_error(user_context, \"Parameter \" + param_name + \" is \" + value + \" but must be at least \" + min_val);\n"
    "        return halide_error_code_param_too_small; } }\n"
    "if (typeof(halide_error_param_too_large_i64) != \"function\") {  halide_error_param_too_large_i64 = \n"
    "        function(user_context, param_name, value, max_val) {\n"
    "        halide_error(user_context, \"Parameter \" + param_name + \" is \" + value + \" but must be at most \" + max_val);\n"
    "        return halide_error_code_param_too_large; } }\n"
    "if (typeof(halide_error_param_too_large_u64) != \"function\") {  halide_error_param_too_large_u64 = \n"
    "    function(user_context, param_name, value, max_val) {\n"
    "        halide_error(user_context, \"Parameter \" + param_name + \" is \" + value + \" but must be at most \" + max_val);\n"
    "        return halide_error_code_param_too_large; } }\n"
    "if (typeof(halide_error_param_too_large_f64) != \"function\") {  halide_error_param_too_large_f64 = \n"
    "    function(user_context, param_name, value, max_val) {\n"
    "        halide_error(user_context, \"Parameter \" + param_name + \" is \" + value + \" but must be at most \" + min_val);\n"
    "        return halide_error_code_param_too_large; } }\n"
    "if (typeof(halide_error_out_of_memory) != \"function\") {  halide_error_out_of_memory = \n"
    "    function (user_context) {\n"
    "        halide_error(user_context, \"Out of memory (halide_malloc returned NULL)\");\n"
    "        return halide_error_code_out_of_memory; } }\n"
    "if (typeof(halide_error_buffer_argument_is_null) != \"function\") {  halide_error_buffer_argument_is_null = \n"
    "    function(user_context, buffer_name) {\n"
    "        halide_error(user_context, \"Buffer argument \" + buffer_name + \" is NULL\");\n"
    "        return halide_error_code_buffer_argument_is_null; } }\n"
    "if (typeof(halide_error_debug_to_file_failed) != \"function\") {  halide_error_debug_to_file_failed = \n"
    "    function(user_context, func, filename, error_code) {\n"
    "        halide_error(user_context, \"Failed to dump function \" + func + \" to file \" + filename + \" with error \" + error_code);\n"
    "        return halide_error_code_debug_to_file_failed; } }\n"
    "function halide_rewrite_buffer(b, elem_size,\n"
    "                           min0, extent0, stride0,\n"
    "                           min1, extent1, stride1,\n"
    "                           min2, extent2, stride2,\n"
    "                           min3, extent3, stride3) {\n"
    " b.min[0] = min0;\n"
    " b.min[1] = min1;\n"
    " b.min[2] = min2;\n"
    " b.min[3] = min3;\n"
    " b.extent[0] = extent0;\n"
    " b.extent[1] = extent1;\n"
    " b.extent[2] = extent2;\n"
    " b.extent[3] = extent3;\n"
    " b.stride[0] = stride0;\n"
    " b.stride[1] = stride1;\n"
    " b.stride[2] = stride2;\n"
    " b.stride[3] = stride3;\n"
    " return true;\n"
    "}\n"
    "function halide_round(num) {\n"
    " var r = Math.round(num);\n"
    " if (r == num + 0.5 && (r % 2)) { r = Math.floor(num); }\n"
    " return r;\n"
    "}\n"
  //...
    "";
}

CodeGen_JavaScript::CodeGen_JavaScript(ostream &s) : IRPrinter(s), id("$$ BAD ID $$") {}

string CodeGen_JavaScript::make_js_int_cast(string value, bool src_unsigned, int src_bits, bool dst_unsigned, int dst_bits) {
    // TODO: Do we us print_assignment to cache constants?
    string result;
    if ((src_bits <= dst_bits) && (src_unsigned == dst_unsigned)) {
        result = value;
    } else {
        string mask;
        switch (dst_bits) {
        case 1:
            mask = "1";
            break;
        case 8:
            mask = "0xff";
            break;
        case 16:
            mask = "0xffff";
            break;
        case 32:
            mask = "0xffffffff";
            break;
        default:
            internal_error << "Unknown bit width making JavaScript cast.\n";
            break;
        }

        result = print_assignment(Int(32), value + " & " + mask);
        string shift_op = dst_unsigned ? ">>>" : ">>";
        string shift_amount = int_to_string(32 - dst_bits);
        result = print_assignment(Int(32), "(" + result + " << " + shift_amount + ") " + shift_op + " " + shift_amount);
    }
    return result;
}

string javascript_type_array_name_fragment(Type type) {
    string typed_array_name;
    if (type.is_float()) {
        if (type.bits == 32) {
            typed_array_name = "Float32";
        } else if (type.bits == 64) {
            typed_array_name = "Float64";
        } else {
            user_error << "Only 32-bit and 64-bit floating-point types are supported in JavaScript.\n";
        }
    } else {
        if (type.is_uint()) {
            typed_array_name = "Ui";
        } else {
            typed_array_name = "I";
        }
        switch (type.bits) {
            case 8:
              typed_array_name += "nt8";
              break;
            case 16:
              typed_array_name += "nt16";
              break;
            case 32:
              typed_array_name += "nt32";
              break;
            case 64:
              user_error << "64-bit integers are not supported in JavaScript.\n";
              break;
            default:
              break;
        }
    }
    return typed_array_name;
}

string CodeGen_JavaScript::print_reinterpret(Type type, Expr e) {
    if (e.type() == type) {
        return print_expr(e);
    } else if ((type.is_int() || type.is_int()) && (e.type().is_int() || e.type().is_int())) {
        return make_js_int_cast(print_expr(e), e.type().is_uint(), e.type().bits, type.is_uint(), type.bits);
    } else {
        int32_t bytes_needed = (std::max(type.bits, e.type().bits) + 7) / 8;
        string dataview = unique_name('r');
        do_indent();
        stream << "var " << dataview << " = new DataView(new ArrayBuffer(" << bytes_needed << "));\n";
        string setter = "set" + javascript_type_array_name_fragment(e.type());
        string getter = "get" + javascript_type_array_name_fragment(type);
        string val = print_expr(e);
        do_indent();
        stream << dataview << "." << setter << "(0, " << val << ", true);\n";
        return dataview + "." + getter + "(0, true)";
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
#if 0 // $ is allowed in JS, but this may be reserved for internal use. I still think it should be passed literally.
        } else if (name[i] == '$') {
            oss << "__";
#endif
        } else if (name[i] != '_' && !isalnum(name[i])) {
            oss << "___";
        }
        else oss << name[i];
    }
    return oss.str();
}

void CodeGen_JavaScript::compile(Stmt s, string name,
                                 const vector<Argument> &args,
                                 const vector<Buffer> &images_to_embed) {
    stream << preamble;

    // Embed the constant images
    for (size_t i = 0; i < images_to_embed.size(); i++) {
        Buffer buffer = images_to_embed[i];
        string name = print_name(buffer.name());
        buffer_t b = *(buffer.raw_buffer());

        // Figure out the offset of the last pixel.
        size_t num_elems = 1;
        for (int d = 0; b.extent[d]; d++) {
            num_elems += b.stride[d] * (b.extent[d] - 1);
        }

        // Emit the data
        stream << "var " << name << "_data = new Uint8Array([";
        for (size_t i = 0; i < num_elems * b.elem_size; i++) {
            if (i > 0) stream << ", ";
            stream << (int)(b.host[i]);
        }
        stream << "]);\n";

        // Emit the buffer_t
        user_assert(b.host) << "Can't embed image: " << buffer.name() << " because it has a null host pointer\n";
        user_assert(!b.dev_dirty) << "Can't embed image: " << buffer.name() << "because it has a dirty device pointer\n";
        stream << "var " << name << "_buffer = {"
               << "dev: 0, " // dev
               << "host: " << name << "_data[0], " // host
               << "extent: [" << b.extent[0] << ", " << b.extent[1] << ", " << b.extent[2] << ", " << b.extent[3] << "], "
               << "stride: [" << b.stride[0] << ", " << b.stride[1] << ", " << b.stride[2] << ", " << b.stride[3] << "], "
               << "min: [" << b.min[0] << ", " << b.min[1] << ", " << b.min[2] << ", " << b.min[3] << "], "
               << "elem_size: " << b.elem_size << ", "
               << "host_dirty: 0, " // host_dirty
               << "dev_dirty: 0};\n"; //dev_dirty

        // TODO: Is this necessary?
        // Make a global pointer to it
        stream << "var " << name << " = " << name << "_buffer;\n";

    }

    have_user_context = false;
    for (size_t i = 0; i < args.size(); i++) {
        have_user_context |= (args[i].name == "__user_context");
    }

    // Emit the function prototype
    stream << "function " << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
      if (args[i].is_buffer()) {
            stream << print_name(args[i].name)
                   << "_buffer";
        } else {
            stream << print_name(args[i].name);
        }

        if (i < args.size()-1) stream << ", ";
    }

    stream << ") {\n";

    // Unpack the buffer_t's
    for (size_t i = 0; i < args.size(); i++) {
      if (args[i].is_buffer()) {
            unpack_buffer(args[i].type, args[i].name);
        }
    }
    for (size_t i = 0; i < images_to_embed.size(); i++) {
        unpack_buffer(images_to_embed[i].type(), images_to_embed[i].name());
    }
    // Emit the body
    print(s);

    stream << "return 0;\n"
           << "}\n";
}

void CodeGen_JavaScript::unpack_buffer(Type t, const std::string &buffer_name) {
    string name = print_name(buffer_name);
    string buf_name = name + "_buffer";

    stream << "var "
           << name
           << " = "
           << buf_name
           << ".host;\n";
    allocations.push(buffer_name, t);

    stream << "var "
           << name
           << "_host_and_dev_are_null = ("
           << buf_name << ".host == null) && ("
           << buf_name << ".dev == 0);\n";

    for (int j = 0; j < 4; j++) {
        stream << "var "
               << name
               << "_min_" << j << " = "
               << buf_name
               << ".min[" << j << "];\n";
    }
    for (int j = 0; j < 4; j++) {
        stream << "var "
               << name
               << "_extent_" << j << " = "
               << buf_name
               << ".extent[" << j << "];\n";
    }
    for (int j = 0; j < 4; j++) {
        stream << "var "
               << name
               << "_stride_" << j << " = "
               << buf_name
               << ".stride[" << j << "];\n";
    }
    stream << "var "
           << name
           << "_elem_size = "
           << buf_name
           << ".elem_size;\n";
}

string CodeGen_JavaScript::print_expr(Expr e) {
    id = "$$ BAD ID $$";
    e.accept(this);
    return id;
}

void CodeGen_JavaScript::print_stmt(Stmt s) {
    s.accept(this);
}

string CodeGen_JavaScript::print_assignment(Type t, const std::string &rhs) {
    // TODO: t is ignored and I expect casts will be required for value correctness.
    map<string, string>::iterator cached = cache.find(rhs);

    if (cached == cache.end()) {
        id = unique_name('_');
        do_indent();
        stream << "var " << id << " = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

void CodeGen_JavaScript::open_scope() {
    cache.clear();
    do_indent();
    indent++;
    stream << "{\n";
}

void CodeGen_JavaScript::close_scope(const std::string &comment) {
    cache.clear();
    indent--;
    do_indent();
    if (!comment.empty()) {
        stream << "} // " << comment << "\n";
    } else {
        stream << "}\n";
    }
}

void CodeGen_JavaScript::visit(const Variable *op) {
    id = print_name(op->name);
}

void CodeGen_JavaScript::visit(const Cast *op) {
    Halide::Type src = op->value.type();
    Halide::Type dst = op->type;

    string value = print_expr(op->value);

    if (dst.is_handle() && src.is_handle()) {
    } else if (dst.is_handle() || src.is_handle()) {
        internal_error << "Can't cast from " << src << " to " << dst << "\n";
    } else if (!src.is_float() && !dst.is_float()) {
        value = make_js_int_cast(value, src.is_uint(), src.bits, dst.is_uint(), dst.bits);
    } else if (src.is_float() && dst.is_int()) {
        value = make_js_int_cast("Math.trunc(" + value + ")", false, 64, false, dst.bits);
    } else if (src.is_float() && dst.is_uint()) {
        value = make_js_int_cast("Math.trunc(" + value + ")", false, 64, true, dst.bits);
    } else if (src.is_int() && dst.is_float()) {
    } else if (src.is_uint() && dst.is_float()) {
    } else {
        internal_assert(src.is_float() && dst.is_float());
        if (dst.bits == 32 && src.bits == 64) {
            value = "Math.fround(" + value + ")";
        } // otherwise a no-op
    }
  
    print_assignment(op->type, value);
}

Expr conditionally_extract_lane(Expr e, int lane) {
    internal_assert(lane < e.type().width) << "Bad lane in conditionally_extract_lane\n";
    if (e.type().width != 1) {
        return extract_lane(e, lane);
    } else {
        return e;
    }
}

void CodeGen_JavaScript::visit_binop(Type t, Expr a, Expr b, const char * op) {
    ostringstream rhs;
    const char *lead_char = (t.width != 1) ? "[" : "";
    for (int lane = 0; lane < t.width; lane++) {
        string sa = print_expr(conditionally_extract_lane(a, lane));
        string sb = print_expr(conditionally_extract_lane(b, lane));
        rhs << lead_char << sa << " " << op << " " << sb;
        lead_char = ", ";
    }
    if (t.width > 1) {
        rhs << "]";
    }
    print_assignment(t, rhs.str());
}

void CodeGen_JavaScript::visit(const Add *op) {
    visit_binop(op->type, op->a, op->b, "+");
}

void CodeGen_JavaScript::visit(const Sub *op) {
    visit_binop(op->type, op->a, op->b, "-");
}

void CodeGen_JavaScript::visit(const Mul *op) {
    visit_binop(op->type, op->a, op->b, "*");
}

void CodeGen_JavaScript::visit(const Div *op) {
    const char *lead_char = (op->type.width != 1) ? "[" : "";
    ostringstream rhs;
    for (int lane = 0; lane < op->type.width; lane++) {
        int bits;

        if (is_const_power_of_two(conditionally_extract_lane(op->b, lane), &bits)) {
            rhs << lead_char << print_expr(conditionally_extract_lane(op->a, lane)) << " >> " << bits;
        } else {
            string a = print_expr(conditionally_extract_lane(op->a, lane));
            string b = print_expr(conditionally_extract_lane(op->b, lane));
            rhs << lead_char;
            if (!op->type.is_float()) {
                rhs << "Math.floor(" << a << " / " << b << ")";
            } else {
                rhs << a << " / " << b;
            }
        }
        lead_char = ", ";
    }
    if (op->type.width > 1) {
        rhs << "]";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Mod *op) {
    const char *lead_char = (op->type.width != 1) ? "[" : "";
    ostringstream rhs;
    for (int lane = 0; lane < op->type.width; lane++) {
        int bits;

        if (is_const_power_of_two(conditionally_extract_lane(op->b, lane), &bits)) {
            rhs << lead_char << print_expr(conditionally_extract_lane(op->a, lane)) << " & " << ((1 << bits) - 1);
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
                stream << "var " << var_name << " = " << a << " - " << b << " * Math.floor(" << a << " / " << b << "); ";
            }
            rhs << lead_char << var_name;
        }
        lead_char = ", ";
    }
    if (op->type.width > 1) {
        rhs << "]";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Max *op) {
    print_expr(Call::make(op->type, "Math.max", vec(op->a, op->b), Call::Extern));
}

void CodeGen_JavaScript::visit(const Min *op) {
    print_expr(Call::make(op->type, "Math.min", vec(op->a, op->b), Call::Extern));
}

void CodeGen_JavaScript::visit(const EQ *op) {
    visit_binop(op->type, op->a, op->b, "==");
}

void CodeGen_JavaScript::visit(const NE *op) {
    visit_binop(op->type, op->a, op->b, "!=");
}

void CodeGen_JavaScript::visit(const LT *op) {
    visit_binop(op->type, op->a, op->b, "<");
}

void CodeGen_JavaScript::visit(const LE *op) {
    visit_binop(op->type, op->a, op->b, "<=");
}

void CodeGen_JavaScript::visit(const GT *op) {
    visit_binop(op->type, op->a, op->b, ">");
}

void CodeGen_JavaScript::visit(const GE *op) {
    visit_binop(op->type, op->a, op->b, ">=");
}

void CodeGen_JavaScript::visit(const Or *op) {
    visit_binop(op->type, op->a, op->b, "||");
}

void CodeGen_JavaScript::visit(const And *op) {
    visit_binop(op->type, op->a, op->b, "&&");
}

void CodeGen_JavaScript::visit(const Not *op) {
    print_assignment(op->type, "!(" + print_expr(op->a) + ")");
}

void CodeGen_JavaScript::visit(const IntImm *op) {
    ostringstream oss;
    oss << op->value;
    id = oss.str();
}

void CodeGen_JavaScript::visit(const StringImm *op) {
    ostringstream oss;
    oss << Expr(op);
    id = oss.str();
}

// NaN is the only float/double for which this is true... and
// surprisingly, there doesn't seem to be a portable isnan function
// (dsharlet).
template <typename T>
static bool isnan(T x) { return x != x; }

template <typename T>
static bool isinf(T x)
{
    return std::numeric_limits<T>::has_infinity && (
        x == std::numeric_limits<T>::infinity() ||
        x == -std::numeric_limits<T>::infinity());
}

void CodeGen_JavaScript::visit(const FloatImm *op) {
    if (isnan(op->value)) {
        id = "Number.NaN";
    } else if (isinf(op->value)) {
        if (op->value > 0) {
            id = "Number.POSITIVE_INFINITY";
        } else {
            id = "Number.NEGATIVE_INFINITY";
        }
    } else {
#if 0   // TODO: Figure out if there is a way to wrie a floating-point hex literal in JS
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
    { "neg_inf_f32", "Number.NEGATIVE_INFINITY" },
    { "inf_f32", "Number.INFINITY" },
    { "minval_f32", "-3.4028234663852885981e+38" },
    { "maxval_f32", "3.4028234663852885981e+38" },
    { "minval_f64", "Number.MIN_VALUE" },
    { "maxval_f64", "Number.MAX_VALUE" },
    { "nan_f32", "Number.NaN" },
    { "nan_f64", "Number.NaN" },
};

std::map<string, std::pair<string, int> > js_math_functions {
  { { "sqrt_f32", { "Math.sqrt", 1 } },
    { "sqrt_f64", { "Math.sqrt", 1 } },
    { "sin_f32", { "Math.sin", 1 } },
    { "sin_f64", { "Math.sin", 1 } },
    { "cos_f32", { "Math.cos", 1 } },
    { "cos_f64", { "Math.cos", 1 } },
    { "tan_f32", { "Math.tan", 1 } },
    { "tan_f64", { "Math.tan", 1 } },
    { "exp_f32", { "Math.exp", 1 } },
    { "exp_f64", { "Math.exp", 1 } },
    { "log_f32", { "Math.log", 1 } },
    { "log_f64", { "Math.log", 1 } },
    { "abs_f32", { "Math.abs", 1 } },
    { "abs_f64", { "Math.abs", 1 } },
    { "floor_f32", { "Math.floor", 1 } },
    { "floor_f64", { "Math.floor", 1 } },
    { "ceil_f32", { "Math.ceil", 1 } },
    { "ceil_f64", { "Math.ceil", 1 } },
    { "round_f32", { "halide_round", 1 } }, // TODO: Figure out if we want a Halide top-level object.
    { "round_f64", { "halide_round", 1 } },
    { "trunc_f32", { "Math.trunc", 1 } },
    { "trunc_f64", { "Math.trunc", 1 } },
    { "pow_f32", { "Math.pow", 2 } },
    { "pow_f64", { "Math.pow", 2 } },
    { "asin_f32", { "Math.asin", 1 } },
    { "asin_f64", { "Math.asin", 1 } },
    { "acos_f32", { "Math.acos", 1 } },
    { "acos_f64", { "Math.acos", 1 } },
    { "atan_f32", { "Math.atan", 1 } },
    { "atan_f64", { "Math.atan", 1 } },
    { "atan2_f32", { "Math.atan2", 2 } },
    { "atan2_f64", { "Math.atan2", 2 } },
    { "sinh_f32", { "Math.sinh", 1 } },
    { "sinh_f64", { "Math.sinh", 1 } },
    { "cosh_f32", { "Math.cosh", 1 } },
    { "cosh_f64", { "Math.cosh", 1 } },
    { "tanh_f32", { "Math.tanh", 1 } },
    { "tanh_f64", { "Math.tanh", 1 } },
    { "asinh_f32", { "Math.asinh", 1 } },
    { "asinh_f64", { "Math.asinh", 1 } },
    { "acosh_f32", { "Math.acosh", 1 } },
    { "acosh_f64", { "Math.acosh", 1 } },
    { "atanh_f32", { "Math.atanh", 1 } },
    { "atanh_f64", { "Math.atanh", 1 } },
    { "is_nan_f32", { "Number.isNaN", 1 } },
    { "is_nan_f64", { "Number.isNaN", 1 } },
  }
};

}

void CodeGen_JavaScript::visit(const Call *op) {
    internal_assert((op->call_type == Call::Extern || op->call_type == Call::Intrinsic))
        << "Can only codegen extern calls and intrinsics\n";

    ostringstream rhs;

    // Handle intrinsics first
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::shuffle_vector) {
            internal_assert((int) op->args.size() == 1 + op->type.width);
            string input_vector = print_expr(op->args[0]);
            const char *lead_char = (op->type.width != 1) ? "[" : "";
            for (size_t i = 1; i < op->args.size(); i++) {
                rhs << lead_char << input_vector << "[" << print_expr(op->args[i]) << "]";
                lead_char = ", ";
            }
            if (op->type.width != 1) {
                rhs << "]";
            }
        } else if (op->name == Call::interleave_vectors) {
            vector<string> vecs(op->args.size());
            for (size_t i = 0; i < op->args.size(); i++) {
                vecs[i] = print_expr(op->args[i]);
            }
            const char *lead_char = "[";
            for (int i = 0; i < op->type.width; i++) {
                for (size_t j = 0; j < op->args.size(); j++) {
                    rhs << lead_char <<  vecs[j] << "[" << i << "]";
                    lead_char = ", ";
                }
            }
            rhs << "]";
        } else if (op->name == Call::debug_to_file) {
            internal_assert(op->args.size() == 9);
            const StringImm *string_imm = op->args[0].as<StringImm>();
            internal_assert(string_imm);
            string filename = string_imm->value;
            const Load *load = op->args[1].as<Load>();
            internal_assert(load);
            string func = print_name(load->name);

            vector<string> args(6);
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = print_expr(op->args[i+3]);
            }

            rhs << "halide_debug_to_file(";
            rhs << (have_user_context ? "__user_context" : "null");
            rhs << ", \"" + filename + "\", " + func;
            for (size_t i = 0; i < args.size(); i++) {
                rhs << ", " << args[i];
            }
            rhs << ")";
        } else if (op->name == Call::bitwise_and) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " & " << a1;
        } else if (op->name == Call::bitwise_xor) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " ^ " << a1;
        } else if (op->name == Call::bitwise_or) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " | " << a1;
        } else if (op->name == Call::bitwise_not) {
            internal_assert(op->args.size() == 1);
            rhs << "~" << print_expr(op->args[0]);
        } else if (op->name == Call::reinterpret) {
            internal_assert(op->args.size() == 1);
            rhs << print_reinterpret(op->type, op->args[0]);
        } else if (op->name == Call::shift_left) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " << " << a1;
        } else if (op->name == Call::shift_right) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " >> " << a1;
        } else if (op->name == Call::rewrite_buffer) {
            int dims = ((int)(op->args.size())-2)/3;
            (void)dims; // In case internal_assert is ifdef'd to do nothing
            internal_assert((int)(op->args.size()) == dims*3 + 2);
            internal_assert(dims <= 4);
            vector<string> args(op->args.size());
            const Variable *v = op->args[0].as<Variable>();
            internal_assert(v);
            args[0] = print_name(v->name);
            for (size_t i = 1; i < op->args.size(); i++) {
                args[i] = print_expr(op->args[i]);
            }
            rhs << "halide_rewrite_buffer(";
            for (size_t i = 0; i < 14; i++) {
                if (i > 0) rhs << ", ";
                if (i < args.size()) {
                    rhs << args[i];
                } else {
                    rhs << '0';
                }
            }
            rhs << ")";
        } else if (op->name == Call::profiling_timer) {
            internal_assert(op->args.size() == 0);
            rhs << "halide_profiling_timer(";
            rhs << (have_user_context ? "__user_context" : "null");
            rhs << ")";
        } else if (op->name == Call::null_handle) {
            rhs << "null";
        } else if (op->name == Call::address_of) {
            // TODO: Figure out if arrays can be sliced, viewed, etc.
            const Load *l = op->args[0].as<Load>();
            internal_assert(op->args.size() == 1 && l);
            rhs << print_name(l->name) << ".subarray(" << print_expr(l->index) << ")";
        } else if (op->name == Call::trace || op->name == Call::trace_expr) {
            int int_args = (int)(op->args.size()) - 5;
            internal_assert(int_args >= 0);

            Type type = op->args[4].type();

            ostringstream value_stream;
            const char *lead_char = "[";
            for (int32_t v_index = 0; v_index < type.width; v_index++) {
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

            string event_name = unique_name('e');
            do_indent();
            stream << "var " << event_name << " = { func: " << op->args[0] << ", ";
            stream << "event: " << op->args[1] << ", ";
            stream << "parent_id: " << op->args[2] << ", ";
            stream << "type_code: " << type.code << ", bits: " << type.bits << ", vector_width: " << type.width << ", ";
            stream << "value_index: " << op->args[3] << ", ";
            stream << "value: " << value_stream.str() << ", ";
            stream << "dimensions: " << int_args * type.width << ", ";
            stream << "coordinates: " << coordinates_stream.str() << " }\n";

            if (op->name == Call::trace_expr) {
                do_indent();
                rhs << "halide_trace(__user_context, " << event_name << ")";
            } else {
                stream << "halide_trace(__user_context, " << event_name << ");\n";
                rhs << print_expr(op->args[4]);
            }
        } else if (op->name == Call::lerp) {
            Expr e = lower_lerp(op->args[0], op->args[1], op->args[2]);
            rhs << print_expr(e);
        } else if (op->name == Call::popcount) {
            Expr e = cast<uint32_t>(op->args[0]);
            e = e - ((e >> 1) & 0x55555555);
            e = (e & 0x33333333) + ((e >> 2) & 0x33333333);
            e = (e & 0x0f0f0f0f) + ((e >> 4) & 0x0f0f0f0f);
            e = (e * 0x1010101) >> 24;
            rhs << print_expr(e);
        } else if (op->name == Call::count_leading_zeros) {
            // TODO: Should this be a print_assignment?
            string e = print_expr(op->args[0]);
            int32_t bits = op->args[0].type().bits;
            rhs << "((" << e << "< 0) ? 0 : ((" << e << " == 0) ? " << bits << ": " << (bits - 1) << " - ((Math.log(" << e << ")  / Math.LN2) >> 0)))";
        } else if (op->name == Call::count_trailing_zeros) {
            Expr e = op->args[0];
            int32_t bits = op->args[0].type().bits;

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
        } else if (op->name == Call::return_second) {
            internal_assert(op->args.size() == 2);
            string arg0 = print_expr(op->args[0]);
            string arg1 = print_expr(op->args[1]);
            rhs << "(" << arg0 << ", " << arg1 << ")";
        } else if (op->name == Call::if_then_else) {
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
        } else if (op->name == Call::copy_buffer_t) {
            internal_assert(op->args.size() == 1);
            string arg = print_expr(op->args[0]);
            string buf_id = unique_name('B');
            do_indent();
            stream << "var " << buf_id << " = ";
            open_scope();
            do_indent();
            stream << "dev: " << arg << ".dev,\n";
            do_indent();
            stream << "host: " << arg << ".host,\n";
            do_indent();
            stream << "min: [" << arg << ".min[0], " << arg << ".min[1], " << arg << ".min[2], " << arg << ".min[3]],\n";
            do_indent();
            stream << "extent: [" << arg << ".extent[0], " << arg << ".extent[1], " << arg << ".extent[2], " << arg << ".extent[3]],\n";
            do_indent();
            stream << "stride: [" << arg << ".stride[0], " << arg << ".stride[1], " << arg << ".stride[2], " << arg << ".stride[3]],\n";
            do_indent();
            stream << "elem_size: " << arg << ".elem_size,\n";
            do_indent();
            stream << "host_dirty: " << arg << ".host_dirty,\n";
            do_indent();
            stream << "dev_dirty: " << arg << ".dev_dirty,\n";
            close_scope("copy_buffer_t");
            rhs << buf_id;
        } else if (op->name == Call::create_buffer_t) {
            internal_assert(op->args.size() >= 2);
            vector<string> args;
            for (size_t i = 0; i < op->args.size(); i++) {
                args.push_back(print_expr(op->args[i]));
            }
            string buf_id = unique_name('B');
            do_indent();
            stream << "var " << buf_id << " = {\n";
            do_indent();
            stream << "dev: 0,\n";
            do_indent();
            stream << "host: " << args[0] << ",\n";
            int dims = ((int)op->args.size() - 2)/3;
            do_indent();
            stream << "min: ["; 
            for (int i = 0; i < dims; i++) {
                if (i > 0) {
                    stream << ", ";
                }
                stream << args[i*3+2];
            }
            stream << "],\n";
            do_indent();
            stream << "extent: [";
            for (int i = 0; i < dims; i++) {
                if (i > 0) {
                    stream << ", ";
                }
                stream << args[i*3+3];
            }
            stream << "],\n";
            do_indent();
            stream << "stride: [";
            for (int i = 0; i < dims; i++) {
                if (i > 0) {
                    stream << ", ";
                }
                stream << args[i*3+4];
            }
            stream << "],\n";
            do_indent();
            stream << "elem_size: " << args[1] << ",\n";
            do_indent();
            stream << "host_dirty: false,\n";
            do_indent();
            stream << "dev_dirty: false,\n";
            stream << "};\n";
            rhs << buf_id;
        } else if (op->name == Call::extract_buffer_max) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << "(" << a0 << ".min[" << a1 << "] + " << a0 << ".extent[" << a1 << "] - 1)";
        } else if (op->name == Call::extract_buffer_min) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << ".min[" << a1 << "]";
        } else if (op->name == Call::set_host_dirty) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            do_indent();
            stream << a0 << ".host_dirty = " << a1 << ";\n";
            rhs << "0";
        } else if (op->name == Call::set_dev_dirty) {
            internal_assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            do_indent();
            stream << a0 << ".dev_dirty = " << a1 << ";\n";
            rhs << "0";
        } else if (op->name == Call::abs) {
            internal_assert(op->args.size() == 1);
            string arg = print_expr(op->args[0]);
            // TODO: Should this use Math.abs?
            rhs << "(" << arg << " > 0 ? " << arg << " : -" << arg << ")";
        } else if (op->name == Call::memoize_expr) {
            internal_assert(op->args.size() >= 1);
            string arg = print_expr(op->args[0]);
            rhs << "(" << arg << ")";
        } else if (op->name == Call::copy_memory) {
            internal_assert(op->args.size() == 3);
            string dest = print_expr(op->args[0]);
            string src = print_expr(op->args[1]);
            string size = print_expr(op->args[2]);
            string index_var = unique_name('i');
            stream << "for (var " << index_var << " = 0; " << index_var << " < " << size << "; " << index_var << "++) ";
            open_scope();
            do_indent();
            stream << dest << "[" << index_var << "] = " << src << "[" << index_var << "];\n";
            close_scope("memcpy");
            rhs << dest;
        } else if (op->name == Call::make_struct) {
            // TODO: Figure out if this is at all useful in JavaScript
            // and if it should be an array rather than an object.

            // Emit a line something like:
            // var foo = {f_0: 3.0f, char f_1: 'c', int f_2: 4 };

            // Get the args
            vector<string> values;
            for (size_t i = 0; i < op->args.size(); i++) {
                values.push_back(print_expr(op->args[i]));
            }
            do_indent();
            string struct_name = unique_name('s');
            stream << "var " << struct_name << " = { ";
            for (size_t i = 0; i < op->args.size(); i++) {
                if (i > 0) stream << ", ";
                stream << "f_" << i << ": " << values[i];
            }
            stream <<" } // make struct\n";
            rhs << struct_name;
        } else if (op->name == Call::stringify) {
            string buf_name = unique_name('b');

            // Print all args that are general Exprs before starting output on stream.
            std::vector<Expr> printed_args(op->args.size());
            for (size_t i = 0; i < op->args.size(); i++) {
                if (op->args[i].as<StringImm>() == NULL && !op->args[i].type().is_handle()) {
                    printed_args[i] = print_expr(op->args[i]);
                }
            }
            do_indent();
            stream << "var " << buf_name << " = \"\";\n";
            for (size_t i = 0; i < op->args.size(); i++) {
                Type t = op->args[i].type();

                do_indent();
                if (op->args[i].as<StringImm>()) {
                  stream << buf_name << " = " << buf_name << ".concat(" << op->args[i] << ");\n";
                } else if (t.is_handle()) {
                    stream << buf_name << " = " << buf_name << ".concat(\"<Object>\");\n";
                } else {
                    stream << buf_name << " = " << buf_name << ".concat(" << printed_args[i] << ".toString());\n";
                }
            }
            rhs << buf_name;
        } else {
            // TODO: other intrinsics
            internal_error << "Unhandled intrinsic in JavaScript backend: " << op->name << '\n';
        }
    } else {
        // Generic calls

        auto js_math_value = js_math_values.find(op->name);

        if (js_math_value != js_math_values.end()) {
            rhs << js_math_value->second;;
        } else {
            // Map math functions to JS names.
            string js_name = op->name;
            auto js_math_fn = js_math_functions.find(op->name);
            if (js_math_fn != js_math_functions.end()) {
                js_name = js_math_fn->second.first;
            }

            for (int lane = 0; lane < op->type.width; lane++) {

                const char *lead_char = "";
                if (op->type.width != 1) {
                    lead_char = (lane == 0) ? "[" : ", ";
                }

                vector<string> args(op->args.size());
                for (size_t i = 0; i < op->args.size(); i++) {
                    args[i] = print_expr(conditionally_extract_lane(op->args[i], lane));
                }
                rhs << lead_char << js_name << "(";

                if (function_takes_user_context(op->name)) {
                    rhs << (have_user_context ? "__user_context, " : "null, ");
                }

                for (size_t i = 0; i < op->args.size(); i++) {
                    if (i > 0) rhs << ", ";
                    rhs << args[i];
                }
                rhs << ")";
            }
        }
        if (op->type.width != 1) {
            rhs << "]";
        }

    }

    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Load *op) {
#if 0 // TODO: Figure out if this can ever result in a value changing cast.
    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name) == op->type);
#endif
    ostringstream rhs;
    if (op->type.width == 1) {
        rhs << print_name(op->name);
        rhs << "["
            << print_expr(op->index)
            << "]";
    } else {
        std::vector<string> indices;

        for (int32_t i = 0; i < op->type.width; i++) {
            indices.push_back(print_expr(extract_lane(op->index, i)));
        }
        rhs << "[";
        for (int32_t i = 0; i < op->type.width; i++) {
            if (i != 0) {
                rhs << ", ";
            }
            rhs << print_name(op->name) << "[" << indices[i] << "]";
        }
        rhs << "]";
    }

    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Ramp *op) {
    ostringstream rhs;
    string base = print_expr(op->base);
    string stride = print_expr(op->stride);
    rhs << "[";
    for (int32_t i = 0; i < op->width; i++) {
        if (i != 0) {
            rhs << ", ";
        }
        rhs << base << " + " << stride << " * " << i;
    }
    rhs << "]";
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Broadcast *op) {
    ostringstream rhs;
    string value = print_expr(op->value);
    const char *lead_char = (op->type.width != 1) ? "[" : "";
    for (int32_t i = 0; i < op->width; i++) {
        rhs << lead_char << value;
        lead_char = ", ";
    }
    if (op->type.width != 1) {
        rhs << "]";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_JavaScript::visit(const Store *op) {
#if 0 // TODO: Figure out if this can ever result in a value changing cast.
    Type t = op->value.type();

    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name) == t);
#endif

    int32_t width = op->value.type().width;
    if (width == 1) {
        string id_index = print_expr(op->index);
        string id_value = print_expr(op->value);
        do_indent();

        stream << print_name(op->name);
        stream << "["
               << id_index
               << "] = "
               << id_value
               << ";\n";
    } else {
        std::vector<string> indices;
        std::vector<string> values;

        for (int32_t i = 0; i < width; i++) {
            indices.push_back(print_expr(extract_lane(op->index, i)));
            values.push_back(print_expr(extract_lane(op->value, i)));
        }
        for (int32_t i = 0; i < width; i++) {
            stream << print_name(op->name);
            stream << "["
                   << indices[i]
                   << "] = "
                   << values[i]
                   << ";\n";

        }
    }
    cache.clear();
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
    rhs << "(" << cond
        << " ? " << true_val
        << " : " << false_val
        << ")";
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
    // Halide asserts have different semantics to C asserts.  They're
    // supposed to clean up and make the containing function return
    // -1, so we can't use the C version of assert. Instead we convert
    // to an if statement.

    stream << "if (!" << id_cond << ") ";
    open_scope();
    string id_msg = print_expr(op->message);
    do_indent();
    stream << "halide_error("
           << (have_user_context ? "__user_context, " : "null, ")
           << id_msg
           << ");\n";
    do_indent();
    stream << "return -1;\n";
    close_scope("");
}

void CodeGen_JavaScript::visit(const Pipeline *op) {
    do_indent();
    stream << "// produce " << op->name << '\n';
    print_stmt(op->produce);

    if (op->update.defined()) {
        do_indent();
        stream << "// update " << op->name << '\n';
        print_stmt(op->update);
    }

    do_indent();
    stream << "// consume " << op->name << '\n';
    print_stmt(op->consume);
}

void CodeGen_JavaScript::visit(const For *op) {
#if 0 // TODO: RiverTrail
    if (op->for_type == For::Parallel) {
        do_indent();
        stream << "#pragma omp parallel for\n";
    } else {
        internal_assert(op->for_type == For::Serial)
            << "Can only emit serial or parallel for loops to C\n";
    }
#endif

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

    allocations.push(op->name, op->type);
    
    internal_assert(op->type.is_float() || op->type.is_int() || op->type.is_uint()) << "Cannot allocate numeric type in JavaScript codegen.\n";
    internal_assert(op->type.width == 1) << "Vector types not supported in JavaScript codegen.\n";

    string typed_array_name = javascript_type_array_name_fragment(op->type) + "Array";
    std::string allocation_size;
    int32_t constant_size;
    // This both potentially does strength reduction at compile time, but also handles the zero extents case.
    if (constant_allocation_size(op->extents, op->name, constant_size)) {
        allocation_size = print_expr(static_cast<int32_t>(constant_size));
    } else {
        // TODO: Verify overflow is not a concern.
        allocation_size = print_expr(op->extents[0]);
        for (size_t i = 1; i < op->extents.size(); i++) {
            allocation_size = print_assignment(Float(64), allocation_size + " * " + print_expr(op->extents[i]));
        }
    }

    stream << "var " << print_name(op->name) << " = new " << typed_array_name << "(" << allocation_size << ");\n";
    // TODO: Error handling?
    heap_allocations.push(op->name, 0);
    do_indent();

    op->body.accept(this);

    // Should have been freed internally
    internal_assert(!heap_allocations.contains(op->name));

    close_scope("alloc " + print_name(op->name));
}

void CodeGen_JavaScript::visit(const Free *op) {
    stream << print_name(op->name) << " = undefined;"; // TODO: should this be null?
    heap_allocations.pop(op->name);
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
    cg.compile(s, "test1", args, vector<Buffer>());

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
