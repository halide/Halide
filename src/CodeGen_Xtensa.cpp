#include "CodeGen_Xtensa.h"

#include <string>

#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Lerp.h"
#include "Simplify.h"
#include "Substitute.h"
#include "XtensaOptimize.h"

// 0 = off
// 1 == outermost loops only
// 2 == 2 outermost loop levels only
// etc
#define POOR_MANS_PROFILING_LOOP_LEVEL 0

namespace Halide {
namespace Internal {

using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

extern "C" unsigned char halide_c_template_CodeGen_Xtensa_prologue[];
extern "C" unsigned char halide_c_template_CodeGen_Xtensa_vectors[];

namespace {

// For most of our purposes, a halide_type_t is just as good as a Halide::Type,
// but notably smaller and more efficient (since it fits into a u32 and hashes well).
class HalideTypeSetHashFunction {
public:
    size_t operator()(const halide_type_t &t) const {
        // classic djb2 hash
        const uint32_t u = t.as_u32();
        size_t h = 5381;
        // Assume that compiler may decide to replace h*33 with (h<<5)+h if it so chooses
        h = h * 33 + ((u)&0xff);
        h = h * 33 + (((u) >> 8) & 0xff);
        h = h * 33 + (((u) >> 16) & 0xff);
        h = h * 33 + (((u) >> 24) & 0xff);
        return h;
    }
};

using HalideTypeSet = std::unordered_set<halide_type_t, HalideTypeSetHashFunction>;

const char *intrinsic_suffix_for_type(const halide_type_t &t) {
    switch (t.with_lanes(1).as_u32()) {
    case halide_type_t(halide_type_float, 16).as_u32():
        return "NXF16";
    case halide_type_t(halide_type_float, 32).as_u32():
        return "N_2XF32";
    case halide_type_t(halide_type_int, 16).as_u32():
        return "NX16";
    case halide_type_t(halide_type_int, 32).as_u32():
        return "N_2X32";
    case halide_type_t(halide_type_int, 8).as_u32():
        return "2NX8";
    case halide_type_t(halide_type_uint, 16).as_u32():
        return "NX16U";
    case halide_type_t(halide_type_uint, 32).as_u32():
        return "N_2X32U";
    case halide_type_t(halide_type_uint, 8).as_u32():
        return "2NX8U";
    default:
        return "";
    }
}

class UsesDmaCopy : public IRGraphVisitor {
private:
    using IRGraphVisitor::visit;

protected:
    void visit(const Call *op) override {
        if ((op->name == "halide_xtensa_copy_1d") || (op->name == "halide_xtensa_copy_2d")) {
            uses_dma = true;
            max_channel_no = std::max<int>(max_channel_no, *as_const_int(op->args[0]));
        }

        IRGraphVisitor::visit(op);
    }

public:
    bool uses_dma = false;
    int max_channel_no = 0;
};

}  // namespace

CodeGen_Xtensa::CodeGen_Xtensa(ostream &s, const Target &t, OutputKind k, const std::string &guard)
    : CodeGen_C(s, t, k, guard),
      op_name_to_intrinsic{
          {"halide_xtensa_abs_i8", "IVP_ABS2NX8"},
          {"halide_xtensa_abs_i16", "IVP_ABSNX16"},
          {"halide_xtensa_abs_i32", "IVP_ABSN_2X32"},
          {"halide_xtensa_abs_f32", "IVP_ABSN_2XF32"},
          {"halide_xtensa_sat_add_i16", "IVP_ADDSNX16"},
          {"halide_xtensa_sat_sub_i16", "IVP_SUBSNX16"},
          {"halide_xtensa_avg_i8", "IVP_AVG2NX8"},
          {"halide_xtensa_avg_u8", "IVP_AVGU2NX8"},
          {"halide_xtensa_avg_i16", "IVP_AVGNX16"},
          {"halide_xtensa_avg_u16", "IVP_AVGUNX16"},
          {"halide_xtensa_avg_round_i8", "IVP_AVGR2NX8"},
          {"halide_xtensa_avg_round_u8", "IVP_AVGRU2NX8U"},
          {"halide_xtensa_avg_round_i16", "IVP_AVGRNX16"},
          {"halide_xtensa_avg_round_u16", "IVP_AVGRUNX16U"},
          {"halide_xtensa_widen_mul_i24", "IVP_MUL2NX8"},
          {"halide_xtensa_widen_mul_u24", "IVP_MULUU2NX8"},
          {"halide_xtensa_widen_mul_i48", "IVP_MULNX16"},
          {"halide_xtensa_widen_mul_u48", "IVP_MULUUNX16U"},
          {"halide_xtensa_mul_i32", "IVP_MULN_2X32"},
          {"halide_xtensa_widen_mul_ui48", "IVP_MULUSNX16"},
          {"halide_xtensa_widen_pair_mul_u48", "IVP_MULUUPNX16"},
          {"halide_xtensa_convert_i48_low_i32", "IVP_CVT32SNX48L"},
          {"halide_xtensa_convert_i48_high_i32", "IVP_CVT32SNX48H"},
          {"halide_xtensa_convert_i48_low_u32", "IVP_CVT32UNX48L"},
          {"halide_xtensa_convert_i48_high_u32", "IVP_CVT32UNX48H"},
          {"halide_xtensa_narrow_i48_with_shift_i16", "IVP_PACKVRNRNX48"},
          {"halide_xtensa_narrow_i48_with_rounding_shift_i16", "IVP_PACKVRNX48"},
          {"halide_xtensa_sat_narrow_i48_with_shift_i16", "IVP_PACKVRNX48"},
          {"halide_xtensa_sat_narrow_with_rounding_shift_i32", "IVP_PACKVRN_2X64W"},
          {"halide_xtensa_full_reduce_add_i8", "IVP_RADD2NX8"},
          {"halide_xtensa_full_reduce_add_i16", "IVP_RADDNX16"},
          {"halide_xtensa_full_reduce_add_i32", "IVP_RADDN_2X32"},

          {"halide_xtensa_full_reduce_min_u8", "IVP_RMINU2NX8U"},
          {"halide_xtensa_full_reduce_min_u16", "IVP_RMINUNX16U"},
          {"halide_xtensa_full_reduce_min_u32", "IVP_RMINUN_2X32U"},
          {"halide_xtensa_full_reduce_min_i8", "IVP_RMIN2NX8"},
          {"halide_xtensa_full_reduce_min_i16", "IVP_RMINNX16"},
          {"halide_xtensa_full_reduce_min_i32", "IVP_RMINN_2X32"},

          {"halide_xtensa_full_reduce_max_u8", "IVP_RMAXU2NX8U"},
          {"halide_xtensa_full_reduce_max_u16", "IVP_RMAXUNX16U"},
          {"halide_xtensa_full_reduce_max_u32", "IVP_RMAXUN_2X32U"},
          {"halide_xtensa_full_reduce_max_i8", "IVP_RMAX2NX8"},
          {"halide_xtensa_full_reduce_max_i16", "IVP_RMAXNX16"},
          {"halide_xtensa_full_reduce_max_i32", "IVP_RMAXN_2X32"},

          {"halide_xtensa_sat_left_shift_i16", "IVP_SLSNX16"},
          {"halide_xtensa_sat_left_shift_i32", "IVP_SLSN_2X32"},
      } {
}

void CodeGen_Xtensa::add_platform_prologue() {
    stream << halide_c_template_CodeGen_Xtensa_prologue;
}

Stmt CodeGen_Xtensa::preprocess_function_body(const Stmt &stmt) {
    Stmt new_body = match_xtensa_patterns(stmt, get_target());

    UsesDmaCopy uses_dma;
    new_body.accept(&uses_dma);
    if (uses_dma.uses_dma) {
        stream << get_indent() << "ScopedDmaInitializer dma_initializer(" << (uses_dma.max_channel_no) + 1 << ");\n";
        stream << get_indent() << "if (!dma_initializer.is_valid()) {\n";
        stream << get_indent() << "halide_error(_ucon, \"DMA initialization failed\");\n";
        stream << get_indent() << "return halide_error_code_generic_error;\n";
        stream << get_indent() << "}\n";
    }

    return new_body;
}

halide_type_t CodeGen_Xtensa::get_native_xtensa_vector(const halide_type_t &t) const {
    // There two types of vectors, the wide vectors are essentially accumulators
    // and can store 24-, 48- or 64-bit values.
    const bool has_q8 = get_target().has_feature(Target::Feature::XtensaQ8);
    const int vector_bitwidth = has_q8 ? 1024 : 512;
    const int wide_vector_bitwidth = has_q8 ? 4096 : 1536;

    switch (t.bits) {
    case 64:
        return t.with_lanes(vector_bitwidth / 32);
    case 24:
    case 48:
        return t.with_lanes(wide_vector_bitwidth / t.bits);
    default:
        return t.with_lanes(vector_bitwidth / t.bits);
    }
}

void CodeGen_Xtensa::add_vector_typedefs(const std::set<Type> &vector_types) {
    stream << R"INLINE_CODE(
#if defined(__XTENSA__)
#include <xtensa/sim.h>
#include <xtensa/tie/xt_ivpn.h>
#include <xtensa/tie/xt_timer.h>

// This inline function is needed by application to get the cycle count from ISS
inline int GetCycleCount() {
  return XT_RSR_CCOUNT();
}

#endif
)INLINE_CODE";

