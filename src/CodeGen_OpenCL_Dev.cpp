#include <sstream>
#include <algorithm>

#include "CodeGen_OpenCL_Dev.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "IROperator.h"
#include "IRMutator.h"
#include "EliminateBoolVectors.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::sort;
using std::to_string;

CodeGen_OpenCL_Dev::CodeGen_OpenCL_Dev(Target t) :
    clc(src_stream, t), target(t) {
}

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::print_type(Type type, AppendSpaceIfNeeded space) {
    ostringstream oss;
    if (type.is_float()) {
        if (type.bits() == 16) {
            oss << "half";
        } else if (type.bits() == 32) {
            oss << "float";
        } else if (type.bits() == 64) {
            oss << "double";
        } else {
            user_error << "Can't represent a float with this many bits in OpenCL C: " << type << "\n";
        }

    } else {
        if (type.is_uint() && type.bits() > 1) oss << 'u';
        switch (type.bits()) {
        case 1:
            internal_assert(type.lanes() == 1) << "Encountered vector of bool\n";
            oss << "bool";
            break;
        case 8:
          oss << "char";
            break;
        case 16:
            oss << "short";
            break;
        case 32:
            oss << "int";
            break;
        case 64:
            oss << "long";
            break;
        default:
            user_error << "Can't represent an integer with this many bits in OpenCL C: " << type << "\n";
        }
    }
    if (type.lanes() != 1) {
        switch (type.lanes()) {
        case 2:
        case 3:
        case 4:
        case 8:
        case 16:
            oss << type.lanes();
            break;
        default:
            user_error <<  "Unsupported vector width in OpenCL C: " << type << "\n";
        }
    }
    if (space == AppendSpace) {
        oss << ' ';
    }
    return oss.str();
}

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::print_reinterpret(Type type, Expr e) {
    ostringstream oss;
    oss << "as_" << print_type(type) << "(" << print_expr(e) << ")";
    return oss.str();
}



namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return "get_local_id(0)";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "get_local_id(1)";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "get_local_id(2)";
    } else if (ends_with(name, ".__thread_id_w")) {
        return "get_local_id(3)";
    } else if (ends_with(name, ".__block_id_x")) {
        return "get_group_id(0)";
    } else if (ends_with(name, ".__block_id_y")) {
        return "get_group_id(1)";
    } else if (ends_with(name, ".__block_id_z")) {
        return "get_group_id(2)";
    } else if (ends_with(name, ".__block_id_w")) {
        return "get_group_id(3)";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        internal_assert(loop->for_type == ForType::Parallel) << "kernel loop must be parallel\n";
        internal_assert(is_zero(loop->min));

        do_indent();
        stream << print_type(Int(32)) << " " << print_name(loop->name)
               << " = " << simt_intrinsic(loop->name) << ";\n";

        loop->body.accept(this);

    } else {
        user_assert(loop->for_type != ForType::Parallel) << "Cannot use parallel loops inside OpenCL kernel\n";
        CodeGen_C::visit(loop);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Ramp *op) {
    string id_base = print_expr(op->base);
    string id_stride = print_expr(op->stride);

    ostringstream rhs;
    rhs << id_base << " + " << id_stride << " * ("
        << print_type(op->type.with_lanes(op->lanes)) << ")(0";
    // Note 0 written above.
    for (int i = 1; i < op->lanes; ++i) {
        rhs << ", " << i;
    }
    rhs << ")";
    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);

    print_assignment(op->type.with_lanes(op->lanes), id_value);
}

namespace {
// Mapping of integer vector indices to OpenCL ".s" syntax.
const char * vector_elements = "0123456789ABCDEF";

// If e is a ramp expression with stride 1, return the base, otherwise undefined.
Expr is_ramp1(Expr e) {
    const Ramp *r = e.as<Ramp>();
    if (r == nullptr) {
        return Expr();
    }

    const IntImm *i = r->stride.as<IntImm>();
    if (i != nullptr && i->value == 1) {
        return r->base;
    }

    return Expr();
}
}


