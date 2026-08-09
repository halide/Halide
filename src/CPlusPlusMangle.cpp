#include "CPlusPlusMangle.h"

#include <map>

#include "ExternFuncArgument.h"
#include "Function.h"
#include "IR.h"
#include "IROperator.h"
#include "Type.h"

/** \file Support for creating C++ mangled function names from a type signature. */

/** \note For Itanium C++ ABI, there is a specification here:
 *     https://mentorembedded.github.io/cxx-abi/abi.html
 * There is also useful info here:
 *     http://www.agner.org/optimize/calling_conventions.pdf
 */

namespace Halide {

namespace Internal {

namespace {

// Used in both Windows and Itanium manglers to track pieces of a type name
// in both their final form in the output and their canonical substituted form.
struct MangledNamePart {
    std::string full_name;
    std::string with_substitutions;

    MangledNamePart() = default;
    MangledNamePart(const std::string &mangled)
        : full_name(mangled), with_substitutions(mangled) {
    }
    MangledNamePart(const char *mangled)
        : full_name(mangled), with_substitutions(mangled) {
    }
};

Type non_null_void_star_type() {
    static halide_handle_cplusplus_type t(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Simple, "void"),
        {}, {}, {halide_handle_cplusplus_type::Pointer}));
    return Handle(&t);
}

namespace WindowsMangling {

struct PreviousDeclarations {
    std::map<std::string, int> prev_types;
    std::map<std::string, int> prev_names;

    std::string check_and_enter(std::map<std::string, int> &table, const std::string &name, const std::string &full) {
        int sub = -1;
        if (table.size() >= 10) {
            auto i = table.find(name);
            if (i != table.end()) {
                sub = i->second;
            }
        } else {
            auto insert_result = table.insert({name, table.size()});
            if (!insert_result.second) {
                sub = insert_result.first->second;
            }
        }
        if (sub != -1) {
            return std::string(1, (char)('0' + sub));
        } else {
            return full;
        }
    }

    std::string check_and_enter_type(const MangledNamePart &mangled) {
        if (mangled.full_name.size() < 2) {
            return mangled.full_name;
        }
        return check_and_enter(prev_types, mangled.full_name, mangled.with_substitutions);
    }

    std::string check_and_enter_name(const std::string &name) {
        return check_and_enter(prev_names, name, name + "@");
    }
};

std::string simple_type_to_mangle_char(const std::string &type_name, const Target &target) {
    if (type_name == "void") {
        return "X";
    } else if (type_name == "bool") {
        return "_N";
    } else if (type_name == "char") {
        return "D";
    }
    if (type_name == "int8_t") {
        return "C";
    } else if (type_name == "uint8_t") {
        return "E";
    } else if (type_name == "int16_t") {
        return "F";
    } else if (type_name == "uint16_t") {
        return "G";
    } else if (type_name == "int32_t") {
        return "H";
    } else if (type_name == "uint32_t") {
        return "I";
    } else if (type_name == "int64_t") {
        return "_J";
    } else if (type_name == "uint64_t") {
        return "_K";
    } else if (type_name == "float") {
        return "M";
    } else if (type_name == "double") {
        return "N";
    }
    user_error << "Unknown type name: " << type_name << "\n";
    return "";
}

std::string one_qualifier_set(bool is_const, bool is_volatile, bool is_restrict, bool is_pointer_target, const std::string &base_mode) {
    if (is_const && is_volatile) {
        return (is_pointer_target ? ("S" + base_mode) : "D");
    } else if (is_const) {
        return (is_pointer_target ? ("Q" + base_mode) : "B");
    } else if (is_volatile) {
        return (is_pointer_target ? ("R" + base_mode) : "C");
    } else if (is_restrict && is_pointer_target) {
        return ("P" + base_mode + "I");
    } else {
        return (is_pointer_target ? ("P" + base_mode) : "A");
    }
}

struct QualsState {
    bool last_is_pointer{false};

    const Type &type;
    const std::string base_mode;
    std::string result;