    if (!vector_types.empty()) {
        // Fix: on at least one config (our arm32 buildbot running gcc 5.4),
        // emitting this long text string was regularly garbled in a predictable
        // pattern; flushing the stream before or after heals it. Since C++
        // codegen is rarely on a compilation critical path, we'll just band-aid
        // it in this way.
        stream << std::flush;
        stream << halide_c_template_CodeGen_Xtensa_vectors;
        stream << std::flush;

        const HalideTypeSet native_vector_types = {
            halide_type_t(halide_type_int, 8, target.natural_vector_size<int8_t>()),
            halide_type_t(halide_type_uint, 8, target.natural_vector_size<uint8_t>()),
            halide_type_t(halide_type_int, 16, target.natural_vector_size<int16_t>()),
            halide_type_t(halide_type_uint, 16, target.natural_vector_size<uint16_t>()),
            halide_type_t(halide_type_int, 32, target.natural_vector_size<int32_t>()),
            halide_type_t(halide_type_uint, 32, target.natural_vector_size<uint32_t>()),
            halide_type_t(halide_type_int, 24, target.natural_vector_size<int8_t>()),
            halide_type_t(halide_type_uint, 24, target.natural_vector_size<uint8_t>()),
            halide_type_t(halide_type_int, 48, target.natural_vector_size<int16_t>()),
            halide_type_t(halide_type_uint, 48, target.natural_vector_size<uint16_t>()),
            halide_type_t(halide_type_int, 64, target.natural_vector_size<int32_t>()),  // Yes, int32, not int64
            halide_type_t(halide_type_float, 16, target.natural_vector_size<float16_t>()),
            halide_type_t(halide_type_float, 32, target.natural_vector_size<float>()),
        };

        const HalideTypeSet predefined_vectors = {
            halide_type_t(halide_type_int, 8, 4),
            halide_type_t(halide_type_uint, 8, 4),
            halide_type_t(halide_type_uint, 8, 8),
            halide_type_t(halide_type_float, 16),
        };

        HalideTypeSet multiple_of_native_types;
        for (const auto &type : vector_types) {
            if (predefined_vectors.count(type) > 0) {
                continue;
            }
            for (const auto &native_vector : native_vector_types) {
                if (native_vector.code == type.code() &&
                    native_vector.bits == type.bits() &&
                    type.lanes() > native_vector.lanes &&
                    (type.lanes() % native_vector.lanes) == 0) {
                    stream << "using " << print_type(type) << " = MultipleOfNativeVector<" << print_type(native_vector) << ", " << type.lanes() / native_vector.lanes << ">;\n";
                    multiple_of_native_types.insert(type);
                    break;
                }
            }
        }

        std::set<Type> filtered_vector_types;
        for (const auto &t : vector_types) {
            if (native_vector_types.count(t) > 0 ||
                predefined_vectors.count(t) > 0 ||
                multiple_of_native_types.count(t) > 0) {
                continue;
            }
            filtered_vector_types.insert(t);
        }

        CodeGen_C::add_vector_typedefs(filtered_vector_types);
    }
}

std::string CodeGen_Xtensa::print_type(Type t, AppendSpaceIfNeeded space_option) {
    if (t.bits() == 1 && t.is_vector()) {
        return "uint1x" + std::to_string(t.lanes()) + "_t" + (space_option == AppendSpace ? " " : "");
    } else if (t.is_float() && t.is_vector()) {
        return "float" + std::to_string(t.bits()) + "x" + std::to_string(t.lanes()) + "_t" + (space_option == AppendSpace ? " " : "");
    }
    return CodeGen_C::print_type(t, space_option);
}

void CodeGen_Xtensa::visit(const IntImm *op) {
    if (op->type.is_int() && (op->type.bits() <= 32)) {
        id = std::to_string(op->value);
    } else {
        static const char *const suffixes[3] = {
            "ll",  // PlainC
            "l",   // OpenCL
            "",    // HLSL
        };
        print_assignment(op->type, "(" + print_type(op->type) + ")(" + std::to_string(op->value) + suffixes[(int)integer_suffix_style] + ")");
    }
}
void CodeGen_Xtensa::visit(const Mul *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        print_expr(Call::make(op->type, Call::shift_left, {op->a, Expr(bits)}, Call::PureIntrinsic));
    } else {
        if (is_native_xtensa_vector<int16_t>(op->type)) {
            string sa = print_expr(op->a);
            string sb = print_expr(op->b);
            print_assignment(op->type, "IVP_MULNX16PACKL(" + sa + ", " + sb + ")");
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            string sa = print_expr(op->a);
            string sb = print_expr(op->b);
            print_assignment(op->type, "IVP_MULNX16UPACKL(" + sa + ", " + sb + ")");
        } else if (is_native_xtensa_vector<int32_t>(op->type) ||
                   is_native_xtensa_vector<uint32_t>(op->type)) {
            string sa = print_expr(op->a);
            string sb = print_expr(op->b);
            print_assignment(op->type, "IVP_PACKLN_2X64W(IVP_MULN_2X32(" + sa + ", " + sb + "))");
        } else {
            visit_binop(op->type, op->a, op->b, "*");
        }
    }
}

