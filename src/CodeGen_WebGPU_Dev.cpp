#include <sstream>
#include <utility>

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "CodeGen_WebGPU_Dev.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

namespace {

class CodeGen_WebGPU_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_WebGPU_Dev(const Target &target);

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    void add_kernel(Stmt stmt,
                    const string &name,
                    const vector<DeviceArgument> &args) override;

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    void init_module() override;

    vector<char> compile_to_src() override;

    string get_current_kernel_name() override;

    void dump() override;

    string print_gpu_name(const string &name) override;

    string api_unique_name() override {
        return "webgpu";
    }

protected:
    class CodeGen_WGSL : public CodeGen_C {
    public:
        CodeGen_WGSL(std::ostream &s, Target t)
            : CodeGen_C(s, t) {
        }
        void add_kernel(const Stmt &stmt,
                        const string &name,
                        const vector<DeviceArgument> &args);

    protected:
        using CodeGen_C::visit;

        std::string print_name(const std::string &) override;
        std::string print_type(Type type,
                               AppendSpaceIfNeeded append_space =
                                   DoNotAppendSpace) override;
        std::string print_assignment(Type t, const std::string &rhs) override;

        void visit(const Allocate *op) override;
        void visit(const Cast *) override;
        void visit(const IntImm *) override;
        void visit(const UIntImm *) override;
        void visit(const For *) override;
        void visit(const Min *op) override;
        void visit(const Max *op) override;
        void visit(const Select *op) override;
    };

    std::ostringstream src_stream;
    string cur_kernel_name;
    CodeGen_WGSL wgsl;
};

CodeGen_WebGPU_Dev::CodeGen_WebGPU_Dev(const Target &t)
    : wgsl(src_stream, t) {
}

void CodeGen_WebGPU_Dev::add_kernel(Stmt s,
                                    const string &name,
                                    const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_WebGPU_Dev::add_kernel " << name << "\n";
    debug(2) << "CodeGen_WebGPU_Dev:\n"
             << s;

    cur_kernel_name = name;
    wgsl.add_kernel(s, name, args);
}

void CodeGen_WebGPU_Dev::init_module() {
    debug(2) << "WebGPU device codegen init_module\n";

    // Wipe the internal shader source.
    src_stream.str("");
    src_stream.clear();

    // Write out the Halide math functions.
    src_stream
        << "fn float_from_bits(x : i32) -> f32 {return bitcast<f32>(x);}\n"
        << "fn acos_f32(x : f32) -> f32 {return acos(x);}\n"
        << "fn acosh_f32(x : f32) -> f32 {return log(x + sqrt(x*x - 1.0));}\n"
        << "fn asin_f32(x : f32) -> f32 {return asin(x);}\n"
        << "fn asinh_f32(x : f32) -> f32 {return log(x + sqrt(x*x + 1.0));}\n"
        << "fn atan_f32(x : f32) -> f32 {return atan(x);}\n"
        << "fn atan2_f32(y : f32, x : f32) -> f32 {return atan2(y, x);}\n"
        << "fn atanh_f32(x : f32) -> f32 {return log((1.0+x)/(1.0-x))/2.0;}\n"
        << "fn ceil_f32(x : f32) -> f32 {return ceil(x);}\n"
        << "fn cos_f32(x : f32) -> f32 {return cos(x);}\n"
        << "fn cosh_f32(x : f32) -> f32 {return cosh(x);}\n"
        << "fn exp_f32(x : f32) -> f32 {return exp(x);}\n"
        << "fn floor_f32(x : f32) -> f32 {return floor(x);}\n"
        << "fn fast_inverse_f32(x : f32) -> f32 {return 1.0 / x;}\n"
        << "fn fast_inverse_sqrt_f32(x : f32) -> f32 {return inverseSqrt(x);}\n"
        << "fn log_f32(x : f32) -> f32 {return log(x);}\n"
        << "fn pow_f32(x : f32, y : f32) -> f32 {return pow(x, y);}\n"
        << "fn round_f32(x : f32) -> f32 {return round(x);}\n"
        << "fn sin_f32(x : f32) -> f32 {return sin(x);}\n"
        << "fn sinh_f32(x : f32) -> f32 {return sinh(x);}\n"
        << "fn sqrt_f32(x : f32) -> f32 {return sqrt(x);}\n"
        << "fn tan_f32(x : f32) -> f32 {return tan(x);}\n"
        << "fn tanh_f32(x : f32) -> f32 {return tanh(x);}\n"
        << "fn trunc_f32(x : f32) -> f32 {return trunc(x);}\n";
}