    bool finished{false};

    QualsState(const Type &type, const std::string &base_mode)
        : type(type), base_mode(base_mode) {
    }

    void handle_modifier(uint8_t modifier) {
        bool is_pointer = (modifier & halide_handle_cplusplus_type::Pointer) != 0;
        bool last_is_const = (modifier & halide_handle_cplusplus_type::Const) != 0;
        bool last_is_volatile = (modifier & halide_handle_cplusplus_type::Volatile) != 0;
        bool last_is_restrict = (modifier & halide_handle_cplusplus_type::Restrict) != 0;

        if (finished ||
            (!is_pointer && !last_is_pointer &&
             type.handle_type()->reference_type == halide_handle_cplusplus_type::NotReference)) {
            finished = true;
            return;
        }

        result = one_qualifier_set(last_is_const, last_is_volatile, last_is_restrict, last_is_pointer, base_mode) + result;
        if (last_is_pointer && (is_pointer || type.handle_type()->reference_type != halide_handle_cplusplus_type::NotReference)) {
            result = one_qualifier_set(last_is_const, last_is_volatile, last_is_restrict, false, base_mode) + result;
        }

        last_is_pointer = is_pointer;
        if (!is_pointer) {
            finished = true;
        }
    }

    void final() {
        if (!finished) {
            handle_modifier(0);
        }
        if (last_is_pointer) {
            result = one_qualifier_set(false, false, false, last_is_pointer, base_mode) + result;
        }

        if (type.handle_type()->reference_type == halide_handle_cplusplus_type::LValueReference) {
            result = "A" + base_mode + result;  // Or is it "R"?
        } else if (type.handle_type()->reference_type == halide_handle_cplusplus_type::RValueReference) {
            result = "$$Q" + base_mode + result;
        }
    }