string CodeGen_Xtensa::print_xtensa_call(const Call *op) {
    ostringstream rhs;

    vector<string> args(op->args.size());

    if (op->name == "halide_xtensa_widening_load") {
        internal_assert(op->args.size() == 3);
        const Variable *src = op->args[0].as<Variable>();
        internal_assert(src != nullptr);
        args[0] = print_name(src->name);
        args[1] = print_expr(op->args[1]);
        // We are only using args[2] argument to get the type of the load.

        rhs << "widening_load<" << print_type(op->type) << ", " << print_type(op->args[2].type()) << ">(" << args[0] << ", " << args[1] << ")";
        return rhs.str();
    }

    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
    }

    if (op->name == "halide_xtensa_pad_to_native" || op->name == "halide_xtensa_slice_from_padded") {
        internal_assert(op->args.size() == 2);
        // TODO(vksnk): bools are tricky, because they are bitmasks, so need to be
        // handled differently.
        const int bytes_in_vector = get_target().natural_vector_size<uint8_t>();
        if (op->type.is_bool()) {
            internal_assert((op->type.lanes() == bytes_in_vector && op->args[0].type().lanes() == bytes_in_vector / 2) || (op->type.lanes() == bytes_in_vector / 2 && op->args[0].type().lanes() == bytes_in_vector / 4) || (op->type.lanes() == bytes_in_vector && op->args[0].type().lanes() == bytes_in_vector / 4)) << Expr(op);
        }
        rhs << op->name << "<" << print_type(op->args[0].type()) << ", "
            << print_type(op->type) << ", " << print_type(op->type.element_of())
            << ", " << op->args[0].type().lanes() << ", " << op->type.lanes()
            << ">(" << args[0] << ", " << args[1] << ")";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_slice_to_native" && !op->type.is_bool()) {
        const halide_type_t native_vector_type = get_native_xtensa_vector(op->type);
        int vector_count = op->type.lanes() / native_vector_type.lanes;

        if (vector_count == 1) {
            rhs << args[0] << ".native_vector[" << args[1] << "]";
        } else {
            rhs << print_type(op->type) << "(" << print_type(op->type) << "::from_native_vector, ";
            std::vector<std::string> native_vectors;
            for (int ix = 0; ix < vector_count; ix++) {
                native_vectors.push_back(args[0] + ".native_vector[" + args[1] + " * " + std::to_string(vector_count) + " + " + std::to_string(ix) + "]");
            }
            rhs << with_commas(native_vectors) << ")";
        }
        return rhs.str();
    }

    if (op->name == "halide_xtensa_concat_from_native" && !op->type.is_bool()) {
        rhs << print_type(op->type) << "(" << print_type(op->type) << "::from_native_vector, " << with_commas(args) << ")";
        return rhs.str();
    }

    if ((op->name.find("halide_xtensa_slice_right") == 0) || (op->name.find("halide_xtensa_slice_left") == 0)) {
        string intrinsic_name;
        string shift_define;
        string direction = (op->name.find("halide_xtensa_slice_right") == 0) ? "RIGHT_" : "LEFT_";
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            intrinsic_name = "IVP_SEL2NX8I";
            shift_define = "IVP_SELI_8B_ROTATE_";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            intrinsic_name = "IVP_SEL2NX8UI";
            shift_define = "IVP_SELI_8B_ROTATE_";
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            intrinsic_name = "IVP_SELNX16I";
            shift_define = "IVP_SELI_16B_ROTATE_";
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            intrinsic_name = "IVP_SELNX16UI";
            shift_define = "IVP_SELI_16B_ROTATE_";
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            intrinsic_name = "IVP_SELN_2X32I";
            shift_define = "IVP_SELI_32B_ROTATE_";
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            intrinsic_name = "IVP_SELN_2X32UI";
            shift_define = "IVP_SELI_32B_ROTATE_";
        } else if (is_native_xtensa_vector<float16_t>(op->type)) {
            intrinsic_name = "IVP_SELNXF16I";
            shift_define = "IVP_SELI_16B_ROTATE_";
        } else if (is_native_xtensa_vector<float>(op->type)) {
            intrinsic_name = "IVP_SELN_2XF32I";
            shift_define = "IVP_SELI_32B_ROTATE_";
        } else {
            internal_assert(false) << "Unsupported type for slicing";
        }

        rhs << intrinsic_name << "(" << args[0] << ".native_vector[1], " << args[0] << ".native_vector[0], " << shift_define << direction << args[1] << ")";

        return rhs.str();
    }
    // absd needs extra cast to uint*
    if (op->name == "halide_xtensa_absd_i16") {
        if (op->args[0].type().is_int()) {
            rhs << "xb_vecNx16_rtor_xb_vecNx16U(IVP_ABSSUBNX16(" << args[0] + ", " + args[1] + "))";
        } else {
            rhs << "IVP_ABSSUBUNX16U(" << args[0] + ", " + args[1] + ")";
        }
        return rhs.str();
    } else if (op->name == "halide_xtensa_narrow_i48_with_shift_u16") {
        rhs << "xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(" << args[0] + ", " + args[1] + "))";
        return rhs.str();
    } else if (op->name == "halide_xtensa_convert_i48_low_u32") {
        rhs << "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48L(" << args[0] + "))";
        return rhs.str();
    } else if (op->name == "halide_xtensa_convert_i48_high_u32") {
        rhs << "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48H(" << args[0] + "))";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_extract_i32" || op->name == "halide_xtensa_extract_u32") {
        rhs << "IVP_EXTRN_2X32(IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(" << args[0] + ")), " + args[1] + ")";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_dual_extract_i32") {
        rhs << "IVP_DEXTRPRN_2X32("
            << "IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(" + args[0] + ")), "
            << "IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(" + args[1] + ")), "
            << args[2] + ", " + args[3] + ")";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_dynamic_shuffle") {
        if (is_native_vector_type(op->args[0].type()) &&
            is_native_vector_type(op->args[1].type())) {
            rhs << "IVP_SHFL" << intrinsic_suffix_for_type(op->type) << "("
                << args[0] + ", " + args[1] + ")";
            return rhs.str();
        }
    }

    string op_name = op->name;
    if (const auto it = op_name_to_intrinsic.find(op_name); it != op_name_to_intrinsic.end()) {
        op_name = it->second;
    }

    rhs << op_name << "(" << with_commas(args) << ")";
    return rhs.str();
}

void CodeGen_Xtensa::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        print_expr(Call::make(op->type, Call::shift_right, {op->a, Expr(bits)}, Call::PureIntrinsic));
    } else if (is_native_xtensa_vector<float16_t>(op->type)) {
        ostringstream rhs;
        rhs << "IVP_DIVNXF16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        print_assignment(op->type, rhs.str());
    } else if (is_native_xtensa_vector<float>(op->type)) {
        ostringstream rhs;
        rhs << "IVP_DIVN_2XF32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        print_assignment(op->type, rhs.str());
    } else {
        string sa = print_expr(op->a);
        string sb = print_expr(op->b);
        // Just cast to clang vector types and use division defined on them.
        if (is_native_xtensa_vector<uint8_t>(op->type) ||
            is_native_xtensa_vector<int8_t>(op->type) ||
            is_native_xtensa_vector<int32_t>(op->type) ||
            is_native_xtensa_vector<uint32_t>(op->type)) {
            print_assignment(
                op->type,
                "(common_" + print_type(op->type) + ")" + sa + " / (common_" + print_type(op->type) + ")" + sb);
        } else {
            print_assignment(op->type, sa + " / " + sb);
        }
    }
}