vector<char> CodeGen_WebGPU_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "WGSL shader:\n"
             << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_WebGPU_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_WebGPU_Dev::dump() {
    std::cerr << src_stream.str() << "\n";
}

string CodeGen_WebGPU_Dev::print_gpu_name(const string &name) {
    return name;
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_name(const string &name) {
    string new_name = c_print_name(name);

    // The double-underscore prefix is reserved in WGSL.
    if (new_name.length() > 1 && new_name[0] == '_' && new_name[1] == '_') {
        new_name = "v" + new_name;
    }
    return new_name;
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_type(Type type,
                                                    AppendSpaceIfNeeded space) {
    ostringstream oss;

    if (type.lanes() != 1) {
        switch (type.lanes()) {
        case 2:
        case 3:
        case 4:
            oss << "vec" << type.lanes() << "<";
            break;
        default:
            user_error << "Unsupported vector width in WGSL: " << type << "\n";
        }
    }

    if (type.is_float()) {
        user_assert(type.bits() == 32) << "WGSL only supports 32-bit floats";
        oss << "f32";
    } else if (type.element_of() == UInt(1)) {
        oss << "bool";
    } else {
        user_assert(type.bits() == 32) << "WGSL only supports 32-bit integers";
        oss << (type.is_uint() ? "u" : "i") << "32";
    }

    if (type.lanes() != 1) {
        oss << ">";
    }

    if (space == AppendSpace) {
        oss << " ";
    }
    return oss.str();
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::add_kernel(
    const Stmt &s, const string &name, const vector<DeviceArgument> &args) {
    debug(2) << "Adding WGSL shader " << name << "\n";

    // The name of the variable that contains the non-buffer arguments.
    string args_var = "Args_" + name;

    std::ostringstream uniforms;
    uint32_t next_binding = 0;
    for (const DeviceArgument &arg : args) {
        if (arg.is_buffer) {
            // Emit buffer arguments as read_write storage buffers.
            stream << "@group(0) @binding(" << next_binding << ")\n"
                   << "var<storage, read_write> " << print_name(arg.name)
                   << " : array<" << print_type(arg.type) << ">;\n\n";
            Allocation alloc;
            alloc.type = arg.type;
            allocations.push(arg.name, alloc);
            next_binding++;
        } else {
            // Collect non-buffer arguments into a single uniform buffer.
            // TODO: Support non-buffer args with different sizes.
            internal_assert(arg.type.bytes() == 4)
                << "unimplemented: non-buffer args assumed to be 32-bits";
            uniforms << "  " << print_name(arg.name) << " : "
                     << print_type(arg.type) << ";\n";
        }
    }
    if (!uniforms.str().empty()) {
        string struct_name = "ArgsStruct_" + name;
        stream << "struct " << struct_name << " {\n"
               << uniforms.str()
               << "}\n";
        stream << "@group(1) @binding(0)\n"
               << "var<uniform> "
               << args_var << " : " << struct_name << " ;\n\n";
    }

    // Determine the workgroup size.
    // TODO: Remove this and use overridable constants instead.
    struct GetWorkgroupSize : public IRVisitor {
        using IRVisitor::visit;
        void visit(const For *loop) override {
            if (!is_gpu_var(loop->name)) {
                return loop->body.accept(this);
            }
            if (loop->for_type != ForType::GPUThread) {
                return loop->body.accept(this);
            }
            int index = loop_name_to_wgsize_index(loop->name);
            user_assert(index >= 0 && index <= 2)
                << "invalid 'wgsize' index for loop variable '" << loop->name
                << "'.\n";
            const IntImm *limit = loop->extent.as<IntImm>();
            user_assert(limit != nullptr)
                << "dynamic workgroup sizes are not yet supported.";
            user_assert(limit->value > 0)
                << "'" << loop->name << "' must be greater than zero.\n";
            values[index] = limit->value;
            debug(4) << "wgsize[" << index << "] is " << values[index] << "\n";
            loop->body.accept(this);
        }
        int loop_name_to_wgsize_index(const string &name) {
            string ids[] = {
                ".__thread_id_x",
                ".__thread_id_y",
                ".__thread_id_z",
                ".__thread_id_w",
            };
            for (auto &id : ids) {
                if (ends_with(name, id)) {
                    return (&id - ids);
                }
            }
            return -1;
        }
        int values[3] = {1, 1, 1};
    };
    GetWorkgroupSize wgsize;
    s.accept(&wgsize);

    // Emit the function prototype.
    stream << "@stage(compute) @workgroup_size("
           << wgsize.values[0] << ", "
           << wgsize.values[1] << ", "
           << wgsize.values[2] << ")\n";
    stream << "fn " << name << "(\n"
           << "  @builtin(local_invocation_id) local_id : vec3<u32>,\n"
           << "  @builtin(workgroup_id) group_id : vec3<u32>,\n"
           << ")\n";

    open_scope();

    // Redeclare non-buffer arguments at function scope.
    for (const DeviceArgument &arg : args) {
        if (!arg.is_buffer) {
            stream << get_indent() << "let " << print_name(arg.name)
                   << " : " << print_type(arg.type)
                   << " = " << args_var << "." << print_name(arg.name) << ";\n";
        }
    }

    // Generate function body.
    print(s);

    close_scope("shader " + name);
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Allocate *op) {
    if (op->memory_type == MemoryType::GPUShared) {
        // TODO: Implement.
        internal_error << "GPU shared memory is not yet implemented for WebGPU";
    } else {
        open_scope();

        debug(2) << "Allocate " << op->name << " on device\n";

        // Allocation is not a shared memory allocation, just make a local
        // declaration.
        // It must have a constant size.
        int32_t size = op->constant_allocation_size();
        user_assert(size > 0)
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        stream << get_indent() << "var " << print_name(op->name)
               << " : array<" << print_type(op->type) << ", " << size << ">;\n";

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        op->body.accept(this);

        // Should have been freed internally
        internal_assert(!allocations.contains(op->name));

        close_scope("alloc " + print_name(op->name));
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Cast *op) {
    print_assignment(op->type,
                     print_type(op->type) + "(" + print_expr(op->value) + ")");
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const IntImm *op) {
    internal_assert(op->type.bits() == 32)
        << "WGSL only supports 32-bit integers";
    print_assignment(op->type, std::to_string(op->value));
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const UIntImm *op) {
    if (op->type == Bool()) {
        if (op->value == 1) {
            id = "true";
        } else {
            id = "false";
        }
    } else {
        internal_assert(op->type.bits() == 32)
            << "WGSL only supports 32-bit integers";
        print_assignment(op->type, std::to_string(op->value) + "u");
    }
}

namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return "local_id.x";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "local_id.y";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "local_id.z";
    } else if (ends_with(name, ".__thread_id_w")) {
        user_error << "WebGPU does not support more than three dimensions.\n";
    } else if (ends_with(name, ".__block_id_x")) {
        return "group_id.x";
    } else if (ends_with(name, ".__block_id_y")) {
        return "group_id.y";
    } else if (ends_with(name, ".__block_id_z")) {
        return "group_id.z";
    } else if (ends_with(name, ".__block_id_w")) {
        user_error << "WebGPU does not support more than three dimensions.\n";
    }
    internal_error << "invalid simt_intrinsic name: " << name << "\n";
    return "";
}
}  // namespace

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const For *loop) {
    user_assert(loop->for_type != ForType::GPULane)
        << "The WebGPU backend does not support the gpu_lanes() directive.";

    if (is_gpu_var(loop->name)) {
        internal_assert((loop->for_type == ForType::GPUBlock) ||
                        (loop->for_type == ForType::GPUThread))
            << "kernel loop must be either gpu block or gpu thread\n";
        internal_assert(is_const_zero(loop->min));

        stream << get_indent()
               << "let " << print_name(loop->name)
               << " = i32(" << simt_intrinsic(loop->name) << ");\n";

        loop->body.accept(this);

    } else {
        user_assert(loop->for_type == ForType::Serial)
            << "Can only use serial loops inside WebGPU shaders\n";

        string id_min = print_expr(loop->min);
        string id_extent = print_expr(loop->extent);
        string id_counter = print_name(loop->name);
        stream << get_indent() << "for (var "
               << id_counter << " = " << id_min << "; "
               << id_counter << " < " << id_min << " + " << id_extent << "; "
               // TODO: Use increment statement when supported by Chromium.
               << id_counter << " = " << id_counter << " + 1)\n";
        open_scope();
        loop->body.accept(this);
        close_scope("for " + print_name(loop->name));
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Select *op) {
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);
    string select = "select(" + false_val + ", " + true_val + ", " + cond + ")";
    print_assignment(op->type, select);
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_assignment(
    Type t, const std::string &rhs) {
    auto cached = cache.find(rhs);
    if (cached == cache.end()) {
        id = unique_name('_');
        stream << get_indent() << "let " << id << " : " << print_type(t)
               << " = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_WebGPU_Dev(const Target &target) {
    return std::make_unique<CodeGen_WebGPU_Dev>(target);
}

}  // namespace Internal
}  // namespace Halide