    const std::string &get_result() const {
        return result;
    }
};

std::string mangle_indirection_and_cvr_quals(const Type &type, const Target &target) {
    QualsState state(type, (target.bits == 64) ? "E" : "");
    for (uint8_t modifier : type.handle_type()->cpp_type_modifiers) {
        state.handle_modifier(modifier);
    }
    state.final();

    return state.get_result();
}

MangledNamePart mangle_inner_name(const Type &type, const Target &target, PreviousDeclarations &prev_decls) {
    MangledNamePart result("");

    std::string quals = mangle_indirection_and_cvr_quals(type, target);
    if (type.handle_type()->inner_name.cpp_type_type == halide_cplusplus_type_name::Simple) {
        return quals + simple_type_to_mangle_char(type.handle_type()->inner_name.name, target);
    } else {
        std::string code;
        if (type.handle_type()->inner_name.cpp_type_type == halide_cplusplus_type_name::Struct) {
            code = "U";
        } else if (type.handle_type()->inner_name.cpp_type_type == halide_cplusplus_type_name::Class) {
            code = "V";
        } else if (type.handle_type()->inner_name.cpp_type_type == halide_cplusplus_type_name::Union) {
            code = "T";
        } else if (type.handle_type()->inner_name.cpp_type_type == halide_cplusplus_type_name::Enum) {
            code = "W4";
        }
        result.full_name = quals + code + type.handle_type()->inner_name.name + "@";
        result.with_substitutions = quals + code + prev_decls.check_and_enter_name(type.handle_type()->inner_name.name);

        for (const auto &enclosing_type : reverse_view(type.handle_type()->enclosing_types)) {
            result.full_name += enclosing_type.name + "@";
            result.with_substitutions += prev_decls.check_and_enter_name(enclosing_type.name);
        }

        for (const auto &ns : reverse_view(type.handle_type()->namespaces)) {
            result.full_name += ns + "@";
            result.with_substitutions += prev_decls.check_and_enter_name(ns);
        }

        result.full_name += "@";
        result.with_substitutions += "@";

        return result;
    }
}

MangledNamePart mangle_type(const Type &type, const Target &target, PreviousDeclarations &prev_decls) {
    if (type.is_int()) {
        switch (type.bits()) {
        case 8:
            return "C";
        case 16:
            return "F";
        case 32:
            return "H";
        case 64:
            return "_J";
        default:
            internal_error << "Unexpected integer size: " << type.bits() << ".\n";
            return "";
        }
    } else if (type.is_uint()) {
        switch (type.bits()) {
        case 1:
            return "_N";
        case 8:
            return "E";
        case 16:
            return "G";
        case 32:
            return "I";
        case 64:
            return "_K";
        default:
            internal_error << "Unexpected unsigned integer size: " << type.bits() << "\n";
            return "";
        }
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            return "M";
        } else if (type.bits() == 64) {
            return "N";
        }
        internal_error << "Unexpected floating-point type size: " << type.bits() << ".\n";
        return "";
    } else if (type.is_handle()) {
        return mangle_inner_name((type.handle_type() != nullptr) ? type : non_null_void_star_type(),
                                 target, prev_decls);
    }
    internal_error << "Unexpected kind of type. Code: " << type.code() << "\n";
    return "";
}

std::string cplusplus_function_mangled_name(const std::string &name, const std::vector<std::string> &namespaces,
                                            Type return_type, const std::vector<ExternFuncArgument> &args,
                                            const Target &target) {
    std::string result("\1?");

    PreviousDeclarations prev_decls;
    result += prev_decls.check_and_enter_name(name);

    for (const auto &ns : reverse_view(namespaces)) {
        result += prev_decls.check_and_enter_name(ns);
    }
    result += "@";

    result += "YA";

    result += prev_decls.check_and_enter_type(mangle_type(return_type, target, prev_decls));

    if (args.empty()) {
        result += "X";
    } else {
        for (const auto &arg : args) {
            result += prev_decls.check_and_enter_type(mangle_type(arg.is_expr() ? arg.expr.type() : type_of<struct halide_buffer_t *>(), target, prev_decls));
        }
        // I think ending in a 'Z' only happens for nested function types, which never
        // occurs with Halide, but putting it in anyway per.
        // http://www.agner.org/optimize/calling_conventions.pdf
        if (result.back() != 'Z') {
            result += "@";
        }
    }
    result += "Z";

    return result;
}

}  // namespace WindowsMangling

namespace ItaniumABIMangling {

std::string itanium_mangle_id(const std::string &id) {
    std::ostringstream oss;
    oss << id.size() << id;
    return oss.str();
}

std::string simple_type_to_mangle_char(const std::string &type_name, const Target &target) {
    if (type_name == "void") {
        return "v";
    } else if (type_name == "bool") {
        return "b";
    } else if (type_name == "char") {
        return "c";
    }
    if (type_name == "int8_t") {
        return "a";
    } else if (type_name == "uint8_t") {
        return "h";
    } else if (type_name == "int16_t") {
        return "s";
    } else if (type_name == "uint16_t") {
        return "t";
    } else if (type_name == "int32_t") {
        return "i";
    } else if (type_name == "uint32_t") {
        return "j";
    } else if (type_name == "int64_t") {
        if (target.os == Target::OSX ||
            target.os == Target::IOS ||
            target.bits == 32) {
            return "x";
        } else {
            return "l";
        }
    } else if (type_name == "uint64_t") {
        if (target.os == Target::OSX ||
            target.os == Target::IOS ||
            target.bits == 32) {
            return "y";
        } else {
            return "m";
        }
    } else if (type_name == "float") {
        return "f";
    } else if (type_name == "double") {
        return "d";
    }
    user_error << "Unknown type name: " << type_name << "\n";
    return "";
}

struct Quals {
    std::string modifiers;
    std::string indirections;
};

struct PrevPrefixes {
    std::map<std::string, int32_t> prev_seen;