void CodeGen_Xtensa::visit(const Mod *op) {
    int bits;
    if (is_native_vector_type(op->type) && is_const_power_of_two_integer(op->b, &bits)) {
        print_expr(op->a &
                   Broadcast::make(
                       Cast::make(op->type.with_lanes(1), Expr((1 << bits) - 1)), op->type.lanes()));
    } else if (is_native_xtensa_vector<int32_t>(op->type)) {
        string sa = print_expr(op->a);
        string sb = print_expr(op->b);
        string common_type = "common_" + print_type(op->type);
        print_assignment(op->type, "(" + common_type + ")" + sa + " % (" + common_type + ")" + sb);
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const Max *op) {
    if (op->type.is_scalar()) {
        print_expr(Call::make(op->type, "::halide_cpp_max<" + print_type(op->type) + ">", {op->a, op->b}, Call::Extern));
    } else {
        ostringstream rhs;
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            rhs << "IVP_MAX2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            rhs << "IVP_MAXU2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            rhs << "IVP_MAXNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            rhs << "IVP_MAXUNX16U(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            rhs << "IVP_MAXN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            rhs << "IVP_MAXUN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type)) {
            rhs << "IVP_MAXNXF16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float>(op->type)) {
            rhs << "IVP_MAXN_2XF32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else {
            rhs << print_type(op->type) << "::max(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        }
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_Xtensa::visit(const Min *op) {
    if (op->type.is_scalar()) {
        print_expr(Call::make(op->type, "::halide_cpp_min<" + print_type(op->type) + ">", {op->a, op->b}, Call::Extern));
    } else {
        ostringstream rhs;
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            rhs << "IVP_MIN2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            rhs << "IVP_MINU2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            rhs << "IVP_MINNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            rhs << "IVP_MINUNX16U(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            rhs << "IVP_MINN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            rhs << "IVP_MINUN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type)) {
            rhs << "IVP_MINNXF16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float>(op->type)) {
            rhs << "IVP_MINN_2XF32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else {
            rhs << print_type(op->type) << "::min(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        }
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_Xtensa::visit(const Select *op) {
    ostringstream rhs;
    string type = print_type(op->type);
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);

    if (op->condition.type().is_scalar()) {
        rhs << "(" << type << ")"
            << "(" << cond
            << " ? " << true_val
            << " : " << false_val
            << ")";
    } else {
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            rhs << "IVP_MOV2NX8T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            rhs << "IVP_MOV2NX8UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            rhs << "IVP_MOVNX16T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            rhs << "IVP_MOVNX16UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            rhs << "IVP_MOVN_2X32T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            rhs << "IVP_MOVN_2X32UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type)) {
            rhs << "IVP_MOVNXF16T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<float>(op->type)) {
            rhs << "IVP_MOVN_2XF32T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else {
            rhs << type << "::select(" << cond << ", " << true_val << ", " << false_val << ")";
        }
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_Xtensa::visit(const Ramp *op) {
    Type vector_type = op->type.with_lanes(op->lanes);
    string id_base = print_expr(op->base);
    string id_stride = print_expr(op->stride);
    int int32_lanes = get_target().natural_vector_size<int32_t>();
    if (is_const_one(op->stride)) {
        if (is_native_xtensa_vector<int32_t>(op->type)) {
            print_assignment(vector_type, "/* ramp */ int32x" + std::to_string(int32_lanes) + "_t(" + id_base + ") + IVP_SEQN_2X32()");
        } else {
            // If it's wide enough split it here into concat of smaller ramps.
            if (op->type.is_int() && (op->type.bits() == 32) && (op->type.lanes() % int32_lanes == 0) && (op->type.lanes() / int32_lanes > 4)) {
                int split_to = op->type.lanes() / int32_lanes;

                std::vector<Expr> concat_args;
                for (int ix = 0; ix < split_to; ix++) {
                    Expr r = Ramp::make(op->base + op->stride * (int32_lanes * ix), op->stride, int32_lanes);
                    concat_args.push_back(std::move(r));
                }
                Expr concat = Call::make(op->type,
                                         "halide_xtensa_concat_from_native",
                                         concat_args, Call::PureExtern);

                concat.accept(this);
            } else {
                print_assignment(vector_type, "dense_ramp<" + print_type(vector_type) + ">(" + id_base + ")");
            }
        }
    } else {
        if (is_native_xtensa_vector<int32_t>(op->type)) {
            print_assignment(vector_type, "/* ramp */ int32x" + std::to_string(int32_lanes) + "_t(" + id_base + ") + IVP_PACKLN_2X64W(IVP_SEQN_2X32() * int32x" + std::to_string(int32_lanes) + "_t(" + id_stride + "))");
        } else if ((op->type.lanes() == 32 || op->type.lanes() == 64 || op->type.lanes() == 128) && op->type.is_int_or_uint() && op->type.bits() == 32) {
            print_assignment(vector_type, "ramp<" + print_type(vector_type) + ">(" + id_base + ", " + id_stride + ")");
        } else {
            print_assignment(vector_type, print_type(vector_type) + "_ops::ramp(" + id_base + ", " + id_stride + ")");
        }
    }
}

void CodeGen_Xtensa::visit(const Broadcast *op) {
    Type vector_type = op->type.with_lanes(op->lanes);
    string rhs;
    if (op->type.is_int() && ((op->type.bits() == 24) || (op->type.bits() == 48)) && is_const(op->value)) {
        // Assigning a constant to wide vector is tricky.
        if (is_const_zero(op->value)) {
            if (op->type.bits() == 24) {
                rhs = "IVP_ZERO2NX24()";
            } else if (op->type.bits() == 48) {
                rhs = "IVP_ZERONX48()";
            }
        } else {
            rhs = std::to_string(op->value.as<IntImm>()->value);
        }
    } else if (op->type.is_int_or_uint() && op->type.bits() == 8 && ((op->type.lanes() == 4) || (op->type.lanes() == 8))) {
        string id_value = print_expr(op->value);
        rhs = "broadcast<" + print_type(op->type) + ", " + print_type(op->value.type()) + ">(" + id_value + ")";
    } else {
        string id_value = print_expr(op->value);

        if (is_native_vector_type(op->type)) {
            // TODO(vsknk): why it this extra cast to scalar is needed?
            rhs = print_type(vector_type) + "((" + print_type(op->type.with_lanes(1)) + ")" + id_value + ")";
        } else if (op->lanes > 1) {
            if (op->type.is_bool()) {
                // TODO(vksnk): figure out how to broadcast bool.
                if (op->type.lanes() == 16) {
                    rhs = id_value + "? (int32x16_t(1) == int32x16_t(1)) : (int32x16_t(1) == int32x16_t(0))";
                } else if (op->type.lanes() == 32) {
                    rhs = id_value + "? (int16x32_t(1) == int16x32_t(1)) : (int16x32_t(1) == int16x32_t(0))";
                } else if (op->type.lanes() == 64) {
                    rhs = id_value + "? (int8x64_t(1) == int8x64_t(1)) : (int8x64_t(1) == int8x64_t(0))";
                }
            } else {
                rhs = id_value;
            }
        } else {
            rhs = id_value;
        }
    }

    print_assignment(vector_type, rhs);
}

template<typename ComparisonOp>
void CodeGen_Xtensa::visit_comparison_op(const ComparisonOp *op, const string &op_name) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (is_native_xtensa_vector<int8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_" + op_name + "2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_" + op_name + "U2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_" + op_name + "NX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_" + op_name + "UNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_" + op_name + "N_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_" + op_name + "UN_2X32U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_O" + op_name + "NXF16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float>(op->a.type())) {
        print_assignment(op->type, "IVP_O" + op_name + "N_2XF32(" + sa + ", " + sb + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const LE *op) {
    visit_comparison_op(op, "LE");
}

void CodeGen_Xtensa::visit(const LT *op) {
    visit_comparison_op(op, "LT");
}

void CodeGen_Xtensa::visit(const GE *op) {
    visit_comparison_op(op, "GE");
}

void CodeGen_Xtensa::visit(const GT *op) {
    visit_comparison_op(op, "GT");
}

void CodeGen_Xtensa::visit(const EQ *op) {
    visit_comparison_op(op, "EQ");
}

void CodeGen_Xtensa::visit(const Or *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (op->a.type().is_bool() && op->type.is_vector()) {
        if (op->a.type().lanes() == 16) {
            print_assignment(op->type, "IVP_ORBN_2(" + sa + ", " + sb + ")");
        } else if (op->a.type().lanes() == 32) {
            print_assignment(op->type, "IVP_ORBN(" + sa + ", " + sb + ")");
        } else if (op->a.type().lanes() == 64) {
            print_assignment(op->type, "IVP_ORB2N(" + sa + ", " + sb + ")");
        } else {
            internal_assert(false) << "Unhandled boolean type in the || op\n";
        }
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const Load *op) {
    // TODO: We could replicate the logic in the llvm codegen which decides whether
    // the vector access can be aligned. Doing so would also require introducing
    // aligned type equivalents for all the vector types.
    ostringstream rhs;

    Type t = op->type;
    string name = print_name(op->name);

    // If we're loading a contiguous ramp into a vector, just load the vector
    Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    if (!is_const_one(op->predicate)) {
        Expr predicate_with_all_lets = substitute_in_all_lets(op->predicate);
        const Call *pred = predicate_with_all_lets.as<Call>();
        if (pred && (pred->name == "clamped_dense_ramp") && dense_ramp_base.defined()) {
            internal_assert(t.is_vector());
            // The number of elements is difference between upper bound and base of the ramp
            // plus one (because the predicate is <=).
            Expr count = simplify(max(pred->args[1] - pred->args[0] + 1, 0));
            string id_ramp_base = print_expr(dense_ramp_base);
            string id_count = print_expr(count);
            rhs << "load_variable"
                << "<" << print_type(t) << ", "
                << print_type(t.element_of()) << ", " << t.lanes()
                << ">(" << name << ", " << id_ramp_base << ", " << id_count << ")";
        } else {
            string id_index = print_expr(op->index);
            string id_predicate = print_expr(op->predicate);
            rhs << "load_predicated<" << print_type(t) << ", "
                << print_type(op->index.type()) << ", "
                << print_type(op->predicate.type()) << ", "
                << print_type(t.element_of()) << ", " << t.lanes()
                << ">(" << name << ", " << id_index << ", " << id_predicate << ")";
        }
    } else if (dense_ramp_base.defined()) {
        internal_assert(t.is_vector());
        std::string op_name;
        const int bytes_in_vector = get_target().natural_vector_size<uint8_t>();
        int native_lanes = (bytes_in_vector / op->type.element_of().bytes());
        if (op->type.element_of().bytes() == 3) {
            native_lanes = bytes_in_vector;
        }
        if (op->type.element_of().bytes() == 6) {
            native_lanes = bytes_in_vector / 2;
        }
        bool is_aligned_load = (op->alignment.modulus % native_lanes == 0) && (op->alignment.remainder % native_lanes == 0);
        if (external_buffers.count(op->name) > 0) {
            is_aligned_load = is_aligned_load && (op->param.host_alignment() % bytes_in_vector == 0);
        }
        if (is_aligned_load) {
            op_name = "aligned_load";
        } else {
            op_name = "load";
        }
        string id_ramp_base = print_expr(dense_ramp_base);
        rhs << op_name << "<" << print_type(t) << ", "
            << print_type(t.element_of()) << ", " << t.lanes()
            << ">(" << name << ", " << id_ramp_base << ")";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, gather vector elements.
        internal_assert(t.is_vector());
        // NOTE(vksnk): strided_load may be a good idea, but needs more work.
        // const Ramp* maybe_ramp = op->index.as<Ramp>();
        // if (maybe_ramp && is_const(maybe_ramp->stride)) {
        //     string id_index_base = print_expr(maybe_ramp->base);
        //     string id_index_stride = print_expr(maybe_ramp->stride);
        //     rhs << print_type(t) + "_strided_load(" << name << ", "
        //         << id_index_base << ", " << id_index_stride << ")";
        // } else {
        string id_index = print_expr(op->index);
        // Is not allocated on the heap and is not a buffer
        bool is_tcm = !(heap_allocations.contains(name) || external_buffers.count(op->name) > 0);

        rhs << "gather_load<" << print_type(t) << ", "
            << print_type(Int(32, t.lanes())) << ", "
            << print_type(t.element_of()) << ", "
            << t.lanes() << ", " << is_tcm << ">("
            << name << ", " << id_index << ")";
        // }
    } else {
        string id_index = print_expr(op->index);
        bool type_cast_needed = !(allocations.contains(op->name) &&
                                  allocations.get(op->name).type.element_of() == t.element_of());
        if (type_cast_needed) {
            rhs << "((const " << print_type(t.element_of()) << " *)" << name << ")";
        } else {
            rhs << name;
        }
        rhs << "[" << id_index << "]";
    }
    print_assignment(t, rhs.str());
}

void CodeGen_Xtensa::visit(const Store *op) {
    Type t = op->value.type();

    if (inside_atomic_mutex_node) {
        user_assert(t.is_scalar())
            << "The vectorized atomic operation for the store" << op->name
            << " is lowered into a mutex lock, which does not support vectorization.\n";
    }

    // Issue atomic store if we are in the designated producer.
    if (emit_atomic_stores) {
        stream << "#if defined(_OPENMP)\n";
        stream << "#pragma omp atomic\n";
        stream << "#else\n";
        stream << "#error \"Atomic stores in the C backend are only supported in compilers that support OpenMP.\"\n";
        stream << "#endif\n";
    }

    bool is_narrowing = false;
    bool is_sat_narrowing = false;
    Expr value = op->value;
    if (const Cast *cast = value.as<Cast>()) {
        if (cast->value.type().is_vector() && cast->type.is_int_or_uint() && cast->value.type().is_int_or_uint() && (cast->value.type().bits() == value.type().bits() * 2)) {
            is_narrowing = true;
            value = cast->value;
        }
    }
    if (const Call *call = value.as<Call>()) {
        // TODO: more checks for this one are needed.
        if (call->name == "halide_xtensa_slice_from_padded") {
            if (const Cast *cast = call->args[0].as<Cast>()) {
                if (cast->value.type().is_vector() && cast->type.is_int_or_uint() && cast->value.type().is_int_or_uint() && (cast->value.type().bits() == value.type().bits() * 2)) {
                    if (const Call *inner_call = cast->value.as<Call>()) {
                        if (inner_call->name == "halide_xtensa_pad_to_native") {
                            is_narrowing = true;
                            value = inner_call->args[0];
                        }
                    }
                }
            }
        }
        // TODO(vksnk): disabled for now, because corresponding implementation
        // is missing.
        // if (call->name.find("halide_xtensa_sat_narrow_i") == 0) {
        //     is_sat_narrowing = true;
        //     value = call->args[0];
        // }
    }

    string id_value = print_expr(value);
    string name = print_name(op->name);

    // TODO: We could replicate the logic in the llvm codegen which decides whether
    // the vector access can be aligned. Doing so would also require introducing
    // aligned type equivalents for all the vector types.

    // If we're writing a contiguous ramp, just store the vector.
    Expr dense_ramp_base = strided_ramp_base(op->index, 1);

    if (!is_const_one(op->predicate)) {
        Expr predicate_with_all_lets = substitute_in_all_lets(op->predicate);
        const Call *pred = predicate_with_all_lets.as<Call>();
        if (pred && (pred->name == "clamped_dense_ramp") && dense_ramp_base.defined()) {
            // The number of elements is difference between upper bound and base of the ramp
            // plus one (because the predicate is <=).
            Expr count = simplify(max(pred->args[1] - pred->args[0] + 1, 0));
            internal_assert(op->value.type().is_vector());
            string id_ramp_base = print_expr(dense_ramp_base);
            string id_count = print_expr(count);
            string op_name = "store_variable";
            if (is_narrowing) {
                op_name = op_name + "_narrowing";
            }
            if (is_sat_narrowing) {
                op_name = op_name + "_narrowing_sat";
            }
            stream << get_indent() << op_name << "<";
            if (is_narrowing) {
                stream << print_type(value.type());
            } else {
                stream << print_type(t);
            }
            stream << ", " << print_type(t.element_of()) << ", " << t.lanes()
                   << ">(" << id_value << ", " << name << ", " << id_ramp_base << ", " << id_count << ");\n";
        } else {
            string id_index = print_expr(op->index);
            string id_predicate = print_expr(op->predicate);
            stream << get_indent() << "store_predicated<" << print_type(t) << ", "
                   << print_type(op->index.type()) << ", "
                   << print_type(op->predicate.type()) << ", "
                   << print_type(t.element_of()) << ", " << t.lanes()
                   << ">(" << id_value << ", " << name << ", " << id_index << ", " << id_predicate << ");\n";
        }
    } else if (dense_ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());
        string op_name;
        const int bytes_in_vector = get_target().natural_vector_size<uint8_t>();
        int native_lanes = (bytes_in_vector / op->value.type().element_of().bytes());
        if (op->value.type().element_of().bytes() == 3) {
            native_lanes = bytes_in_vector;
        }
        if (op->value.type().element_of().bytes() == 6) {
            native_lanes = bytes_in_vector / 2;
        }

        bool is_aligned_store = (op->alignment.modulus % native_lanes == 0) && (op->alignment.remainder % native_lanes == 0);
        if (external_buffers.count(op->name) > 0) {
            is_aligned_store = is_aligned_store && (op->param.host_alignment() % bytes_in_vector == 0);
        }

        if (is_aligned_store) {
            op_name = "aligned_store";
        } else {
            op_name = "store";
        }

        if (is_narrowing) {
            op_name = op_name + "_narrowing";
        }
        if (is_sat_narrowing) {
            op_name = op_name + "_narrowing_sat";
        }

        string id_ramp_base = print_expr(dense_ramp_base);
        stream << get_indent() << op_name << "<";
        if (is_narrowing) {
            stream << print_type(value.type());
        } else {
            stream << print_type(t);
        }
        stream << ", " << print_type(t.element_of()) << ", " << t.lanes()
               << ">(" << id_value << ", " << name << ", " << id_ramp_base << ");\n";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());
        string id_index = print_expr(op->index);
        stream << get_indent() << "store_scatter<" << print_type(t) << ", "
               << print_type(op->index.type()) << ", "
               << print_type(t.element_of()) << ", " << t.lanes()
               << ">(" << id_value << ", " << name << ", " << id_index << ");\n";
    } else {
        bool type_cast_needed =
            t.is_handle() ||
            !allocations.contains(op->name) ||
            allocations.get(op->name).type != t;

        string id_index = print_expr(op->index);
        stream << get_indent();
        if (type_cast_needed) {
            stream << "((" << print_type(t) << " *)" << name << ")";
        } else {
            stream << name;
        }
        stream << "[" << id_index << "] = " << id_value << ";\n";
    }
    cache.clear();
}

bool CodeGen_Xtensa::is_stack_private_to_thread() const {
    return true;
}

void CodeGen_Xtensa::visit(const Call *op) {
    ostringstream rhs;

    // Handle intrinsics first
    if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        const int64_t *bits = as_const_int(op->args[1]);
        if (is_native_xtensa_vector<uint8_t>(op->type) && bits) {
            rhs << "IVP_SLLI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int8_t>(op->type) && bits) {
            rhs << "IVP_SLLI2NX8(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type) && bits) {
            rhs << "IVP_SLLINX16U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type) && bits) {
            rhs << "IVP_SLLINX16(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type) && bits) {
            rhs << "IVP_SLLIN_2X32U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type) && bits) {
            rhs << "IVP_SLLIN_2X32(" << a0 << ", " << std::to_string(*bits) << ")";
        } else {
            string a1 = print_expr(op->args[1]);
            if (is_native_xtensa_vector<uint16_t>(op->type)) {
                rhs << "IVP_SLLNX16U(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int16_t>(op->type)) {
                rhs << "IVP_SLANX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
                rhs << "IVP_SLLN_2X32U(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int32_t>(op->type)) {
                rhs << "IVP_SLAN_2X32(" << a0 << ", " << a1 << ")";
            } else {
                if (op->args[1].type().is_uint()) {
                    if (op->type.is_vector()) {
                        rhs << "scalarize_binary<" << print_type(op->type) << ", "
                            << print_type(op->type.with_lanes(1)) << ", "
                            << print_type(op->type.with_lanes(1)) << ", "
                            << op->type.lanes() << ">(&halide_shift_left, "
                            << print_expr(op->args[0])
                            << ", " << print_expr(op->args[1]) << ")";

                    } else {
                        string a0 = print_expr(op->args[0]);
                        string a1 = print_expr(op->args[1]);
                        rhs << a0 << " << " << a1;
                    }
                } else {
                    rhs << print_expr(lower_signed_shift_left(op->args[0], op->args[1]));
                }
            }
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        const int64_t *bits = as_const_int(op->args[1]);
        if (is_native_xtensa_vector<uint8_t>(op->type) && bits) {
            rhs << "IVP_SRLI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int8_t>(op->type) && bits) {
            rhs << "IVP_SRAI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type) && bits) {
            rhs << "IVP_SRAINX16(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type) && bits) {
            rhs << "IVP_SRLINX16U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type) && bits) {
            rhs << "IVP_SRAIN_2X32(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type) && bits) {
            rhs << "IVP_SRLIN_2X32U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else {
            string a1 = print_expr(op->args[1]);
            if (is_native_xtensa_vector<uint16_t>(op->type)) {
                rhs << "IVP_SRLNX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int16_t>(op->type)) {
                rhs << "IVP_SRANX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
                rhs << "IVP_SRLN_2X32U(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int32_t>(op->type)) {
                rhs << "IVP_SRAN_2X32(" << a0 << ", (" << print_type(op->type) << ")" << a1 << ")";
            } else {
                if (op->args[1].type().is_uint()) {
                    if (op->type.is_vector()) {
                        rhs << "scalarize_binary<" << print_type(op->type) << ", "
                            << print_type(op->type.with_lanes(1)) << ", "
                            << print_type(op->type.with_lanes(1)) << ", "
                            << op->type.lanes() << ">(&halide_shift_right, "
                            << print_expr(op->args[0])
                            << ", " << print_expr(op->args[1]) << ")";
                    } else {
                        string a0 = print_expr(op->args[0]);
                        string a1 = print_expr(op->args[1]);
                        rhs << a0 << " >> " << a1;
                    }
                } else {
                    rhs << print_expr(lower_signed_shift_right(op->args[0], op->args[1]));
                }
            }
        }
    } else if (op->is_intrinsic(Call::count_leading_zeros)) {
        internal_assert(op->args.size() == 1);
        if (is_native_xtensa_vector<int16_t>(op->type) ||
            is_native_xtensa_vector<uint16_t>(op->type)) {
            // TODO(vksnk): it seems that what Halide does is always matching IVP_NSAUN*?
            string intrins_name = op->type.is_int() ? "(IVP_NSAUNX16(" : "xb_vecNx16_rtor_xb_vecNx16U(IVP_NSAUNX16U(";
            rhs << intrins_name << print_expr(op->args[0]) << "))";
        } else if (is_native_xtensa_vector<int32_t>(op->type) ||
                   is_native_xtensa_vector<uint32_t>(op->type)) {
            // TODO(vksnk): it seems that what Halide does is always matching IVP_NSAUN*?
            string intrins_name = op->type.is_int() ? "(IVP_NSAUN_2X32(" : "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_NSAUN_2X32U(";
            rhs << intrins_name << print_expr(op->args[0]) << "))";
        } else if (op->args[0].type().is_vector()) {
            // Xtensa doesn't have 8-bit intrinsics for count_leading_zeros.
            rhs << "scalarize_unary<" << print_type(op->type) << ", "
                << print_type(op->type.with_lanes(1)) << ", "
                // return type of halide_count_leading_zeros is always int.
                << "int, "
                << op->type.lanes() << ">(&halide_count_leading_zeros, " << print_expr(op->args[0]) << ")";
        } else {
            string a0 = print_expr(op->args[0]);
            rhs << "halide_" << op->name << "(" << a0 << ")";
        }
    } else if (op->is_intrinsic(Call::popcount)) {
        internal_assert(op->args.size() == 1);
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            rhs << "IVP_POPC2NX8(" << print_expr(op->args[0]) << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            rhs << "IVP_POPC2NX8U(" << print_expr(op->args[0]) << ")";
        } else if (op->type.is_vector()) {
            // Xtensa only has popcount intrinsics for 8-bit vector types.
            rhs << "scalarize_unary<" << print_type(op->type) << ", "
                << print_type(op->type.with_lanes(1)) << ", "
                // return type of halide_popcount is always int.
                << "int, "
                << op->type.lanes() << ">(&halide_popcount, " << print_expr(op->args[0]) << ")";
        } else {
            CodeGen_C::visit(op);
            return;
        }
    } else if (op->is_intrinsic(Call::count_trailing_zeros)) {
        internal_assert(op->args.size() == 1);
        if (op->type.is_vector()) {
            // Xtensa doesn't have intrinsics for count_trailing_zeros.
            rhs << "scalarize_unary<" << print_type(op->type) << ", "
                << print_type(op->type.with_lanes(1)) << ", "
                // return type of halide_count_trailing_zeros is always int.
                << "int, "
                << op->type.lanes() << ">(&halide_count_trailing_zeros, " << print_expr(op->args[0]) << ")";
        } else {
            CodeGen_C::visit(op);
            return;
        }
    } else if (op->is_intrinsic(Call::prefetch)) {
        user_error << "Prefetch is not supported by Xtensa backend." << Expr(op) << "\n";
    } else if (op->name == "sqrt" || op->name == "sqrt_f32") {
        string a0 = print_expr(op->args[0]);
        if (is_native_xtensa_vector<float>(op->type)) {
            rhs << "IVP_SQRTN_2XF32(" << a0 << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type)) {
            rhs << "IVP_SQRTNXF16(" << a0 << ")";
        } else {
            rhs << "sqrtf(" << a0 << ")";
        }
    } else if (op->name == "round" || op->name == "round_f32") {
        string a0 = print_expr(op->args[0]);
        if (is_native_xtensa_vector<float>(op->type)) {
            rhs << "IVP_FIRINTN_2XF32(" << a0 << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type)) {
            rhs << "IVP_FIRINTNXF16(" << a0 << ")";
        } else {
            rhs << "nearbyint(" << a0 << ")";
        }
    } else if (op->name == "floor" || op->name == "floor_f32") {
        string a0 = print_expr(op->args[0]);
        if (is_native_xtensa_vector<float>(op->type)) {
            rhs << "IVP_FIFLOORN_2XF32(" << a0 << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type)) {
            rhs << "IVP_FIFLOORNXF16(" << a0 << ")";
        } else {
            rhs << "floor_f32(" << a0 << ")";
        }
    } else if (op->name.find("halide_xtensa_") == 0) {
        rhs << print_xtensa_call(op);
    } else {
        CodeGen_C::visit(op);
        return;
    }

    print_assignment(op->type, rhs.str());
}

