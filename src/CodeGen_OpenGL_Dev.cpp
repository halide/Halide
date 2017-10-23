#include "CodeGen_OpenGL_Dev.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "Simplify.h"
#include "VaryingAttributes.h"
#include <iomanip>
#include <map>
#include <limits>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::map;

namespace {

bool is_opengl_es(const Target &target) {
    // TODO: we need a better way to switch between the different OpenGL
    // versions (desktop GL, GLES2, GLES3, ...), probably by making it part of
    // Target.
    return (target.os == Target::Android ||
            target.os == Target::IOS);
}

// Maps Halide types to appropriate GLSL types or emit error if no equivalent
// type is available.
Type map_type(const Type &type) {
    Type result = type;
    if (type.is_scalar()) {
        if (type.is_float()) {
            user_assert(type.bits() <= 32)
                << "GLSL: Can't represent a float with " << type.bits() << " bits.\n";
            result = Float(32);
        } else if (type.bits() == 1) {
            result = Bool();
        } else if (type == Int(32)) {
            // Keep unchanged
        } else if (type == UInt(32)) {
            // GLSL doesn't have unsigned types, simply use int.
            result = Int(32);
        } else if (type.bits() <= 16) {
            // Embed all other ints in a GLSL float. Probably not actually
            // valid for uint16 on systems with low float precision.
            result = Float(32);
        } else {
            user_error << "GLSL: Can't represent type '"<< type << "'.\n";
        }
    } else {
        user_assert(type.lanes() <= 4)
            << "GLSL: vector types wider than 4 aren't supported\n";
        user_assert(type.is_bool() || type.is_int() || type.is_uint() || type.is_float())
            << "GLSL: Can't represent vector type '"<< type << "'.\n";
        Type scalar_type = type.element_of();
        result = map_type(scalar_type).with_lanes(type.lanes());
    }
    return result;
}

char get_lane_suffix(int i) {
    internal_assert(i >= 0 && i < 4);
    return "rgba"[i];
}

// Most GLSL builtins are only defined for float arguments, so we may have to
// introduce type casts around the arguments and the entire function call.
Expr call_builtin(const Type &result_type, const string &func,
                  const vector<Expr> &args) {
    Type float_type = Float(32, result_type.lanes());
    vector<Expr> new_args(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        if (!args[i].type().is_float()) {
            new_args[i] = Cast::make(float_type, args[i]);
        } else {
            new_args[i] = args[i];
        }
    }
    Expr val = Call::make(float_type, func, new_args, Call::Extern);
    return simplify(Cast::make(result_type, val));
}

}

CodeGen_OpenGL_Dev::CodeGen_OpenGL_Dev(const Target &target)
    : target(target) {
    debug(1) << "Creating GLSL codegen\n";
    glc = new CodeGen_GLSL(src_stream, target);
}

CodeGen_OpenGL_Dev::~CodeGen_OpenGL_Dev() {
    delete glc;
}

void CodeGen_OpenGL_Dev::add_kernel(Stmt s, const string &name,
                                    const vector<DeviceArgument> &args) {
    cur_kernel_name = name;
    glc->add_kernel(s, name, args);
}

void CodeGen_OpenGL_Dev::init_module() {
    src_stream.str("");
    src_stream.clear();
    cur_kernel_name = "";
}

vector<char> CodeGen_OpenGL_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "GLSL source:\n" << str << '\n';
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_OpenGL_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenGL_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

string CodeGen_OpenGL_Dev::print_gpu_name(const string &name) {
    return glc->print_name(name);
}

//
// CodeGen_GLSLBase
//
CodeGen_GLSLBase::CodeGen_GLSLBase(std::ostream &s, Target target) : CodeGen_C(s, target) {
    builtin["sin_f32"] = "sin";
    builtin["sqrt_f32"] = "sqrt";
    builtin["cos_f32"] = "cos";
    builtin["exp_f32"] = "exp";
    builtin["log_f32"] = "log";
    builtin["abs_f32"] = "abs";
    builtin["floor_f32"] = "floor";
    builtin["ceil_f32"] = "ceil";
    builtin["pow_f32"] = "pow";
    builtin["asin_f32"] = "asin";
    builtin["acos_f32"] = "acos";
    builtin["tan_f32"] = "tan";
    builtin["atan_f32"] = "atan";
    builtin["atan2_f32"] = "atan"; // also called atan in GLSL
    builtin["min"] = "min";
    builtin["max"] = "max";
    builtin["mix"] = "mix";
    builtin["mod"] = "mod";
    builtin["abs"] = "abs";
    builtin["isnan"] = "isnan";
    builtin["round_f32"] = "roundEven";
    builtin["trunc_f32"] = "_trunc_f32";

    // functions that produce bvecs
    builtin["equal"] = "equal";
    builtin["notEqual"] = "notEqual";
    builtin["lessThan"] = "lessThan";
    builtin["lessThanEqual"] = "lessThanEqual";
    builtin["greaterThan"] = "greaterThan";
    builtin["greaterThanEqual"] = "greaterThanEqual";
}

