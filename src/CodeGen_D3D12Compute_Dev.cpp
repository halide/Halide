#include <sstream>
#include <algorithm>

#include "CodeGen_D3D12Compute_Dev.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::sort;

static ostringstream nil;

CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_Dev(Target t) :
    d3d12compute_c(src_stream, t) {
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_type_maybe_storage(Type type, bool storage, AppendSpaceIfNeeded space) {
    ostringstream oss;

    // Storage uses packed vector types.
    if (storage && type.lanes() != 1)
    {
        // TODO(marcos): HLSL has 'packoffset', which only applies to constant buffer fields (constant registers (c))
    }
    if (type.is_float())
    {
        switch (type.bits())
        {
            case 16:
                // 16-bit floating point value. This data type is provided only for language compatibility.
                // Direct3D 10 shader targets map all half data types to float data types.
                // A half data type cannot be used on a uniform global variable (use the /Gec flag if this functionality is desired).
                oss << "half";
                break;
            case 32:
                oss << "float";
                break;
            case 64:
                // 64-bit floating point value. You cannot use double precision values as inputs and outputs for a stream.
                // To pass double precision values between shaders, declare each double as a pair of uint data types.
                // Then, use the asdouble function to pack each double into the pair of uints and the asuint function to unpack the pair of uints back into the double.
                oss << "double";
                break;
            default:
                user_error << "Can't represent a float with this many bits in HLSL (SM 5.1): " << type << "\n";
        }
    }
    else
    {
        switch (type.bits())
        {
            case 1:
                oss << "bool";
                break;
            case 8:
            case 16:
            case 32:
                if (type.is_uint()) oss << 'u';
                oss << "int";
                break;
            case 64:
                user_error << "HLSL (SM 5.1) does not support 64-bit integers.\n";
                break;
            default:
                user_error << "Can't represent an integer with this many bits in HLSL (SM 5.1): " << type << "\n";
        }
    }
    switch (type.lanes())
    {
        case 1:
            break;
        case 2:
        case 3:
        case 4:
            oss << type.lanes();
            break;
        case 8:
        case 16:
            // TODO(marcos): are there 8-wide and 16-wide types in HLSL?
            // (CodeGen_GLSLBase seems to happily generate invalid vector types)
        default:
            user_error <<  "Unsupported vector width in HLSL (SM 5.1): " << type << "\n";
    }
    if (space == AppendSpace)
    {
        oss << ' ';
    }
    return oss.str();
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_type(Type type, AppendSpaceIfNeeded space) {
    return print_type_maybe_storage(type, false, space);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_storage_type(Type type) {
    return print_type_maybe_storage(type, true, DoNotAppendSpace);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_reinterpret(Type type, Expr e) {
    ostringstream oss;
    // TODO(marcos): remove the 'lanes' suffix when printing the type here
    oss << "as" << print_type(type) << "(" << print_expr(e) << ")";
    return oss.str();
}



namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return "tid_in_tgroup.x";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "tid_in_tgroup.y";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "tid_in_tgroup.z";
    } else if (ends_with(name, ".__thread_id_w")) {
        user_error << "HLSL (SM5.1) does not support more than three dimensions for compute kernel threads.\n";
    } else if (ends_with(name, ".__block_id_x")) {
        return "tgroup_index.x";
    } else if (ends_with(name, ".__block_id_y")) {
        return "tgroup_index.y";
    } else if (ends_with(name, ".__block_id_z")) {
        return "tgroup_index.z";
    } else if (ends_with(name, ".__block_id_w")) {
        user_error << "HLSL (SM5.1) does not support more than three dimensions for compute dispatch groups.\n";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Evaluate *op) {
    if (is_const(op->value)) return;
    print_expr(op->value);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_extern_call(const Call *op) {
    internal_assert(!function_takes_user_context(op->name));

    vector<string> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
    }
    ostringstream rhs;
    rhs << op->name << "(" << with_commas(args) << ")";
    return rhs.str();
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " >> " << bits;
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " & " << ((1 << bits)-1);
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const For *loop)
{
    if (!is_gpu_var(loop->name))
    {
        user_assert(loop->for_type != ForType::Parallel) << "Cannot use parallel loops inside D3D12Compute kernel\n";
        CodeGen_C::visit(loop);
        return;
    }

    internal_assert((loop->for_type == ForType::GPUBlock) ||
                    (loop->for_type == ForType::GPUThread))
        << "kernel loop must be either gpu block or gpu thread\n";
    internal_assert(is_zero(loop->min));

    do_indent();
    stream << print_type(Int(32)) << " " << print_name(loop->name)
            << " = " << simt_intrinsic(loop->name) << ";\n";

    loop->body.accept(this);
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Ramp *op) {
    ostringstream rhs;
    rhs << print_expr(op->base)
        << " + "
        << print_expr(op->stride)
        << " * "
        << print_type(op->type.with_lanes(op->lanes))
        << "(";

    rhs << "0";
    for (int i = 1; i < op->lanes; ++i) {
        rhs << ", " << i;
    }

    rhs << ")";
    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);

    ostringstream rhs;
    rhs << print_type(op->type.with_lanes(op->lanes)) << "(" << id_value << ")";

    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

namespace {

// If e is a ramp expression with stride 1, return the base, otherwise undefined.
Expr is_ramp_one(Expr e) {
    const Ramp *r = e.as<Ramp>();
    if (r == nullptr) {
        return Expr();
    }

    if (is_one(r->stride)) {
        return r->base;
    }

    return Expr();
}
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Load *op) {
    user_assert(is_one(op->predicate)) << "Predicated load is not supported inside D3D12Compute kernel.\n";

    // If we're loading a contiguous ramp, load from a vector type pointer.
    Expr ramp_base = is_ramp_one(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->type.is_vector());

        ostringstream rhs;
        rhs << "("
            << print_name(op->name)
            << " + "
            << print_expr(ramp_base)
            << ")";
        print_assignment(op->type, rhs.str());

        return;
    }

    string id_index = print_expr(op->index);

    // Get the rhs just for the cache.
    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name).type == op->type);
    ostringstream rhs;
    if (type_cast_needed) {
        rhs << print_storage_type(op->type) << "("
            << print_name(op->name)
            << ")";
    } else {
        rhs << print_name(op->name);
    }
    rhs << "[" << id_index << "]";

    std::map<string, string>::iterator cached = cache.find(rhs.str());
    if (cached != cache.end()) {
        id = cached->second;
        return;
    }

    if (op->index.type().is_vector()) {
        // If index is a vector, gather vector elements.
        internal_assert(op->type.is_vector());

        // This has to be underscore as print_name prepends and
        // underscore to names without one and that results in a name
        // mismatch of a Load appears as the value of a Let.
        id = unique_name('_');
        cache[rhs.str()] = id;

        do_indent();
        stream << print_type(op->type)
               << " " << id << ";\n";

        for (int i = 0; i < op->type.lanes(); ++i) {
            do_indent();
            stream << id << "[" << i << "] = "
                   << print_type(op->type.element_of())
                   << "(" << print_name(op->name) << ")"
                   << "[" << id_index << "[" << i << "]];\n";
        }
    } else {
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Store *op) {
    user_assert(is_one(op->predicate)) << "Predicated store is not supported inside D3D12Compute kernel.\n";

    string id_value = print_expr(op->value);
    Type t = op->value.type();

    // If we're writing a contiguous ramp, store through a pointer of vector type.
    Expr ramp_base = is_ramp_one(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());

        do_indent();
        stream << "("
               << print_name(op->name)
               << " + "
               << print_expr(ramp_base)
               << ") = "
               << id_value
               << ";\n";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());

        string id_index = print_expr(op->index);

        for (int i = 0; i < t.lanes(); ++i) {
            do_indent();
            stream << "("
                   << print_name(op->name)
                   << ")["
                   << id_index << "[" << i << "]] = "
                   << id_value << "[" << i << "];\n";
        }
    } else {
        bool type_cast_needed = !(allocations.contains(op->name) &&
                                  allocations.get(op->name).type == t);

        string id_index = print_expr(op->index);
        string id_value = print_expr(op->value);
        do_indent();

        if (type_cast_needed) {
            stream << "("
                   << print_name(op->name)
                   << ")";
        } else {
            stream << print_name(op->name);
        }
        stream << "[" << id_index << "] = "
               << id_value << ";\n";
    }

    cache.clear();
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Select *op) {
    ostringstream rhs;
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);
    rhs << print_type(op->type)
        << "("   << cond
        << " ? " << true_val
        << " : " << false_val
        << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Allocate *op) {

    if (op->name == "__shared") {
        // Already handled
        op->body.accept(this);
    } else {
        open_scope();

        debug(2) << "Allocate " << op->name << " on device\n";

        debug(3) << "Pushing allocation called " << op->name << " onto the symbol table\n";

        // Allocation is not a shared memory allocation, just make a local declaration.
        // It must have a constant size.
        int32_t size = op->constant_allocation_size();
        user_assert(size > 0)
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        do_indent();
        stream << print_storage_type(op->type) << ' '
               << print_name(op->name) << "[" << size << "];\n";
        do_indent();

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        op->body.accept(this);

        // Should have been freed internally
        internal_assert(!allocations.contains(op->name));

        close_scope("alloc " + print_name(op->name));
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Free *op) {
    if (op->name == "__shared") {
        return;
    } else {
        // Should have been freed internally
        internal_assert(allocations.contains(op->name));
        allocations.pop(op->name);
        do_indent();
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Cast *op) {
    print_assignment(op->type, print_type(op->type) + "(" + print_expr(op->value) + ")");
}

void CodeGen_D3D12Compute_Dev::add_kernel(Stmt s,
                                   const string &name,
                                   const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_D3D12Compute_Dev::compile " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    d3d12compute_c.add_kernel(s, name, args);
}

namespace {
struct BufferSize {
    string name;
    size_t size;

    BufferSize() : size(0) {}
    BufferSize(string name, size_t size) : name(name), size(size) {}

    bool operator < (const BufferSize &r) const {
        return size < r.size;
    }
};
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::add_kernel(Stmt s,
                                                    const string &name,
                                                    const vector<DeviceArgument> &args) {

    debug(2) << "Adding D3D12Compute kernel " << name << "\n";

    // Figure out which arguments should be passed in constant.
    // Such arguments should be:
    // - not written to,
    // - loads are block-uniform,
    // - constant size,
    // - and all allocations together should be less than the max constant
    //   buffer size given by D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT*(4*sizeof(float))
    // The last condition is handled via the preprocessor in the kernel
    // declaration.
    vector<BufferSize> constants;
    auto isConstantBuffer = [&s](const DeviceArgument& arg)
    {
        return arg.is_buffer
            && CodeGen_GPU_Dev::is_buffer_constant(s, arg.name)
            && arg.size > 0;
    };
    for (auto& arg : args)
    {
        if (isConstantBuffer(arg))
        {
            constants.push_back(BufferSize(arg.name, arg.size));
        }
    }

    // Sort the constant candidates from smallest to largest. This will put
    // as many of the constant allocations in constant as possible.
    // Ideally, we would prioritize constant buffers by how frequently they
    // are accessed.
    sort(constants.begin(), constants.end());

    // Compute the cumulative sum of the constants.
    for (size_t i = 1; i < constants.size(); i++)
    {
        constants[i].size += constants[i-1].size;
    }

    // Find all the shared allocations and declare them at global scope.
    class FindSharedAllocations : public IRVisitor
    {
        using IRVisitor::visit;
        void visit(const Allocate* op)
        {
            op->body.accept(this);
            if (starts_with(op->name, "__shared"))
            {
                allocs.push_back(op);
            }
        }
        friend class CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C;
        vector<const Allocate *> allocs;
    };
    FindSharedAllocations fsa;
    s.accept(&fsa);
    for (const Allocate* op : fsa.allocs)
    {
        internal_assert(op->extents.size() == 1 && is_const(op->extents[0]));
        stream << "groupshared"
               << " "  << print_type(op->type)
               << " "  << print_name(op->name)
               << " [" << op->extents[0] << "];\n";
        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);
    }

    // Emit the kernel function prototype

    // Figure out the thread group size by traversing the stmt:
    class FindThreadGroupSize : public IRVisitor
    {
        using IRVisitor::visit;
        void visit(const For* loop)
        {
            if (!is_gpu_var(loop->name))
                return loop->body.accept(this);
            if (loop->for_type != ForType::GPUThread)
                return loop->body.accept(this);
            internal_assert(is_zero(loop->min));
            int index = thread_loop_workgroup_index(loop->name);
            user_assert(index >= 0) << "Invalid 'numthreads' index for loop variable '" << loop->name << "'.\n";
            const IntImm* int_limit = loop->extent.as<IntImm>();
            user_assert(int_limit != nullptr) << "For D3D12Compute, 'numthreads[" << index << "]' must be a constant integer.\n";
            numthreads[index] = int_limit->value;
            user_assert(numthreads[index] > 0) << "For D3D12Compute, 'numthreads[" << index << "]' values must be greater than zero.\n";
            debug(4) << "Thread group size for index " << index << " is " << numthreads[index] << "\n";
            loop->body.accept(this);
        }
        int thread_loop_workgroup_index(const string &name)
        {
            string ids[] = { ".__thread_id_x",
                             ".__thread_id_y",
                             ".__thread_id_z",
                             ".__thread_id_w" };
            for (auto& id : ids)
            {
                if (ends_with(name, id))
                {
                    return (&id - ids);
                }
            }
            return -1;
        }
        friend class CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C;
        int numthreads [3] = { 1, 1, 1 };
    };
    FindThreadGroupSize ftg;
    s.accept(&ftg);
    stream << "[ numthreads("
           << " "  << ftg.numthreads[0]
           << ", " << ftg.numthreads[1]
           << ", " << ftg.numthreads[2]
           << " ) ]\n";

    stream << "void " << name << "(\n";
    stream << "uint3 tgroup_index  : SV_GroupID,\n"
           << "uint3 tid_in_tgroup : SV_GroupThreadID";
    for (auto& arg : args)
    {
        if (arg.is_buffer)
        {
            // NOTE(marcos): I guess there is no point in having "const" Buffer
            // or const RWBuffer; storage type also does not make sense here...
            stream << ",\n";
            // NOTE(marcos): Passing all buffers as RWBuffers in order to bind
            // all buffers as UAVs since there is no way the runtime can know
            // if a given halide_buffer_t is read-only (SRV) or read-write...
            //if (arg.write) stream << "RW";
            //stream << "Buffer"
            stream << "RWBuffer"
                   << "<" << print_type(arg.type) << ">"
                   << " " << print_name(arg.name);
            Allocation alloc;
            alloc.type = arg.type;
            allocations.push(arg.name, alloc);
        }
        else
        {
            stream << ",\n"
                   << "uniform"
                   << " " << print_type(arg.type)
                   << " " << print_name(arg.name);
        }
    }

    stream << ")\n";

    open_scope();

    print(s);
    close_scope("kernel " + name);

    for (size_t i = 0; i < args.size(); i++) {
        // Remove buffer arguments from allocation scope
        if (args[i].is_buffer) {
            allocations.pop(args[i].name);
        }
    }

    stream << "\n";
}

void CodeGen_D3D12Compute_Dev::init_module() {
    debug(2) << "D3D12Compute device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    // Write out the Halide math functions.
    src_stream 
             //<< "namespace {\n"   // HLSL does not support unnamed namespaces...
               << "#define float_from_bits asfloat \n"
             // NOTE(marcos): there is neither NAN nor INF keywords in HLSL,
             //               and I don't feel comfortable bit-hacking here...
             //<< "constexpr float nan_f32()     { return as_type<float>(0x7fc00000);  } \n" // Quiet NaN with minimum fractional value.
             //<< "constexpr float neg_inf_f32() { return float_from_bits(0xff800000); } \n"
             //<< "constexpr float inf_f32()     { return float_from_bits(0x7f800000); } \n"
               << "#define sqrt_f32  sqrt  \n"
               << "#define sin_f32   sin   \n"
               << "#define cos_f32   cos   \n"
               << "#define exp_f32   exp   \n"
               << "#define log_f32   log   \n"
               << "#define abs_f32   abs   \n"
               << "#define floor_f32 floor \n"
               << "#define ceil_f32  ceil  \n"
               << "#define round_f32 round \n"
               << "#define trunc_f32 trunc \n"
               << "#define pow_f32   pow   \n"
               << "#define asin_f32  asin  \n"
               << "#define acos_f32  acos  \n"
               << "#define tan_f32   tan   \n"
               << "#define atan_f32  atan  \n"
               << "#define atan2_f32 atan2 \n"
               << "#define sinh_f32  sinh  \n"
               << "#define asinh_f32 asinh \n"
               << "#define cosh_f32  cosh  \n"
               << "#define acosh_f32 acosh \n"
               << "#define tanh_f32  tanh  \n"
             //<< "#define atanh_f32 atanh \n"
               << "#define fast_inverse_f32      rcp   \n"
               << "#define fast_inverse_sqrt_f32 rsqrt \n"
             // Halide only ever needs threadgroup memory fences:
             // NOTE(marcos): using "WithGroupSync" here just to be safe, as a
             // simple "GroupMemoryBarrier" is probably too relaxed for Halide
             // (also note we need to return an integer)
               << "\n"
               << "int halide_gpu_thread_barrier() \n"
                  "{ \n"
                  "  GroupMemoryBarrierWithGroupSync(); \n"
                  "  return 0; \n"
                  "} \n"
               << "\n";
             //<< "}\n"; // close namespace

    src_stream << '\n';

    cur_kernel_name = "";
}

vector<char> CodeGen_D3D12Compute_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "D3D12Compute kernel:\n" << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_D3D12Compute_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_D3D12Compute_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

std::string CodeGen_D3D12Compute_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}}