void CodeGen_Xtensa::visit(const Cast *op) {
    const Type &t = op->type;
    const Expr &e = op->value;
    string value = print_expr(e);
    string type = print_type(t);
    if ((is_native_xtensa_vector<int8_t>(t) ||
         is_native_xtensa_vector<uint8_t>(t)) &&
        (is_native_xtensa_vector<int8_t>(e.type()) ||
         is_native_xtensa_vector<uint8_t>(e.type()))) {
        if (e.type().is_int()) {
            id = print_assignment(t, "xb_vec2Nx8_rtor_xb_vec2Nx8U(" + value + ")");
        } else {
            id = print_assignment(t, "xb_vec2Nx8U_rtor_xb_vec2Nx8(" + value + ")");
        }
    } else if ((is_native_xtensa_vector<int16_t>(t) ||
                is_native_xtensa_vector<uint16_t>(t)) &&
               (is_native_xtensa_vector<int16_t>(e.type()) ||
                is_native_xtensa_vector<uint16_t>(e.type()))) {
        if (e.type().is_int()) {
            id = print_assignment(t, "xb_vecNx16_rtor_xb_vecNx16U(" + value + ")");
        } else {
            id = print_assignment(t, "xb_vecNx16U_rtor_xb_vecNx16(" + value + ")");
        }
    } else if ((is_native_xtensa_vector<int32_t>(t) ||
                is_native_xtensa_vector<uint32_t>(t)) &&
               (is_native_xtensa_vector<int32_t>(e.type()) ||
                is_native_xtensa_vector<uint32_t>(e.type()))) {
        if (e.type().is_int()) {
            id = print_assignment(t, "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(" + value + ")");
        } else {
            id = print_assignment(t, "xb_vecN_2x32Uv_rtor_xb_vecN_2x32v(" + value + ")");
        }
    } else if (is_native_xtensa_vector<int64_t>(e.type()) &&
               is_native_xtensa_vector<int32_t>(t)) {
        id = print_assignment(t, "IVP_PACKLN_2X64W(" + value + ")");
    } else if (t.is_vector() &&
               t.lanes() == e.type().lanes() &&
               t != e.type()) {
        id = print_assignment(t, "convert<" + type + "," + print_type(e.type()) + ">(" + value + ")");
    } else {
        id = print_assignment(t, "(" + type + ")(" + value + ")");
    }
}