string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::get_memory_space(const string &buf) {
    return "__address_space_" + print_name(buf);
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Call *op) {
    if (op->is_intrinsic(Call::interleave_vectors)) {
        int op_lanes = op->type.lanes();
        internal_assert(op->args.size() > 0);
        int arg_lanes = op->args[0].type().lanes();
        if (op->args.size() == 1) {
            // 1 argument, just do a simple assignment
            internal_assert(op_lanes == arg_lanes);
            print_assignment(op->type, print_expr(op->args[0]));
        } else if (op->args.size() == 2) {
            // 2 arguments, set the .even to the first arg and the
            // .odd to the second arg
            internal_assert(op->args[1].type().lanes() == arg_lanes);
            internal_assert(op_lanes / 2 == arg_lanes);
            string a1 = print_expr(op->args[0]);
            string a2 = print_expr(op->args[1]);
            id = unique_name('_');
            do_indent();
            stream << print_type(op->type) << " " << id << ";\n";
            do_indent();
            stream << id << ".even = " << a1 << ";\n";
            do_indent();
            stream << id << ".odd = " << a2 << ";\n";
        } else {
            // 3+ arguments, interleave via a vector literal
            // selecting the appropriate elements of the args
            int dest_lanes = op->type.lanes();
            internal_assert(dest_lanes <= 16);
            int num_args = op->args.size();
            vector<string> arg_exprs(num_args);
            for (int i = 0; i < num_args; i++) {
                internal_assert(op->args[i].type().lanes() == arg_lanes);
                arg_exprs[i] = print_expr(op->args[i]);
            }
            internal_assert(num_args * arg_lanes >= dest_lanes);
            id = unique_name('_');
            do_indent();
            stream << print_type(op->type) << " " << id;
            stream << " = (" << print_type(op->type) << ")(";
            for (int i = 0; i < dest_lanes; i++) {
                int arg = i % num_args;
                int arg_idx = i / num_args;
                internal_assert(arg_idx <= arg_lanes);
                stream << arg_exprs[arg] << ".s" << vector_elements[arg_idx];
                if (i != dest_lanes - 1) {
                    stream << ", ";
                }
            }
            stream << ");\n";
        }
    } else if (op->name == Call::image_load) {
        // image_load(<image name>, <buffer>, <x>, <x-extent>, <y>,
        // <y-extent>, <c>, <c-extent>)
        internal_assert(op->args.size() == 6 || op->args.size() == 8); // 2D and 3D or is it always normalized to 3D?
        // string_imm is the name of the image being read from
        const StringImm *string_imm = op->args[0].as<StringImm>();
        if (!string_imm) {
            internal_assert(op->args[0].as<Broadcast>());
            string_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
        }
        internal_assert(string_imm);
        bool is_2d_array = op->args.size() == 8;
        string arg1 = print_expr(op->args[2]);
        string arg2 = print_expr(op->args[4]);
        string arg3 = is_2d_array ? print_expr(op->args[6]) : "";
        Type arg_type = op->args[2].type();
        vector<string> results;
        Type return_type;
        // If doing a vector read_image, flatten into a sequence of
        // read_image calls
        for (int i = 0; i < arg_type.lanes(); i++) {
            string x = arg1;
            string y = arg2;
            string z = arg3;
            if (arg_type.is_vector()) {
                x = print_assignment(arg_type.element_of(), arg1 + ".s" + to_string(i));
                y = print_assignment(arg_type.element_of(), arg2 + ".s" + to_string(i));
                if (is_2d_array) {
                    z = print_assignment(arg_type.element_of(), arg3 + ".s" + to_string(i));
                }
            }
            // Codegen the read_image call
            ostringstream rhs;
            rhs << "read_image";
            if (op->type.is_float()) {
                rhs << "f";
                return_type = Type(Type::Float, 32, 4);
            } else if (op->type.is_int()) {
                rhs << "i";
                return_type = Type(Type::Int, 32, 4);
            } else if (op->type.is_uint()) {
                rhs << "ui";
                return_type = Type(Type::UInt, 32, 4);
            } else {
                internal_error << "Unexpected type for read_image\n";
            }
            rhs << "(" << print_name(string_imm->value) << ", sampler, ";
            if (is_2d_array) {
                rhs << "(int4)(" << x << ", " << y << ", " << z << ", 0)";
            } else {
                rhs << "(int2)(" << x << ", " << y << ")";
            }
            rhs << ")";
            print_assignment(return_type, rhs.str());
            // Get the first value (because it returns a vector)
            print_assignment(return_type.element_of(), id + ".x");
            results.push_back(id);
        }
        // Convert to the correct type if necessary
        if (return_type != op->type) {
            string operand = id;
            if (op->type.is_vector()) {
                internal_assert(op->type.lanes() == (int)results.size());
                ostringstream operand_vector;
                operand_vector << "("
                               << print_type(return_type.with_lanes(op->type.lanes()))
                               << ")(";
                for (int i = 0; i < op->type.lanes(); i++) {
                    operand_vector << results[i];
                    if (i < op->type.lanes() - 1) {
                        operand_vector << ", ";
                    }
                }
                operand_vector << ")";
                id = operand_vector.str();
            }
            print_assignment(op->type, "convert_" + print_type(op->type) + "(" + id + ")");
        }
    } else if (op->name == Call::image_store) {
        // image_store(<image name>, <buffer>, <x>, <y>, <c>, <value>)
        internal_assert(op->args.size() == 5 || op->args.size() == 6); // 2D and 3D or is it always normalized to 3D?
        // string_imm is the name of the image being written to
        const StringImm *string_imm = op->args[0].as<StringImm>();
        if (!string_imm) {
            internal_assert(op->args[0].as<Broadcast>());
            string_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
        }
        internal_assert(string_imm);
        bool is_2d_array = op->args.size() == 6;
        string arg1 = print_expr(op->args[2]);
        string arg2 = print_expr(op->args[3]);
        string arg3 = print_expr(op->args[4]);
        string arg4 = is_2d_array ? print_expr(op->args[5]) : arg3;
        Type arg_type = op->args[2].type();
        Type value_type = op->args.back().type();
        internal_assert(arg_type.lanes() == value_type.lanes());
        // If doing a write_image with a vector, flatten into a
        // sequence of write_image calls
        for (int i = 0; i < arg_type.lanes(); i++) {
            string x = arg1;
            string y = arg2;
            string z = arg3;
            string value = arg4;
            if (arg_type.is_vector()) {
                x = print_assignment(arg_type.element_of(), arg1 + ".s" + to_string(i));
                y = print_assignment(arg_type.element_of(), arg2 + ".s" + to_string(i));
                if (is_2d_array) {
                    z = print_assignment(arg_type.element_of(), arg3 + ".s" + to_string(i));
                    value = print_assignment(value_type.element_of(), arg4 + ".s" + to_string(i));
                    if (value_type.bits() != 32) {
                        Type converted_value_type = value_type.with_bits(32);
                        value = print_assignment(converted_value_type,
                                                 "convert_"
                                                 + print_type(converted_value_type)
                                                 + "(" + value + ")");
                    }
                } else {
                    value = print_assignment(value_type.element_of(), arg3 + ".s" + to_string(i));
                }
            } else if (value_type.bits() != 32) {
                Type converted_value_type = value_type.with_bits(32);
                value = print_assignment(converted_value_type,
                                         "convert_" + print_type(converted_value_type)
                                         + "(" + value + ")");
            }
            // Codegen the write_image call
            do_indent();
            stream << "write_image";
            Type color_type;
            if (value_type.is_float()) {
                stream << "f";
                color_type = Type(Type::Float, 32, 4);
            } else if (value_type.is_int()) {
                stream << "i";
                color_type = Type(Type::Int, 32, 4);
            } else if (value_type.is_uint()) {
                stream << "ui";
                color_type = Type(Type::UInt, 32, 4);
            } else {
                internal_error << "Unexpected type for write_image\n";
            }
            stream << "(" << print_name(string_imm->value) << ", ";
            if (is_2d_array) {
                stream << "(int4)(" << x << ", " << y << ", " << z << ", 0)";
            } else {
                stream << "(int2)(" << x << ", " << y << ")";
            }
            stream << ", (" << print_type(color_type) << ")(" << value << ", 0, 0, 0));\n";
        }
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Load *op) {
    // If we're loading a contiguous ramp into a vector, use vload instead.
    Expr ramp_base = is_ramp1(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->type.is_vector());
        string id_ramp_base = print_expr(ramp_base);

        ostringstream rhs;
        rhs << "vload" << op->type.lanes()
            << "(0, (" << get_memory_space(op->name) << " "
            << print_type(op->type.element_of()) << "*)"
            << print_name(op->name) << " + " << id_ramp_base << ")";

        print_assignment(op->type, rhs.str());
        return;
    }

    string id_index = print_expr(op->index);

    // Get the rhs just for the cache.
    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name).type == op->type);
    ostringstream rhs;
    if (type_cast_needed) {
        rhs << "((" << get_memory_space(op->name) << " "
            << print_type(op->type) << " *)"
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

        id = "_" + unique_name('V');
        cache[rhs.str()] = id;

        do_indent();
        stream << print_type(op->type)
               << " " << id << ";\n";

        for (int i = 0; i < op->type.lanes(); ++i) {
            do_indent();
            stream
                << id << ".s" << vector_elements[i]
                << " = ((" << get_memory_space(op->name) << " "
                << print_type(op->type.element_of()) << "*)"
                << print_name(op->name) << ")"
                << "[" << id_index << ".s" << vector_elements[i] << "];\n";
        }
    } else {
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Store *op) {
    string id_value = print_expr(op->value);
    Type t = op->value.type();

    // If we're writing a contiguous ramp, use vstore instead.
    Expr ramp_base = is_ramp1(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());
        string id_ramp_base = print_expr(ramp_base);

        do_indent();
        stream << "vstore" << t.lanes() << "("
               << id_value << ","
               << 0 << ", (" << get_memory_space(op->name) << " "
               << print_type(t.element_of()) << "*)"
               << print_name(op->name) << " + " << id_ramp_base
               << ");\n";

    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());

        string id_index = print_expr(op->index);

        for (int i = 0; i < t.lanes(); ++i) {
            do_indent();
            stream << "((" << get_memory_space(op->name) << " "
                   << print_type(t.element_of()) << " *)"
                   << print_name(op->name)
                   << ")["
                   << id_index << ".s" << vector_elements[i] << "] = "
                   << id_value << ".s" << vector_elements[i] << ";\n";
        }
    } else {
        bool type_cast_needed = !(allocations.contains(op->name) &&
                                  allocations.get(op->name).type == t);

        string id_index = print_expr(op->index);
        string id_value = print_expr(op->value);
        do_indent();

        if (type_cast_needed) {
            stream << "(("
                   << get_memory_space(op->name) << " "
                   << print_type(t)
                   << " *)"
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

namespace {
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const EQ *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, "==");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const NE *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, "!=");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const LT *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, "<");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const LE *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, "<=");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const GT *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, ">");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const GE *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, ">=");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Cast *op) {
    if (op->type.is_vector()) {
        print_assignment(op->type, "convert_" + print_type(op->type) + "(" + print_expr(op->value) + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Select *op) {
    if (op->condition.type().is_vector()) {
        string true_val = print_expr(op->true_value);
        string false_val = print_expr(op->false_value);
        string cond = print_expr(op->condition);

        // Yes, you read this right. OpenCL's select function is declared
        // 'select(false_case, true_case, condition)'.
        ostringstream rhs;
        rhs << "select(" << false_val << ", " << true_val << ", " << cond << ")";
        print_assignment(op->type, rhs.str());
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Allocate *op) {
    user_assert(!op->new_expr.defined()) << "Allocate node inside OpenCL kernel has custom new expression.\n" <<
        "(Memoization is not supported inside GPU kernels at present.)\n";

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
        stream << print_type(op->type) << ' '
               << print_name(op->name) << "[" << size << "];\n";
        do_indent();
        stream << "#define " << get_memory_space(op->name) << " __private\n";

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        op->body.accept(this);

        // Should have been freed internally
        internal_assert(!allocations.contains(op->name));

        close_scope("alloc " + print_name(op->name));
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Free *op) {
    if (op->name == "__shared") {
        return;
    } else {
        // Should have been freed internally
        internal_assert(allocations.contains(op->name));
        allocations.pop(op->name);
        do_indent();
        stream << "#undef " << get_memory_space(op->name) << "\n";
    }
}


void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const AssertStmt *op) {
    user_warning << "Ignoring assertion inside OpenCL kernel: " << op->condition << "\n";
}

void CodeGen_OpenCL_Dev::add_kernel(Stmt s,
                                    const string &name,
                                    const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_OpenCL_Dev::compile " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    clc.add_kernel(s, name, args);
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

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::add_kernel(Stmt s,
                                                      const string &name,
                                                      const vector<DeviceArgument> &args) {

    debug(2) << "Adding OpenCL kernel " << name << "\n";

    debug(2) << "Eliminating bool vectors\n";
    s = eliminate_bool_vectors(s);
    debug(2) << "After eliminating bool vectors:\n" << s << "\n";

    // Figure out which arguments should be passed in __constant.
    // Such arguments should be:
    // - not written to,
    // - loads are block-uniform,
    // - constant size,
    // - and all allocations together should be less than the max constant
    //   buffer size given by CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE.
    // The last condition is handled via the preprocessor in the kernel
    // declaration.
    vector<BufferSize> constants;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer &&
            CodeGen_GPU_Dev::is_buffer_constant(s, args[i].name) &&
            args[i].size > 0) {
            constants.push_back(BufferSize(args[i].name, args[i].size));
        }
    }

    // Sort the constant candidates from smallest to largest. This will put
    // as many of the constant allocations in __constant as possible.
    // Ideally, we would prioritize constant buffers by how frequently they
    // are accessed.
    sort(constants.begin(), constants.end());

    // Compute the cumulative sum of the constants.
    for (size_t i = 1; i < constants.size(); i++) {
        constants[i].size += constants[i - 1].size;
    }

    // Create preprocessor replacements for the address spaces of all our buffers.
    stream << "// Address spaces for " << name << "\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            vector<BufferSize>::iterator constant = constants.begin();
            while (constant != constants.end() &&
                   constant->name != args[i].name) {
                constant++;
            }

            if (constant != constants.end()) {
                stream << "#if " << constant->size << " < MAX_CONSTANT_BUFFER_SIZE && "
                       << constant - constants.begin() << " < MAX_CONSTANT_ARGS\n";
                stream << "#define " << get_memory_space(args[i].name) << " __constant\n";
                stream << "#else\n";
                stream << "#define " << get_memory_space(args[i].name) << " __global\n";
                stream << "#endif\n";
            } else {
                stream << "#define " << get_memory_space(args[i].name) << " __global\n";
            }
        }
    }

    // Emit the function prototype
    stream << "__kernel void " << name << "(\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            if (target.has_feature(Target::Textures)) {
                if (args[i].read && args[i].write) {
                    stream << " __read_write ";
                } else if (args[i].read) {
                    stream << " __read_only ";
                } else if (args[i].write) {
                    stream << " __write_only ";
                } else {
                    internal_error << "image param " << args[i].name
                                   << " is neither read nor write";
                }
                if (args[i].dimensions == 2) {
                    stream << "image2d_t ";
                } else {
                    internal_assert(args[i].dimensions == 3);
                    stream << "image2d_array_t ";
                }
                stream << print_name(args[i].name);
            } else {
                stream << " " << get_memory_space(args[i].name) << " ";
                if (!args[i].write) stream << "const ";
                stream << print_type(args[i].type) << " *"
                       << print_name(args[i].name);
            }
            Allocation alloc;
            alloc.type = args[i].type;
            allocations.push(args[i].name, alloc);
        } else {
            stream << " const "
                   << print_type(args[i].type)
                   << " "
                   << print_name(args[i].name);
        }

        if (i < args.size()-1) stream << ",\n";
    }
    stream << ",\n" << " __address_space___shared int16* __shared";

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

    // Undef all the buffer address spaces, in case they're different in another kernel.
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << "#undef " << get_memory_space(args[i].name) << "\n";
        }
    }
}