    bool check_and_enter(const std::string &prefix, std::string &substitute) {
        auto place = prev_seen.insert({prefix, prev_seen.size()});
        if (place.first->second == 0) {
            substitute = "S_";
        } else {
            // Convert to base 36, using digits and upper case letters for each digit.
            std::string seq_id;
            int32_t to_encode = place.first->second - 1;
            do {
                int least_sig_digit = to_encode % 36;
                if (least_sig_digit < 10) {
                    seq_id = std::string(1, (char)('0' + least_sig_digit)) + seq_id;
                } else {
                    seq_id = (char)('A' + (least_sig_digit - 10)) + seq_id;
                }
                to_encode /= 36;
            } while (to_encode > 0);
            substitute = "S" + seq_id + "_";
        }
        return !place.second;
    }

    bool extend_name_part(MangledNamePart &name_part, const std::string &mangled) {
        std::string substitute;
        bool found = check_and_enter(name_part.with_substitutions + mangled, substitute);
        if (found) {
            name_part.full_name = substitute;
        } else {
            name_part.full_name = name_part.full_name + mangled;
        }
        name_part.with_substitutions = substitute;
        return found;
    }

    bool prepend_name_part(const std::string &mangled, MangledNamePart &name_part) {
        std::string substitute;
        bool found = check_and_enter(mangled + name_part.with_substitutions, substitute);
        if (found) {
            name_part.full_name = substitute;
        } else {
            name_part.full_name = mangled + name_part.full_name;
        }
        name_part.with_substitutions = substitute;
        return found;
    }
};

MangledNamePart apply_indirection_and_cvr_quals(const Type &type, MangledNamePart &name_part,
                                                PrevPrefixes &prevs) {
    for (uint8_t modifier : type.handle_type()->cpp_type_modifiers) {
        // Qualifiers on a value type are simply not encoded.
        // E.g. "int f(const int)" mangles the same as "int f(int)".
        if (!(modifier & halide_handle_cplusplus_type::Pointer) &&
            type.handle_type()->reference_type == halide_handle_cplusplus_type::NotReference) {
            break;
        }

        std::string quals;

        if (modifier & halide_handle_cplusplus_type::Restrict) {
            quals += "r";
        }
        if (modifier & halide_handle_cplusplus_type::Volatile) {
            quals += "V";
        }
        if (modifier & halide_handle_cplusplus_type::Const) {
            quals += "K";
        }

        if (!quals.empty()) {
            prevs.prepend_name_part(quals, name_part);
        }

        if (modifier & halide_handle_cplusplus_type::Pointer) {
            prevs.prepend_name_part("P", name_part);
        } else {
            break;
        }
    }

    if (type.handle_type()->reference_type == halide_handle_cplusplus_type::LValueReference) {
        prevs.prepend_name_part("R", name_part);
    } else if (type.handle_type()->reference_type == halide_handle_cplusplus_type::RValueReference) {
        prevs.prepend_name_part("O", name_part);
    }

    return name_part;
}

MangledNamePart mangle_qualified_name(const std::string &name, const std::vector<std::string> &namespaces,
                                      const std::vector<halide_cplusplus_type_name> &enclosing_types,
                                      bool can_substitute, PrevPrefixes &prevs) {
    MangledNamePart result;

    // Nested names start with N and then have the enclosing scope names.
    bool is_directly_in_std = enclosing_types.empty() && (namespaces.size() == 1 && namespaces[0] == "std");
    bool not_simple = !is_directly_in_std && (!namespaces.empty() || !enclosing_types.empty());
    std::string substitute;
    if (is_directly_in_std) {
        // TODO: more cases here.
        if (name == "allocator") {
            return "Sa";
        } else if (name == "string") {  // Not correct, but it does the right thing
            return "Ss";
        }
        result.full_name += "St";
        result.with_substitutions += "St";
    } else if (not_simple) {
        for (const auto &ns : namespaces) {
            if (ns == "std") {
                result.full_name += "St";
                result.with_substitutions += "St";
            } else {
                prevs.extend_name_part(result, itanium_mangle_id(ns));
            }
        }
        for (const auto &et : enclosing_types) {
            prevs.extend_name_part(result, itanium_mangle_id(et.name));
        }
    }

    std::string mangled = itanium_mangle_id(name);
    bool substituted = false;
    if (can_substitute) {
        substituted = prevs.extend_name_part(result, mangled);
    } else {
        result.full_name += mangled;
        result.with_substitutions += mangled;
    }
    if (not_simple && !substituted) {
        result.full_name = "N" + result.full_name + "E";
    }

    return result;
}

std::string mangle_inner_name(const Type &type, const Target &target, PrevPrefixes &prevs) {
    if (type.handle_type()->inner_name.cpp_type_type == halide_cplusplus_type_name::Simple) {
        MangledNamePart result = simple_type_to_mangle_char(type.handle_type()->inner_name.name, target);
        return apply_indirection_and_cvr_quals(type, result, prevs).full_name;
    } else {
        MangledNamePart mangled = mangle_qualified_name(type.handle_type()->inner_name.name, type.handle_type()->namespaces,
                                                        type.handle_type()->enclosing_types, true, prevs);
        return apply_indirection_and_cvr_quals(type, mangled, prevs).full_name;
    }
}

std::string mangle_type(const Type &type, const Target &target, PrevPrefixes &prevs) {
    if (type.is_int()) {
        switch (type.bits()) {
        case 8:
            return "a";
        case 16:
            return "s";
        case 32:
            if (target.arch == Target::Hexagon) {
                return "l";
            } else {
                return "i";
            }
        case 64:
            if (target.os == Target::OSX ||
                target.os == Target::IOS ||
                target.bits == 32) {
                return "x";
            } else {
                return "l";
            }
        default:
            internal_error << "Unexpected integer size: " << type.bits() << ".\n";
            return "";
        }
    } else if (type.is_uint()) {
        switch (type.bits()) {
        case 1:
            return "b";
        case 8:
            return "h";
        case 16:
            return "t";
        case 32:
            if (target.arch == Target::Hexagon) {
                return "m";
            } else {
                return "j";
            }
        case 64:
            if (target.os == Target::OSX ||
                target.os == Target::IOS ||
                target.bits == 32) {
                return "y";
            } else {
                return "m";
            }
        default:
            internal_error << "Unexpected unsigned integer size: " << type.bits() << "\n";
            return "";
        }
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            return "f";
        } else if (type.bits() == 64) {
            return "d";
        }
        internal_error << "Unexpected floating-point type size: " << type.bits() << ".\n";
        return "";
    } else if (type.is_handle()) {
        return mangle_inner_name((type.handle_type() != nullptr) ? type : non_null_void_star_type(),
                                 target, prevs);
    }
    internal_error << "Unexpected kind of type. Code: " << type.code() << "\n";
    return "";
}

std::string cplusplus_function_mangled_name(const std::string &name, const std::vector<std::string> &namespaces,
                                            Type return_type, const std::vector<ExternFuncArgument> &args,
                                            const Target &target) {
    std::string result("_Z");

    PrevPrefixes prevs;
    result += mangle_qualified_name(name, namespaces, {}, false, prevs).full_name;

    if (args.empty()) {
        result += "v";
    }

    for (const auto &arg : args) {
        result += mangle_type(arg.is_expr() ? arg.expr.type() : type_of<struct halide_buffer_t *>(), target, prevs);
    }

    return result;
}

}  // namespace ItaniumABIMangling

}  // namespace

std::string cplusplus_function_mangled_name(const std::string &name, const std::vector<std::string> &namespaces,
                                            Type return_type, const std::vector<ExternFuncArgument> &args,
                                            const Target &target) {
    if (target.os == Target::Windows) {
        return WindowsMangling::cplusplus_function_mangled_name(name, namespaces, return_type, args, target);
    } else {
        return ItaniumABIMangling::cplusplus_function_mangled_name(name, namespaces, return_type, args, target);
    }
}

}  // namespace Internal

}  // namespace Halide