void CodeGen_Xtensa::visit(const Reinterpret *op) {
    if (is_native_vector_type(op->type) &&
        is_native_vector_type(op->value.type())) {
        string op_name = "";
        if (is_native_xtensa_vector<int32_t>(op->type) &&
            is_native_xtensa_vector<uint32_t>(op->value.type())) {
            op_name = "xb_vecN_2x32Uv_rtor_xb_vecN_2x32v";
        } else if (is_native_xtensa_vector<uint32_t>(op->type) &&
                   is_native_xtensa_vector<int32_t>(op->value.type())) {
            op_name = "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv";
        } else if (is_native_xtensa_vector<uint32_t>(op->type) &&
                   is_native_xtensa_vector<float>(op->value.type())) {
            op_name = "IVP_MOVN_2X32_FROMN_2XF32";
        } else if (is_native_xtensa_vector<float>(op->type) &&
                   is_native_xtensa_vector<uint32_t>(op->value.type())) {
            op_name = "IVP_MOVN_2XF32_FROMN_2X32";
        }
        if (!op_name.empty()) {
            string value = print_expr(op->value);
            id = print_assignment(op->type, op_name + "(" + value + ")");
            return;
        }
    }
    CodeGen_C::visit(op);
}

// TODO(aelphy): xtensa compiler produces sub-optimal results with the default C
// implementation
void CodeGen_Xtensa::emit_halide_free_helper(
    const std::string &alloc_name, const std::string &free_function) {
    stream << get_indent() << "HalideXtensaFreeHelper "
           << alloc_name << "_free(_ucon, " << alloc_name
           << ", " << free_function << ");\n";
}