void CodeGen_OpenCL_Dev::init_module() {
    debug(2) << "OpenCL device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    // This identifies the program as OpenCL C (as opposed to SPIR).
    src_stream << "/*OpenCL C*/\n";

    src_stream << "#pragma OPENCL FP_CONTRACT ON\n";

    // Write out the Halide math functions.
    src_stream << "float float_from_bits(unsigned int x) {return as_float(x);}\n"
               << "float nan_f32() { return NAN; }\n"
               << "float neg_inf_f32() { return -INFINITY; }\n"
               << "float inf_f32() { return INFINITY; }\n"
               << "#define sqrt_f32 sqrt \n"
               << "#define sin_f32 sin \n"
               << "#define cos_f32 cos \n"
               << "#define exp_f32 exp \n"
               << "#define log_f32 log \n"
               << "#define abs_f32 fabs \n"
               << "#define floor_f32 floor \n"
               << "#define ceil_f32 ceil \n"
               << "#define round_f32 round \n"
               << "#define trunc_f32 trunc \n"
               << "#define pow_f32 pow\n"
               << "#define asin_f32 asin \n"
               << "#define acos_f32 acos \n"
               << "#define tan_f32 tan \n"
               << "#define atan_f32 atan \n"
               << "#define atan2_f32 atan2\n"
               << "#define sinh_f32 sinh \n"
               << "#define asinh_f32 asinh \n"
               << "#define cosh_f32 cosh \n"
               << "#define acosh_f32 acosh \n"
               << "#define tanh_f32 tanh \n"
               << "#define atanh_f32 atanh \n"
               << "#define fast_inverse_f32 native_recip \n"
               << "#define fast_inverse_sqrt_f32 native_rsqrt \n"
               << "int halide_gpu_thread_barrier() {\n"
               << "  barrier(CLK_LOCAL_MEM_FENCE);\n" // Halide only ever needs local memory fences.
               << "  return 0;\n"
               << "}\n";

    // __shared always has address space __local.
    src_stream << "#define __address_space___shared __local\n";

    if (target.has_feature(Target::CLDoubles)) {
        src_stream << "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n"
                   << "bool is_nan_f64(double x) {return x != x; }\n"
                   << "#define sqrt_f64 sqrt\n"
                   << "#define sin_f64 sin\n"
                   << "#define cos_f64 cos\n"
                   << "#define exp_f64 exp\n"
                   << "#define log_f64 log\n"
                   << "#define abs_f64 fabs\n"
                   << "#define floor_f64 floor\n"
                   << "#define ceil_f64 ceil\n"
                   << "#define round_f64 round\n"
                   << "#define trunc_f64 trunc\n"
                   << "#define pow_f64 pow\n"
                   << "#define asin_f64 asin\n"
                   << "#define acos_f64 acos\n"
                   << "#define tan_f64 tan\n"
                   << "#define atan_f64 atan\n"
                   << "#define atan2_f64 atan2\n"
                   << "#define sinh_f64 sinh\n"
                   << "#define asinh_f64 asinh\n"
                   << "#define cosh_f64 cosh\n"
                   << "#define acosh_f64 acosh\n"
                   << "#define tanh_f64 tanh\n"
                   << "#define atanh_f64 atanh\n";
    }

    src_stream << '\n';

    // Add at least one kernel to avoid errors on some implementations for functions
    // without any GPU schedules.
    src_stream << "__kernel void _at_least_one_kernel(int x) { }\n";

    src_stream << "__constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE"
               << "| CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;\n";

    cur_kernel_name = "";
}

vector<char> CodeGen_OpenCL_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "OpenCL kernel:\n" << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_OpenCL_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenCL_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

std::string CodeGen_OpenCL_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}}