void CodeGen_GLSLBase::visit(const Max *op) {
    print_expr(call_builtin(op->type, "max", {op->a, op->b}));
}

void CodeGen_GLSLBase::visit(const Min *op) {
    print_expr(call_builtin(op->type, "min", {op->a, op->b}));
}

void CodeGen_GLSLBase::visit(const Div *op) {
    if (op->type.is_int()) {
        // Halide's integer division is defined to round down. Since the
        // rounding behavior of GLSL's integer division is undefined, emulate
        // the correct behavior using floating point arithmetic.
        Type float_type = Float(32, op->type.lanes());
        Expr val = Div::make(Cast::make(float_type, op->a), Cast::make(float_type, op->b));
        print_expr(call_builtin(op->type, "floor_f32", {val}));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_GLSLBase::visit(const Mod *op) {
    print_expr(call_builtin(op->type, "mod", {op->a, op->b}));
}

void CodeGen_GLSLBase::visit(const Call *op) {
    ostringstream rhs;
    if (builtin.count(op->name) == 0) {
        user_error << "GLSL: unknown function '" << op->name << "' encountered.\n";
    }

    rhs << builtin[op->name] << "(";
    for (size_t i = 0; i < op->args.size(); i++) {
        if (i > 0) rhs << ", ";
        rhs << print_expr(op->args[i]);
    }
    rhs << ")";
    print_assignment(op->type, rhs.str());
}

string CodeGen_GLSLBase::print_type(Type type, AppendSpaceIfNeeded space_option) {
    ostringstream oss;
    type = map_type(type);
    if (type.is_scalar()) {
        if (type.is_float()) {
            oss << "float";
        } else if (type.is_bool()) {
            oss << "bool";
        } else if (type.is_int()) {
            oss << "int";
        } else {
            internal_error << "GLSL: invalid type '" << type << "' encountered.\n";
        }
    } else {
        if (type.is_float()) {
            // no prefix for float vectors
        } else if (type.is_bool()) {
            oss << "b";
        } else if (type.is_int()) {
            oss << "i";
        } else {
            internal_error << "GLSL: invalid type '" << type << "' encountered.\n";
        }
        oss << "vec" << type.lanes();
    }

    if (space_option == AppendSpace) {
        oss << " ";
    }

    return oss.str();
}

// The following comparisons are defined for ivec and vec
// types, so we don't use call_builtin
void CodeGen_GLSLBase::visit(const EQ *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "equal", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_GLSLBase::visit(const NE *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "notEqual", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_GLSLBase::visit(const LT *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "lessThan", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_GLSLBase::visit(const LE *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "lessThanEqual", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_GLSLBase::visit(const GT *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "greaterThan", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_GLSLBase::visit(const GE *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "greaterThanEqual", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_GLSLBase::visit(const Shuffle *op) {
    // The halide Shuffle represents the llvm intrinisc
    // shufflevector, however, for GLSL its use is limited to swizzling
    // up to a four channel vec type.

    internal_assert(op->vectors.size() == 1);

    int shuffle_lanes = op->type.lanes();
    internal_assert(shuffle_lanes <= 4);

    string expr = print_expr(op->vectors[0]);

    // Create a swizzle expression for the shuffle
    string swizzle;
    for (int i = 0; i != shuffle_lanes; ++i) {
        int channel = op->indices[i];
        internal_assert(channel < 4) << "Shuffle of invalid channel";
        swizzle += get_lane_suffix(channel);
    }

    print_assignment(op->type, expr + "." + swizzle);
}

// Identifiers containing double underscores '__' are reserved in GLSL, so we
// have to use a different name mangling scheme than in the C code generator.
string CodeGen_GLSLBase::print_name(const string &name) {
    const string mangled = CodeGen_C::print_name(name);
    return replace_all(mangled, "__", "XX");
}

//
// CodeGen_GLSL
//
void CodeGen_GLSL::visit(const FloatImm *op) {
    ostringstream oss;
    // Print integral numbers with trailing ".0". For fractional numbers use a
    // precision of 9 digits, which should be enough to recover the binary
    // float unambiguously from the decimal representation (if iostreams
    // implements correct rounding).
    const float truncated = (op->value < 0 ? std::ceil(op->value) : std::floor(op->value) );
    if (truncated == op->value) {
        oss << std::fixed << std::setprecision(1) << op->value;
    } else {
        oss << std::setprecision(9) << op->value;
    }
    id = oss.str();
}

void CodeGen_GLSL::visit(const IntImm *op) {
    if (op->type == Int(32)) {
        id = std::to_string(op->value);
    } else {
        id = print_type(op->type) + "(" + std::to_string(op->value) + ")";
    }
}

void CodeGen_GLSL::visit(const UIntImm *op) {
    id = print_type(op->type) + "(" + std::to_string(op->value) + ")";
}

void CodeGen_GLSL::visit(const Cast *op) {
    Type value_type = op->value.type();
    // If both types are represented by the same GLSL type, no explicit cast
    // is necessary.
    if (map_type(op->type) == map_type(value_type)) {
        Expr value = op->value;
        if (value_type.code() == Type::Float) {
            // float->int conversions may need explicit truncation if the
            // integer types is embedded into floats.  (Note: overflows are
            // considered undefined behavior, so we do nothing about values
            // that are out of range of the target type.)
            if (op->type.code() == Type::UInt) {
                value = simplify(floor(value));
            } else if (op->type.code() == Type::Int) {
                value = simplify(trunc(value));
            }
        }
        value.accept(this);
        return;
    } else {
        Type target_type = map_type(op->type);
        print_assignment(target_type, print_type(target_type) + "(" + print_expr(op->value) + ")");
    }
}

void CodeGen_GLSL::visit(const Let *op) {

    if (op->name.find(".varying") != string::npos) {

        // Skip let statements for varying attributes
        op->body.accept(this);

        return;
    }

    CodeGen_C::visit(op);
}

void CodeGen_GLSL::visit(const For *loop) {
    if (ends_with(loop->name, ".__block_id_x") ||
        ends_with(loop->name, ".__block_id_y")) {
        internal_assert(loop->for_type == ForType::GPUBlock)
            << "kernel loop must be gpu block\n";

        debug(1) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";

        string idx;
        if (ends_with(loop->name, ".__block_id_x")) {
            idx = "int(_varyingf0[0])";
        } else if (ends_with(loop->name, ".__block_id_y")) {
            idx = "int(_varyingf0[1])";
        }
        do_indent();
        stream << print_type(Int(32)) << " " << print_name(loop->name) << " = " << idx << ";\n";
        loop->body.accept(this);
    } else {
        user_assert(loop->for_type != ForType::Parallel) << "GLSL: parallel loops aren't allowed inside kernel.\n";
        CodeGen_C::visit(loop);
    }
}

vector<Expr> evaluate_vector_select(const Select *op) {
    const int lanes = op->type.lanes();
    vector<Expr> result(lanes);
    for (int i = 0; i < lanes; i++) {
        Expr cond = extract_lane(op->condition, i);
        Expr true_value = extract_lane(op->true_value, i);
        Expr false_value = extract_lane(op->false_value, i);

        if (is_const(cond)) {
            result[i] = is_one(cond) ? true_value : false_value;
        } else {
            result[i] = Select::make(cond, true_value, false_value);
        }
    }
    return result;
}

void CodeGen_GLSL::visit(const Select *op) {
    string id_value;
    if (op->condition.type().is_scalar()) {
        id_value = unique_name('_');
        do_indent();
        stream << print_type(op->type) << " " << id_value << ";\n";
        string cond = print_expr(op->condition);
        do_indent();
        stream << "if (" << cond << ") ";
        open_scope();
        {
            string true_val = print_expr(op->true_value);
            do_indent();
            stream << id_value << " = " << true_val << ";\n";
        }
        close_scope("");

        do_indent();
        stream << "else ";
        open_scope();
        {
            string false_val = print_expr(op->false_value);
            do_indent();
            stream << id_value << " = " << false_val<< ";\n";
        }
        close_scope("");
    } else {
        // Selects with vector conditions are typically used for constructing
        // vector types. If the select condition can be evaluated at
        // compile-time (which is often the case), we can built the vector
        // directly without lowering to a sequence of "if" statements.
        internal_assert(op->condition.type().lanes() == op->type.lanes());
        int lanes = op->type.lanes();
        vector<Expr> result = evaluate_vector_select(op);
        vector<string> ids(lanes);
        for (int i = 0; i < lanes; i++) {
            ids[i] = print_expr(result[i]);
        }
        id_value = unique_name('_');
        do_indent();
        stream << print_type(op->type) << " " << id_value << " = "
               << print_type(op->type) << "(";
        for (int i = 0; i < lanes; i++) {
            stream << ids[i] << ((i < lanes - 1) ? ", " : ");\n");
        }
    }

    id = id_value;
}

string CodeGen_GLSL::get_vector_suffix(Expr e) {
    vector<Expr> matches;
    Expr w = Variable::make(Int(32), "*");

    // The vectorize pass will insert a ramp in the color dimension argument.
    const Ramp *r = e.as<Ramp>();
    if (r && is_zero(r->base) && is_one(r->stride) && r->lanes == 4) {
        // No suffix is needed when accessing a full RGBA vector.
        return "";
    } else if (r && is_zero(r->base) && is_one(r->stride) && r->lanes == 3) {
        return ".rgb";
    } else if (r && is_zero(r->base) && is_one(r->stride) && r->lanes == 2) {
        return ".rg";
    } else {
        // GLSL 1.0 Section 5.5 supports subscript based vector indexing
        internal_assert(e.type().is_scalar());
        string id = print_expr(e);
        if (e.type() != Int(32)) {
            id = "int(" + id + ")";
        }
        return string("[" + id + "]");
    }
}

vector<string> CodeGen_GLSL::print_lanes(Expr e) {
    int l = e.type().lanes();
    internal_assert(e.type().is_vector());
    vector<string> result(l);
    if (const Broadcast *b = e.as<Broadcast>()) {
        string val = print_expr(b->value);
        for (int i = 0; i < l; i++) {
            result[i] = val;
        }
    } else if (const Ramp *r = e.as<Ramp>()) {
        for (int i = 0; i < l; i++) {
            result[i] = print_expr(simplify(r->base + i * r->stride));
        }
    } else {
        string val = print_expr(e);
        for (int i = 0; i < l; i++) {
            result[i] = val + "[" + std::to_string(i) + "]";
        }
    }
    return result;
}

void CodeGen_GLSL::visit(const Load *op) {
    user_assert(is_one(op->predicate)) << "GLSL: predicated load is not supported.\n";
    if (scalar_vars.contains(op->name)) {
        internal_assert(is_zero(op->index));
        id = print_name(op->name);
    } else if (vector_vars.contains(op->name)) {
        id = print_name(op->name) + get_vector_suffix(op->index);
    } else if (op->type.is_scalar()) {
        string idx = print_expr(op->index);
        print_assignment(op->type, print_name(op->name) + "[" + idx + "]");
    } else {
        vector<string> indices = print_lanes(op->index);
        ostringstream rhs;
        rhs << print_type(op->type) << "(";
        for (int i = 0; i < op->type.lanes(); i++) {
            if (i > 0) {
                rhs << ", ";
            }
            rhs << print_name(op->name) << "[" + indices[i] + "]";
        }
        rhs << ")";
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_GLSL::visit(const Store *op) {
    user_assert(is_one(op->predicate)) << "GLSL: predicated store is not supported.\n";
    if (scalar_vars.contains(op->name)) {
        internal_assert(is_zero(op->index));
        string val = print_expr(op->value);
        do_indent();
        stream << print_name(op->name) << " = " << val << ";\n";
    } else if (vector_vars.contains(op->name)) {
        string val = print_expr(op->value);
        do_indent();
        stream << print_name(op->name) << get_vector_suffix(op->index)
               << " = " << val << ";\n";
    } else if (op->value.type().is_scalar()) {
        string val = print_expr(op->value);
        string idx = print_expr(op->index);
        do_indent();
        stream << print_name(op->name) << "[" << idx << "] = " << val << ";\n";
    } else {
        vector<string> indices = print_lanes(op->index);
        vector<string> values = print_lanes(op->value);
        for (int i = 0; i < op->value.type().lanes(); i++) {
            do_indent();
            stream << print_name(op->name)
                   << "[" << indices[i] << "] = "
                   << values[i] << ";\n";
        }
    }
}


void CodeGen_GLSL::visit(const Evaluate *op) {
    print_expr(op->value);
}

void CodeGen_GLSL::visit(const Call *op) {
    ostringstream rhs;
    if (op->is_intrinsic(Call::glsl_texture_load)) {
        // This intrinsic takes five arguments
        // glsl_texture_load(<tex name>, <buffer>, <x>, <y>, <c>)
        internal_assert(op->args.size() == 5);

        // The argument to the call is either a StringImm or a broadcasted
        // StringImm if this is part of a vectorized expression
        internal_assert(op->args[0].as<StringImm>() ||
                        (op->args[0].as<Broadcast>() && op->args[0].as<Broadcast>()->value.as<StringImm>()));

        const StringImm *string_imm = op->args[0].as<StringImm>();
        if (!string_imm) {
            string_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
        }

        // Determine the halide buffer associated with this load
        string buffername = string_imm->value;

        internal_assert((op->type.code() == Type::UInt || op->type.code() == Type::Float) &&
                        (op->type.lanes() >= 1 && op->type.lanes() <= 4));

        if (op->type.is_vector()) {
            // The channel argument must be a ramp or a broadcast of a constant.
            Expr c = op->args[4];
            internal_assert(is_const(c));

            const Ramp *rc = c.as<Ramp>();
            const Broadcast *bx = op->args[2].as<Broadcast>();
            const Broadcast *by = op->args[3].as<Broadcast>();
            if (rc && is_zero(rc->base) && is_one(rc->stride) && bx && by) {
                // If the x and y coordinates are broadcasts, and the c
                // coordinate is a dense ramp, we can do a single
                // texture2D call.
                rhs << "texture2D(" << print_name(buffername) << ", vec2("
                    << print_expr(bx->value) << ", "
                    << print_expr(by->value) << "))";

                // texture2D always returns a vec4. Swizzle out the lanes we want.
                switch (op->type.lanes()) {
                case 1:
                    rhs << ".r";
                    break;
                case 2:
                    rhs << ".rg";
                    break;
                case 3:
                    rhs << ".rgb";
                    break;
                default:
                    break;
                }
            } else {
                // Otherwise do one load per lane and make a vector
                vector<string> xs = print_lanes(op->args[2]);
                vector<string> ys = print_lanes(op->args[3]);
                vector<string> cs = print_lanes(op->args[4]);
                string name = print_name(buffername);

                string x = print_expr(op->args[2]), y = print_expr(op->args[3]);
                rhs << print_type(op->type) << "(";
                for (int i = 0; i < op->type.lanes(); i++) {
                    if (i > 0) {
                        rhs << ", ";
                    }
                    rhs << "texture2D(" << name << ", vec2("
                        << xs[i] << ", " << ys[i] << "))[" << cs[i] << "]";
                }
                rhs << ")";
            }
        } else if (const int64_t *ic = as_const_int(op->args[4])) {
            internal_assert(*ic >= 0 && *ic < 4);
            rhs << "texture2D(" << print_name(buffername) << ", vec2("
                << print_expr(op->args[2]) << ", "
                << print_expr(op->args[3]) << "))."
                << get_lane_suffix(*ic);
        } else {
            rhs << "texture2D(" << print_name(buffername) << ", vec2("
                << print_expr(op->args[2]) << ", "
                << print_expr(op->args[3]) << "))["
                << print_expr(op->args[4]) << "]";

        }

        if (op->type.is_uint()) {
            rhs << " * " << print_expr(cast<float>(op->type.max()));
        }

    } else if (op->is_intrinsic(Call::glsl_texture_store)) {
        internal_assert(op->args.size() == 6);
        string sval = print_expr(op->args[5]);
        string suffix = get_vector_suffix(op->args[4]);
        do_indent();
        stream << "gl_FragColor" << suffix
               << " = " << sval;
        if (op->args[5].type().is_uint()) {
            stream << " / " << print_expr(cast<float>(op->args[5].type().max()));
        }
        stream << ";\n";
        // glsl_texture_store is called only for its side effect; there is
        // no return value.
        id = "";
        return;
    } else if (op->is_intrinsic(Call::glsl_varying)) {
        // Varying attributes should be substituted out by this point in
        // codegen.
        debug(2) << "Found skipped varying attribute: " << op->args[0] << "\n";

        // Output the tagged expression.
        print_expr(op->args[1]);
        return;
    } else if (op->is_intrinsic(Call::lerp)) {
        // Implement lerp using GLSL's mix() function, which always uses
        // floating point arithmetic.
        Expr zero_val = op->args[0];
        Expr one_val = op->args[1];
        Expr weight = op->args[2];

        internal_assert(weight.type().is_uint() || weight.type().is_float());
        if (weight.type().is_uint()) {
            // Normalize integer weights to [0.0f, 1.0f] range.
            internal_assert(weight.type().bits() < 32);
            weight = Div::make(Cast::make(Float(32), weight),
                               Cast::make(Float(32), weight.type().max()));
        } else if (op->type.is_uint()) {
            // Round float weights down to next multiple of (1/op->type.imax())
            // to give same results as lerp based on integer arithmetic.
            internal_assert(op->type.bits() < 32);
            weight = floor(weight * op->type.max()) / op->type.max();
        }

        Type result_type = Float(32, op->type.lanes());
        Expr e = call_builtin(result_type, "mix", {zero_val, one_val, weight});

        if (!op->type.is_float()) {
            // Mirror rounding implementation of Halide's integer lerp.
            e = Cast::make(op->type, floor(e + 0.5f));
        }
        print_expr(e);

        return;
    } else if (op->is_intrinsic(Call::abs)) {
        print_expr(call_builtin(op->type, op->name, op->args));
        return;
    } else if (op->is_intrinsic(Call::return_second)) {
        internal_assert(op->args.size() == 2);
        // Simply discard the first argument, which is generally a call to
        // 'halide_printf'.
        rhs << print_expr(op->args[1]);
    } else {
        CodeGen_GLSLBase::visit(op);
        return;
    }
    print_assignment(op->type, rhs.str());
}

namespace {
class AllAccessConstant : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Load *op) {
        if (op->name == buf && !is_const(op->index)) {
            result = false;
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        if (op->name == buf && !is_const(op->index)) {
            result = false;
        }
        IRVisitor::visit(op);
    }

public:
    bool result = true;
    string buf;
};
}

void CodeGen_GLSL::visit(const Allocate *op) {
    int32_t size = op->constant_allocation_size();
    user_assert(size) << "Allocations inside GLSL kernels must be constant-sized\n";

    // Check if all access to the allocation uses a constant index
    AllAccessConstant all_access_constant;
    all_access_constant.buf = op->name;
    op->body.accept(&all_access_constant);

    do_indent();
    if (size == 1) {
        // We can use a variable
        stream << print_type(op->type) << " " << print_name(op->name) << ";\n";
        scalar_vars.push(op->name, 0);
        op->body.accept(this);
        scalar_vars.pop(op->name);
    } else if (size <= 4 && all_access_constant.result) {
        // We can just use a vector variable
        stream << print_type(op->type.with_lanes(size)) << " " << print_name(op->name) << ";\n";
        vector_vars.push(op->name, 0);
        op->body.accept(this);
        vector_vars.pop(op->name);
    } else {
        stream << print_type(op->type) << " " << print_name(op->name) << "[" << size << "];\n";
        op->body.accept(this);
    }
}

void CodeGen_GLSL::visit(const Free *op) {
}

void CodeGen_GLSL::visit(const AssertStmt *) {
    internal_error << "GLSL: unexpected Assertion node encountered.\n";
}

void CodeGen_GLSL::visit(const Ramp *op) {
    ostringstream rhs;
    rhs << print_type(op->type) << "(";

    if (op->lanes > 4)
        internal_error << "GLSL: ramp lanes " << op->lanes << " is not supported\n";

    rhs << print_expr(op->base);

    for (int i = 1; i < op->lanes; ++i) {
        rhs << ", " << print_expr(Add::make(op->base, Mul::make(i, op->stride)));
    }

    rhs << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_GLSL::visit(const Broadcast *op) {
    ostringstream rhs;
    rhs << print_type(op->type) << "(" << print_expr(op->value) << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_GLSL::add_kernel(Stmt stmt, string name,
                              const vector<DeviceArgument> &args) {

    // This function produces fragment shader source for the halide statement.
    // The corresponding vertex shader will be generated by the halide opengl
    // runtime based on the arguments passed in comments below. Host codegen
    // outputs expressions that are evaluated at runtime to produce vertex data
    // and varying attribute values at the vertices.

    // Emit special header that declares the kernel name and its arguments.
    // There is currently no standard way of passing information from the code
    // generator to the runtime, and the information Halide passes to the
    // runtime are fairly limited.  We use these special comments to know the
    // data types of arguments and whether textures are used for input or
    // output.

    // Keep track of the number of uniform and varying attributes
    int num_uniform_floats = 0;
    int num_uniform_ints = 0;

    // The spatial x and y coordinates are always passed in the first two
    // varying float attribute slots
    int num_varying_floats = 2;

    ostringstream header;
    header << "/// KERNEL " << name << "\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            Type t = args[i].type.element_of();

            user_assert(args[i].read != args[i].write) <<
                "GLSL: buffers may only be read OR written inside a kernel loop.\n";
            string type_name;
            if (t == UInt(8)) {
                type_name = "uint8_t";
            } else if (t == UInt(16)) {
                type_name = "uint16_t";
            } else if (t == Float(32)) {
                type_name = "float";
            } else {
                user_error << "GLSL: buffer " << args[i].name << " has invalid type " << t << ".\n";
            }
            header << "/// " << (args[i].read ? "IN_BUFFER " : "OUT_BUFFER ")
                   << type_name << " " << print_name(args[i].name) << "\n";
        } else if (ends_with(args[i].name, ".varying")) {
            header << "/// VARYING "
            // GLSL requires that varying attributes are float. Integer
            // expressions for vertex attributes are cast to float during
            // host codegen
            << "float " << print_name(args[i].name) << " varyingf" << args[i].packed_index/4 << "[" << args[i].packed_index%4 << "]\n";
            ++num_varying_floats;
        } else if (args[i].type.is_float()) {
            header << "/// UNIFORM "
            << CodeGen_C::print_type(args[i].type) << " "
            << print_name(args[i].name) << " uniformf" << args[i].packed_index/4 << "[" << args[i].packed_index%4 << "]\n";
            ++num_uniform_floats;
        } else if (args[i].type.is_int()) {
            header << "/// UNIFORM "
            << CodeGen_C::print_type(args[i].type) << " "
            << print_name(args[i].name) << " uniformi" << args[i].packed_index/4 << "[" << args[i].packed_index%4 << "]\n";
            ++num_uniform_ints;
        }
    }

    // Compute the number of vec4's needed to pack the arguments
    num_varying_floats = (num_varying_floats + 3) / 4;
    num_uniform_floats = (num_uniform_floats + 3) / 4;
    num_uniform_ints   = (num_uniform_ints + 3) / 4;

    stream << header.str();

    // Specify default float precision when compiling for OpenGL ES.
    // TODO: emit correct #version
    if (is_opengl_es(target)) {
        stream << "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
               << "precision highp float;\n"
               << "#endif\n";
    }

    // Declare input textures and variables
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer && args[i].read) {
            stream << "uniform sampler2D " << print_name(args[i].name) << ";\n";
        }
    }

    for (int i = 0; i != num_varying_floats; ++i) {
        stream << "varying vec4 _varyingf" << i << ";\n";
    }

    for (int i = 0; i != num_uniform_floats; ++i) {
        stream << "uniform vec4 _uniformf" << i << ";\n";
    }

    for (int i = 0; i != num_uniform_ints; ++i) {
        stream << "uniform ivec4 _uniformi" << i << ";\n";
    }

    // Output additional builtin functions.
    stream <<
        "float _trunc_f32(float x) {\n"
        "  return floor(abs(x)) * sign(x);\n"
        "}\n";

    stream << "void main() {\n";
    indent += 2;

    // Unpack the uniform and varying parameters
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            continue;
        } else if (ends_with(args[i].name, ".varying")) {
            do_indent();
            stream << "float " << print_name(args[i].name)
                   << " = _varyingf" << args[i].packed_index/4
                   << "[" << args[i].packed_index%4 << "];\n";
        } else if (args[i].type.is_float()) {
            do_indent();
            stream << print_type(args[i].type) << " "
                   << print_name(args[i].name)
                   << " = _uniformf" << args[i].packed_index/4
                   << "[" << args[i].packed_index%4 << "];\n";
        } else if (args[i].type.is_int()) {
            do_indent();
            stream << print_type(args[i].type) << " "
                   << print_name(args[i].name)
                   << " = _uniformi" << args[i].packed_index/4
                   << "[" << args[i].packed_index%4 << "];\n";
        }
    }

    print(stmt);
    indent -= 2;
    stream << "}\n";
}

namespace {
// Replace all temporary variables names like _1234 with '$'. This is done to
// make the individual tests below self-contained.
string normalize_temporaries(const string &s) {
    string result;
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '_') {
            result += '$';
            for (i++; i < s.size() && isdigit(s[i]); i++) {
            }
        } else {
            result += s[i++];
        }
    }
    return result;
}

void check(Expr e, const string &result) {
    ostringstream source;
    CodeGen_GLSL cg(source, Target());
    if (e.as<FloatImm>() || e.as<IntImm>()) {
        // Hack: CodeGen_C doesn't treat immediates like other expressions, so
        // wrap them to obtain useful output.
        e = Halide::print(e);
    }
    Evaluate::make(e).accept(&cg);
    string src = normalize_temporaries(source.str());
    if (!ends_with(src, result)) {
        internal_error
            << "Codegen failed for " << e << "\n"
            << "  Correct source code:\n" << result
            << "  Actual source code:\n" << src;
    }
}

}  // namespace

void CodeGen_GLSL::test() {
    vector<Expr> e;

    // Check that float constants are printed correctly.
    check(1.0f, "float $ = 1.0;\n");
    check(1.0f + std::numeric_limits<float>::epsilon(), "float $ = 1.00000012;\n");
    check(1.19209290e-07f, "float $ = 1.1920929e-07;\n");
    check(8388608.f, "float $ = 8388608.0;\n");
    check(-2.1e19f, "float $ = -20999999189405401088.0;\n");
    check(3.1415926536f, "float $ = 3.14159274;\n");

    // Uint8 is embedded in GLSL floats, so no cast necessary
    check(cast<float>(Variable::make(UInt(8), "x") * 1.0f),
          "float $ = $x * 1.0;\n");
    // But truncation is necessary for the reverse direction
    check(cast<uint8_t>(Variable::make(Float(32), "x")),
          "float $ = floor($x);\n");

    check(Min::make(Expr(1), Expr(5)),
          "float $ = min(1.0, 5.0);\n"
          "int $ = int($);\n");

    check(Max::make(Expr(1), Expr(5)),
          "float $ = max(1.0, 5.0);\n"
          "int $ = int($);\n");

    check(Max::make(Broadcast::make(1, 4), Broadcast::make(5, 4)),
          "vec4 $ = vec4(1.0);\n"
          "vec4 $ = vec4(5.0);\n"
          "vec4 $ = max($, $);\n"
          "ivec4 $ = ivec4($);\n");

    check(Variable::make(Int(32), "x") / Expr(3),
          "float $ = float($x);\n"
          "float $ = $ * 0.333333343;\n"
          "float $ = floor($);\n"
          "int $ = int($);\n");
    check(Variable::make(Int(32, 4), "x") / Variable::make(Int(32, 4), "y"),
          "vec4 $ = vec4($x);\n"
          "vec4 $ = vec4($y);\n"
          "vec4 $ = $ / $;\n"
          "vec4 $ = floor($);\n"
          "ivec4 $ = ivec4($);\n");
    check(Variable::make(Float(32, 4), "x") / Variable::make(Float(32, 4), "y"),
          "vec4 $ = $x / $y;\n");

    // Integer lerp with integer weight
    check(lerp(cast<uint8_t>(0), cast<uint8_t>(255), cast<uint8_t>(127)),
          "float $ = mix(0.0, 255.0, 0.498039216);\n"
          "float $ = $ + 0.5;\n"
          "float $ = floor($);\n");

    // Integer lerp with float weight
    check(lerp(cast<uint8_t>(0), cast<uint8_t>(255), 0.3f),
          "float $ = mix(0.0, 255.0, 0.298039228);\n"
          "float $ = $ + 0.5;\n"
          "float $ = floor($);\n");

    // Floating point lerp
    check(lerp(0.0f, 1.0f, 0.3f),
          "float $ = mix(0.0, 1.0, 0.300000012);\n");

    // Vectorized lerp
    check(lerp(Variable::make(Float(32, 4), "x"), Variable::make(Float(32, 4), "y"), Broadcast::make(0.25f, 4)),
          "vec4 $ = vec4(0.25);\n"
          "vec4 $ = mix($x, $y, $);\n");

    // Sin with scalar arg
    check(sin(3.0f), "float $ = sin(3.0);\n");

    // Sin with vector arg
    check(Call::make(Float(32, 4), "sin_f32", {Broadcast::make(1.f, 4)}, Internal::Call::Extern),
          "vec4 $ = vec4(1.0);\n"
          "vec4 $ = sin($);\n");

    // use float version of abs in GLSL
    check(abs(-2),
          "float $ = abs(-2.0);\n"
          "int $ = int($);\n");

    check(Halide::print(3.0f), "float $ = 3.0;\n");

    // Test rounding behavior of integer division.
    check(Variable::make(Int(32), "x") / Variable::make(Int(32), "y"),
          "float $ = float($x);\n"
          "float $ = float($y);\n"
          "float $ = $ / $;\n"
          "float $ = floor($);\n"
          "int $ = int($);\n");

    // Select with scalar condition
    check(Select::make(EQ::make(Variable::make(Float(32), "x"), 1.0f),
                       Broadcast::make(1.f, 4),
                       Broadcast::make(2.f, 4)),
          "vec4 $;\n"
          "bool $ = $x == 1.0;\n"
          "if ($) {\n"
          " vec4 $ = vec4(1.0);\n"
          " $ = $;\n"
          "}\n"
          "else {\n"
          " vec4 $ = vec4(2.0);\n"
          " $ = $;\n"
          "}\n");

    // Select with vector condition
    check(Select::make(EQ::make(Ramp::make(-1, 1, 4), Broadcast::make(0, 4)),
                       Broadcast::make(1.f, 4),
                       Broadcast::make(2.f, 4)),
          "vec4 $ = vec4(2.0, 1.0, 2.0, 2.0);\n");

    // Test codegen for texture loads
    Expr load4 = Call::make(Float(32, 4), Call::glsl_texture_load,
                            {string("buf"),
                             0,
                             Broadcast::make(0, 4),
                             Broadcast::make(0, 4),
                             Ramp::make(0, 1, 4)},
                            Call::Intrinsic);
    check(load4, "vec4 $ = texture2D($buf, vec2(0, 0));\n");

    check(log(1.0f), "float $ = log(1.0);\n");
    check(exp(1.0f), "float $ = exp(1.0);\n");

    // Integer powers are expanded
    check(pow(1.4f, 2), "float $ = 1.39999998 * 1.39999998;\n");
    check(pow(1.0f, 2.1f), "float $ = pow(1.0, 2.0999999);\n");

    std::cout << "CodeGen_GLSL test passed\n";
}

}}