void CodeGen_Xtensa::visit(const For *op) {
    current_loop_level++;
    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    if (op->for_type == ForType::Parallel) {
        stream << get_indent() << "#pragma omp parallel for\n";
    } else {
        internal_assert(op->for_type == ForType::Serial)
            << "Can only emit serial or parallel for loops to C\n";
    }

#if POOR_MANS_PROFILING_LOOP_LEVEL > 0
    std::string n = op->name;
    for (auto &c : n) {
        if (c == '$' || c == '.') {
            c = '_';
        }
    }
    if (current_loop_level <= POOR_MANS_PROFILING_LOOP_LEVEL) {
        open_scope();
        stream << get_indent() << "const int cycles_start_" << n << " = GetCycleCount();\n";
    }
#endif

    stream << get_indent() << "for (int "
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
#if POOR_MANS_PROFILING_LOOP_LEVEL > 0
    if (current_loop_level <= POOR_MANS_PROFILING_LOOP_LEVEL) {
        stream << get_indent() << "const int cycles_stop_" << n << " = GetCycleCount();\n";
        stream << get_indent() << "const int cycles_tot_" << n << " = cycles_stop_" << n << " - cycles_start_" << n << ";\n";
        stream << get_indent() << "printf(\"@" << current_loop_level << ": " << op->name << ": %d\\n\", cycles_tot_" << n << ");\n";
        close_scope("profiler" + print_name(op->name));
    }
#endif
    current_loop_level--;
}

void CodeGen_Xtensa::visit(const Shuffle *op) {
    internal_assert(!op->vectors.empty());
    for (size_t i = 1; i < op->vectors.size(); i++) {
        internal_assert(op->vectors[0].type() == op->vectors[i].type());
    }
    internal_assert(op->type.lanes() == (int)op->indices.size());
    const int max_index = (int)(op->vectors[0].type().lanes() * op->vectors.size());
    for (int i : op->indices) {
        internal_assert(i >= -1 && i < max_index);
    }

    // Generate intrinsics for the interleave op.
    if (op->is_interleave() &&
        (is_native_vector_type(op->vectors[0].type()) || is_double_native_vector_type(op->vectors[0].type()) || (op->vectors[0].type().is_bool()))) {
        string type_suffix = suffix_for_type(op->type);

        Expr call = Call::make(op->type, "halide_xtensa_interleave" + type_suffix,
                               op->vectors, Call::PureExtern);
        call.accept(this);
        return;
    }

    if (op->is_slice() && (op->slice_stride() == 1) &&
        (is_native_xtensa_vector<int8_t>(op->type) ||
         is_native_xtensa_vector<uint8_t>(op->type) ||
         is_native_xtensa_vector<int16_t>(op->type) ||
         is_native_xtensa_vector<uint16_t>(op->type) ||
         is_native_xtensa_vector<int32_t>(op->type) ||
         is_native_xtensa_vector<uint32_t>(op->type) ||
         is_native_xtensa_vector<float>(op->type) ||
         is_native_xtensa_vector<float16_t>(op->type))) {
        string type_suffix = suffix_for_type(op->type);
        string function_name = "halide_xtensa_slice";
        int slice_begin = op->slice_begin();
        if (op->slice_begin() < 5 || (op->slice_begin() == 6) || (op->slice_begin() == 8)) {
            function_name += "_right";
        }
        if ((op->type.lanes() - op->slice_begin() < 5) && (op->type.lanes() > op->slice_begin())) {
            function_name += "_left";
            slice_begin = op->type.lanes() - op->slice_begin();
        }
        Expr call = Call::make(op->type, function_name + type_suffix,
                               {op->vectors[0], slice_begin}, Call::PureExtern);
        call.accept(this);
        return;
    }

    if (op->vectors.size() == 1) {
        if (op->is_slice() && (op->slice_begin() < 2) && (op->slice_stride() == 2) && ((int)op->indices.size() == op->vectors[0].type().lanes() / 2)) {
            string type_suffix = suffix_for_type(op->type);
            string function_name = std::string("halide_xtensa_deinterleave") + ((op->slice_begin() == 0) ? "_even" : "_odd");
            Expr call = Call::make(op->type, function_name + type_suffix,
                                   {op->vectors[0]}, Call::PureExtern);
            call.accept(this);
            return;
        }
        if (is_native_vector_type(op->type) && op->is_slice() && (op->slice_begin() >= 0 && op->slice_begin() < 4) && (op->slice_stride() == 4) && ((int)op->indices.size() == op->vectors[0].type().lanes() / 4)) {
            string type_suffix = suffix_for_type(op->type);
            string function_name = std::string("halide_xtensa_extract_" + std::to_string(op->slice_begin()) + "_of_4");
            Expr call = Call::make(op->type, function_name + type_suffix,
                                   {op->vectors[0]}, Call::PureExtern);
            call.accept(this);
            return;
        }
        if (is_native_vector_type(op->type) && op->is_slice() && (op->slice_begin() >= 0 && op->slice_begin() < 8) && (op->slice_stride() == 8) && ((int)op->indices.size() == op->vectors[0].type().lanes() / 8)) {
            string type_suffix = suffix_for_type(op->type);
            string function_name = std::string("halide_xtensa_extract_" + std::to_string(op->slice_begin()) + "_of_8");
            Expr call = Call::make(op->type, function_name + type_suffix,
                                   {op->vectors[0]}, Call::PureExtern);
            call.accept(this);
            return;
        }
    }

    if (op->is_concat() && is_native_vector_type(op->vectors[0].type())) {
        Expr call = Call::make(op->type, "halide_xtensa_concat_from_native", op->vectors, Call::PureExtern);
        call.accept(this);
        return;
    }

    std::vector<string> vecs;
    for (const Expr &v : op->vectors) {
        vecs.push_back(print_expr(v));
    }
    string src = vecs[0];
    Type src_type = op->vectors[0].type();
    if (op->vectors.size() > 1) {
        ostringstream rhs;
        rhs << "concat<"
            << print_type(op->type) << ", "
            << print_type(op->vectors[0].type()) << ", "
            << print_type(op->type.element_of()) << ", "
            << op->type.lanes() << ", "
            << op->vectors[0].type().lanes()
            << ">(" << with_commas(vecs) << ")";
        src = print_assignment(op->type, rhs.str());
        src_type = src_type.with_lanes(src_type.lanes() * op->vectors.size());
    }
    ostringstream rhs;
    if (op->type.is_scalar()) {
        rhs << src << "[" << op->indices[0] << "]";
    } else if (op->is_concat()) {
        // Do nothing if it's just concat.
        return;
    } else if (op->type.bits() == 24 && op->vectors[0].type().lanes() == 128 && op->type.is_int()) {
        if (op->is_slice() && op->slice_begin() == 0 && op->slice_stride() == 1 && op->indices.size() == 64) {
            rhs << src << ".native_vector[0]";
        }
        if (op->is_slice() && op->slice_begin() == 64 &&
            op->slice_stride() == 1 && op->indices.size() == 64) {
            rhs << src << ".native_vector[1]";
        }
    } else {
        string indices_name = unique_name('_');
        stream << get_indent() << "const int32_t " << indices_name << "[" << op->indices.size() << "] = { " << with_commas(op->indices) << " };\n";
        rhs << "shuffle"
            << "<"
            << print_type(src_type) << ", "
            << print_type(op->type) << ", "
            << print_type(op->type.element_of()) << ", " << src_type.lanes()
            << ", " << op->type.lanes()
            << ">(" << src << ", " << indices_name << ")";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_Xtensa::visit(const Allocate *op) {
    open_scope();

    string op_name = print_name(op->name);
    string op_type = print_type(op->type, AppendSpace);

    // For sizes less than 8k, do a stack allocation
    bool on_stack = false;
    int32_t constant_size;
    string size_id;
    Type size_id_type;

    if (op->new_expr.defined()) {
        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);
        heap_allocations.push(op->name);
        stream << op_type << "*" << op_name << " = (" << print_expr(op->new_expr) << ");\n";
    } else {
        constant_size = op->constant_allocation_size();
        if (constant_size > 0) {
            int64_t stack_bytes = constant_size * op->type.bytes();

            if (stack_bytes > ((int64_t(1) << 31) - 1)) {
                user_error << "Total size for allocation "
                           << op->name << " is constant but exceeds 2^31 - 1.\n";
            } else {
                size_id_type = Int(32);
                size_id = print_expr(make_const(size_id_type, constant_size));

                if (op->memory_type == MemoryType::Stack ||
                    op->memory_type == MemoryType::Register ||
                    (op->memory_type == MemoryType::Auto && (stack_bytes <= 512))) {
                    on_stack = true;
                }
            }
        } else {
            // Check that the allocation is not scalar (if it were scalar
            // it would have constant size).
            internal_assert(!op->extents.empty());

            size_id = print_assignment(Int(64), print_expr(op->extents[0]));
            size_id_type = Int(64);

            for (size_t i = 1; i < op->extents.size(); i++) {
                // Make the code a little less cluttered for two-dimensional case
                string new_size_id_rhs;
                string next_extent = print_expr(op->extents[i]);
                if (i > 1) {
                    new_size_id_rhs = "(" + size_id + " > ((int64_t(1) << 31) - 1)) ? " + size_id + " : (" + size_id + " * " + next_extent + ")";
                } else {
                    new_size_id_rhs = size_id + " * " + next_extent;
                }
                size_id = print_assignment(Int(64), new_size_id_rhs);
            }
        }

        // Check the condition to see if this allocation should actually be created.
        // If the allocation is on the stack, the only condition we can respect is
        // unconditional false (otherwise a non-constant-sized array declaration
        // will be generated).
        if (!on_stack || is_const_zero(op->condition)) {
            Expr conditional_size = Select::make(op->condition,
                                                 Variable::make(size_id_type, size_id),
                                                 make_const(size_id_type, 0));
            conditional_size = simplify(conditional_size);
            size_id = print_assignment(Int(64), print_expr(conditional_size));
        }

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        stream << get_indent() << op_type;

        if (on_stack) {
            stream << "__attribute__((aligned(XCHAL_VISION_SIMD8))) " << op_name
                   << "[" << size_id << "];\n";
        } else {
            const char *const alloc_fn = (op->memory_type == MemoryType::VTCM) ?
                                             "halide_tcm_malloc" :
                                             "halide_malloc";
            stream << "*"
                   << "__attribute__((aligned(XCHAL_VISION_SIMD8))) "
                   << " __restrict "
                   << op_name
                   << " = ("
                   << op_type
                   << " *)" << alloc_fn << "(_ucon, sizeof("
                   << op_type
                   << ")*" << size_id << ");\n";
            // TODO: why doesn't TCM count as a heap allocation?
            if (op->memory_type != MemoryType::VTCM) {
                heap_allocations.push(op->name);
            }
        }
    }

    if (!on_stack) {
        ostringstream check;
        if (is_const_zero(op->condition)) {
            // Assertion always succeeds here, since allocation is never used
            check << print_expr(const_true());
        } else {
            // Assert that the allocation worked....
            check << "((" << op_name << " != nullptr) || (" << size_id << " == 0))";
            if (!is_const_one(op->condition)) {
                // ...but if the condition is false, it's OK for the new_expr to be null.
                string op_condition = print_assignment(Bool(), print_expr(op->condition));
                check << " || (!" << op_condition << ")";
            }
        }
        create_assertion(check.str(), Call::make(Int(32), "halide_error_out_of_memory", {}, Call::Extern));

        string free_function = op->free_function.empty() ?
                                   (op->memory_type != MemoryType::VTCM ? "halide_free" : "halide_tcm_free") :
                                   op->free_function;

        emit_halide_free_helper(op_name, free_function);
    }

    op->body.accept(this);

    // Free the memory if it was allocated on the heap and there is no matching
    // Free node.
    print_heap_free(op->name);
    if (allocations.contains(op->name)) {
        allocations.pop(op->name);
    }

    close_scope("alloc " + print_name(op->name));
}

void CodeGen_Xtensa::visit(const Let *op) {
    const auto *call = op->value.as<Call>();
    if (call && (call->name == "clamped_dense_ramp")) {
        Expr body = substitute(op->name, call, op->body);
        body.accept(this);
        return;
    }
    return CodeGen_C::visit(op);
}

void CodeGen_Xtensa::visit(const LetStmt *op) {
    const auto *call = op->value.as<Call>();
    if (call && (call->name == "clamped_dense_ramp")) {
        Stmt body = substitute(op->name, call, op->body);
        body.accept(this);
        return;
    }
    return CodeGen_C::visit(op);
}

}  // namespace Internal
}  // namespace Halide
