#include "CPlusPlusMangle.h"

#include <map>

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
    MangledNamePart(const std::string &mangled) : full_name(mangled), with_substitutions(mangled) { }
    MangledNamePart(const char *mangled) : full_name(mangled), with_substitutions(mangled) { }
};

Type non_null_void_star_type() {
    static halide_handle_cplusplus_type t(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Simple, "void"),
        { }, { }, { halide_handle_cplusplus_type::Pointer }));
    return Handle(1, &t);
}

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
            auto insert_result = table.insert({ name, table.size() });
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

    std::string check_and_enter_name(std::string name) {
        return check_and_enter(prev_names, name, name + "@");
    }
};

std::string simple_type_to_mangle_char(const std::string type_name, const Target &target) {
    if (type_name == "void") {
        return "X";
    } else if (type_name == "bool") {
        return "_N";
    } else if (type_name == "char") {
        return "D";
    } if (type_name == "int8_t") {
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
        return  ("P" + base_mode + "I");
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

    QualsState(const Type &type, const std::string &base_mode) : type(type), base_mode(base_mode) { }

    void handle_modifier(uint8_t modifier) {
        bool is_pointer = modifier & halide_handle_cplusplus_type::Pointer;
        bool last_is_const = modifier & halide_handle_cplusplus_type::Const;
        bool last_is_volatile = modifier & halide_handle_cplusplus_type::Volatile;
        bool last_is_restrict = modifier & halide_handle_cplusplus_type::Restrict;

        if (finished ||
            (!is_pointer && !last_is_pointer &&
             type.handle_type->reference_type == halide_handle_cplusplus_type::NotReference)) {
            finished = true;
            return;
        }

        result = one_qualifier_set(last_is_const, last_is_volatile, last_is_restrict, last_is_pointer, base_mode) + result;
        if (last_is_pointer && (is_pointer || type.handle_type->reference_type != halide_handle_cplusplus_type::NotReference)) {
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

        if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
            result = "A" + base_mode + result; // Or is it "R"?
        } else if (type.handle_type->reference_type == halide_handle_cplusplus_type::RValueReference) {
            result = "$$Q" + base_mode + result;
        }
    }

    const std::string &get_result() {
        return result;
    }
};

std::string mangle_indirection_and_cvr_quals(const Type &type, const Target &target) {
    QualsState state(type, (target.bits == 64) ? "E" : "");
    for (uint8_t modifier : type.handle_type->cpp_type_modifiers) {
        state.handle_modifier(modifier);
    }
    state.final();

    return state.get_result();
}

MangledNamePart mangle_inner_name(const Type &type, const Target &target, PreviousDeclarations &prev_decls) {
    MangledNamePart result("");

    std::string quals = mangle_indirection_and_cvr_quals(type, target);
    if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Simple) {
        return quals + simple_type_to_mangle_char(type.handle_type->inner_name.name, target);
    } else {
        std::string code;
        if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Struct) {
            code = "U";
        } else if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Class) {
            code = "V";
        } else if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Union) {
            code = "T";
        } else if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Enum) {
            code = "W4";
        }
        result.full_name = quals + code + type.handle_type->inner_name.name + "@";
        result.with_substitutions = quals + code + prev_decls.check_and_enter_name(type.handle_type->inner_name.name);

        for (size_t i = type.handle_type->enclosing_types.size(); i > 0; i--) {
            result.full_name += type.handle_type->enclosing_types[i - 1].name + "@";
            result.with_substitutions += prev_decls.check_and_enter_name(type.handle_type->enclosing_types[i - 1].name);
        }

        for (size_t i = type.handle_type->namespaces.size(); i > 0; i--) {
            result.full_name += type.handle_type->namespaces[i - 1] + "@";
            result.with_substitutions += prev_decls.check_and_enter_name(type.handle_type->namespaces[i - 1]);
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
         }
        internal_error << "Unexpected integer size: " << type.bits() << ".\n";
        return "";
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
        }
        internal_error << "Unexpected unsigned integer size: " << type.bits() << "\n";
        return "";
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            return "M";
        } else if (type.bits() == 64) {
            return "N";
        }
        internal_error << "Unexpected floating-point type size: " << type.bits() << ".\n";
        return "";
    } else if (type.is_handle()) {
        return mangle_inner_name((type.handle_type != nullptr) ? type : non_null_void_star_type(),
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

    for (size_t i = namespaces.size(); i > 0; i--) {
        result += prev_decls.check_and_enter_name(namespaces[i - 1]);
    }
    result += "@";

    result += "YA";

    result += prev_decls.check_and_enter_type(mangle_type(return_type, target, prev_decls));

    if (args.size() == 0) {
        result += "X";
    } else {
        for (const auto &arg : args) {
            result += prev_decls.check_and_enter_type(mangle_type(arg.is_expr() ? arg.expr.type() : type_of<struct buffer_t *>(), target, prev_decls));
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

}

namespace ItaniumABIMangling {

std::string itanium_mangle_id(std::string id) {
    std::ostringstream oss;
    oss << id.size() << id;
    return oss.str();
}

std::string simple_type_to_mangle_char(const std::string type_name, const Target &target) {
    if (type_name == "void") {
        return "v";
    } else if (type_name == "bool") {
        return "b";
    } else if (type_name == "char") {
        return "c";
    } if (type_name == "int8_t") {
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
            target.bits == 32 ||
            target.has_feature(Target::MinGW)) {
            return "x";
        } else {
            return "l";
        }
    } else if (type_name == "uint64_t") {
        if (target.os == Target::OSX ||
            target.bits == 32 ||
            target.has_feature(Target::MinGW)) {
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
        auto place = prev_seen.insert({ prefix, prev_seen.size() });
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

    bool extend_name_part(MangledNamePart &name_part, const std::string mangled) {
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

    bool prepend_name_part(const std::string mangled, MangledNamePart &name_part) {
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
    for (uint8_t modifier : type.handle_type->cpp_type_modifiers) {
        // Qualifiers on a value type are simply not encoded.
        // E.g. "int f(const int)" mangles the same as "int f(int)".
        if (!(modifier & halide_handle_cplusplus_type::Pointer) &&
            type.handle_type->reference_type == halide_handle_cplusplus_type::NotReference) {
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

    if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
        prevs.prepend_name_part("R", name_part);
    } else if (type.handle_type->reference_type == halide_handle_cplusplus_type::RValueReference) {
        prevs.prepend_name_part("O", name_part);
    }

    return name_part;
}

MangledNamePart mangle_qualified_name(std::string name, const std::vector<std::string> &namespaces,
                                      const std::vector<halide_cplusplus_type_name> &enclosing_types,
                                      bool can_substitute, PrevPrefixes &prevs) {
    MangledNamePart result;

    // Nested names start with N and then have the enclosing scope names.
    bool is_directly_in_std = enclosing_types.size() == 0 && (namespaces.size() == 1 && namespaces[0] == "std");
    bool not_simple = !is_directly_in_std && (!namespaces.empty() || !enclosing_types.empty());
    std::string substitute;
    if (is_directly_in_std) {
        // TODO: more cases here.
        if (name == "allocator") {
          return "Sa";
        } else if (name == "string") { // Not correct, but it does the right thing
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
    if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Simple) {
        MangledNamePart result = simple_type_to_mangle_char(type.handle_type->inner_name.name, target);
        return apply_indirection_and_cvr_quals(type, result, prevs).full_name;
    } else {
        MangledNamePart mangled = mangle_qualified_name(type.handle_type->inner_name.name, type.handle_type->namespaces,
                                                        type.handle_type->enclosing_types, true, prevs);
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
            return "i";
          case 64:
            if (target.os == Target::OSX ||
                target.bits == 32 ||
                target.has_feature(Target::MinGW)) {
                return "x";
            } else {
                return "l";
            }
        }
        internal_error << "Unexpected integer size: " << type.bits() << ".\n";
        return "";
    } else if (type.is_uint()) {
        switch (type.bits()) {
          case 1:
            return "b";
          case 8:
            return "h";
          case 16:
            return "t";
          case 32:
            return "j";
          case 64:
            if (target.os == Target::OSX ||
                target.bits == 32 ||
                target.has_feature(Target::MinGW)) {
                return "y";
            } else {
                return "m";
            }
        }
        internal_error << "Unexpected unsigned integer size: " << type.bits() << "\n";
        return "";
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            return "f";
        } else if (type.bits() == 64) {
            return "d";
        }
        internal_error << "Unexpected floating-point type size: " << type.bits() << ".\n";
        return "";
    } else if (type.is_handle()) {
        return mangle_inner_name((type.handle_type != nullptr) ? type : non_null_void_star_type(),
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

    if (args.size() == 0) {
        result += "v";
    }

    for (const auto &arg : args) {
        result += mangle_type(arg.is_expr() ? arg.expr.type() : type_of<struct buffer_t *>(), target, prevs);
    }

    return result;
}

} // namespace ItaniumABIMangling

std::string cplusplus_function_mangled_name(const std::string &name, const std::vector<std::string> &namespaces,
                                            Type return_type, const std::vector<ExternFuncArgument> &args,
                                            const Target &target) {
    if (target.os == Target::Windows && !target.has_feature(Target::MinGW)) {
        return WindowsMangling::cplusplus_function_mangled_name(name, namespaces, return_type, args, target);
    } else {
        return ItaniumABIMangling::cplusplus_function_mangled_name(name, namespaces, return_type, args, target);
    }
}

// All code below is for tests.

namespace {

struct MangleResult {
  const char *expected;
  const char *label;
};

MangleResult ItaniumABIMangling_main[] = {
  { "_Z13test_functionv", "int32_t test_function(void)" },
  { "_ZN3foo13test_functionEv", "int32_t foo::test_function(void)" },
  { "_ZN3foo3bar13test_functionEv", "int32_t foo::bar::test_function(void)" },
  { "_ZN3foo3bar13test_functionEi", "int32_t foo::test_function(int32_t)" },
  { "_ZN3foo3bar13test_functionEiP8buffer_t", "int32_t foo::test_function(int32_t, struct buffer_t *)" },
  { "_ZN14test_namespace14test_namespace13test_functionENS0_15enclosing_class11test_structE",
    "test_namespace::test_namespace::test_function(test_namespace::test_namespace::enclosing_class::test_struct)" },
  { "_ZN3foo3bar13test_functionEiP8buffer_tS2_", "foo::bar::test_function(int, buffer_t*, buffer_t*)" },
  { "_ZN14test_namespace14test_namespace13test_functionEPNS_11test_structEPKS1_", "test_namespace::test_namespace::test_function(test_namespace::test_struct*, test_namespace::test_struct const*)" },
  { "_ZN14test_namespace14test_namespace13test_functionENS0_15enclosing_class11test_structES2_",
    "test_namespace::test_namespace::test_function(test_namespace::test_namespace::enclosing_class::test_struct, test_namespace::test_namespace::enclosing_class::test_struct)" },
  { "_ZSt13test_functionv", "std::test_function()" },
  { "_ZNSt3foo13test_functionEv", "std::foo::test_function()" },
  { "_ZSt13test_functionNSt15enclosing_class11test_structE", "std::test_function(std::enclosing_class::test_struct)" },
  { "_ZN14test_namespace14test_namespace13test_functionEPNS_10test_classE", "test_namespace::test_namespace::test_function(test_namespace::test_class*)" },
  { "_ZN14test_namespace14test_namespace13test_functionEPNS_10test_unionE", "test_namespace::test_namespace::test_function(test_namespace::test_union*)" },
  { "_ZN14test_namespace14test_namespace13test_functionEPNS_9test_enumE", "test_namespace::test_namespace::test_function(test_namespace::test_enum*)" },
};

MangleResult win32_expecteds[] = {
  { "\001?test_function@@YAHXZ", "int32_t test_function(void)" },
  { "\001?test_function@foo@@YAHXZ", "int32_t foo::test_function(void)" },
  { "\001?test_function@bar@foo@@YAHXZ", "int32_t foo::bar::test_function(void)" },
  { "\001?test_function@bar@foo@@YAHH@Z", "int32_t foo::test_function(int32_t)" },
  { "\001?test_function@bar@foo@@YAHHPAUbuffer_t@@@Z", "int32_t foo::test_function(int32_t, struct buffer_t *)" },
  { "\001?test_function@test_namespace@1@YAHUtest_struct@enclosing_class@11@@Z",
    "test_namespace::test_namespace::test_function(test_namespace::test_namespace::enclosing_class::test_struct)" },
  { "\001?test_function@bar@foo@@YAHHPAUbuffer_t@@0@Z", "foo::bar::test_function(int, buffer_t*, buffer_t*)" },
  { "\001?test_function@test_namespace@1@YAHPAUtest_struct@1@PBU21@@Z", "test_namespace::test_namespace::test_function(test_namespace::test_struct*, test_namespace::test_struct const*)" },
  { "\001?test_function@test_namespace@1@YAHUtest_struct@enclosing_class@11@0@Z",
    "test_namespace::test_namespace::test_function(test_namespace::test_namespace::enclosing_class::test_struct, test_namespace::test_namespace::enclosing_class::test_struct)" },
  { "\001?test_function@std@@YAHXZ", "std::test_function()" },
  { "\001?test_function@foo@std@@YAHXZ", "std::foo::test_function()" },
  { "\001?test_function@std@@YAHUtest_struct@enclosing_class@1@@Z", "std::test_function(std::enclosing_class::test_struct)" },
  { "\001?test_function@test_namespace@1@YAHPAVtest_class@1@@Z", "test_namespace::test_namespace::test_function(test_namespace::test_class*)" },
  { "\001?test_function@test_namespace@1@YAHPATtest_union@1@@Z", "test_namespace::test_namespace::test_function(test_namespace::test_union*)" },
  { "\001?test_function@test_namespace@1@YAHPAVtest_enum@1@@Z", "test_namespace::test_namespace::test_function(test_namespace::test_enum*)" },
};

MangleResult win64_expecteds[] = {
  { "\001?test_function@@YAHXZ", "int32_t test_function(void)" },
  { "\001?test_function@foo@@YAHXZ", "int32_t foo::test_function(void)" },
  { "\001?test_function@bar@foo@@YAHXZ", "int32_t foo::bar::test_function(void)" },
  { "\001?test_function@bar@foo@@YAHH@Z", "int32_t foo::test_function(int32_t)" },
  { "\001?test_function@bar@foo@@YAHHPEAUbuffer_t@@@Z", "int32_t foo::test_function(int32_t, struct buffer_t *)" },
  { "\001?test_function@test_namespace@1@YAHUtest_struct@enclosing_class@11@@Z",
    "test_namespace::test_namespace::test_function(test_namespace::test_namespace::enclosing_class::test_struct)" },
  { "\001?test_function@bar@foo@@YAHHPEAUbuffer_t@@0@Z", "foo::bar::test_function(int, buffer_t*, buffer_t*)" },
  { "\001?test_function@test_namespace@1@YAHPEAUtest_struct@1@PEBU21@@Z", "test_namespace::test_namespace::test_function(test_namespace::test_struct*, test_namespace::test_struct const*)" },
  { "\001?test_function@test_namespace@1@YAHUtest_struct@enclosing_class@11@0@Z",
    "test_namespace::test_namespace::test_function(test_namespace::test_namespace::enclosing_class::test_struct, test_namespace::test_namespace::enclosing_class::test_struct)" },
  { "\001?test_function@std@@YAHXZ", "std::test_function()" },
  { "\001?test_function@foo@std@@YAHXZ", "std::foo::test_function()" },
  { "\001?test_function@std@@YAHUtest_struct@enclosing_class@1@@Z", "std::test_function(std::enclosing_class::test_struct)" },
  { "\001?test_function@test_namespace@1@YAHPEAVtest_class@1@@Z", "test_namespace::test_namespace::test_function(test_namespace::test_class*)" },
  { "\001?test_function@test_namespace@1@YAHPEATtest_union@1@@Z", "test_namespace::test_namespace::test_function(test_namespace::test_union*)" },
  { "\001?test_function@test_namespace@1@YAHPEAVtest_enum@1@@Z", "test_namespace::test_namespace::test_function(test_namespace::test_enum*)" },
};

MangleResult all_types_by_target[] = {
  { "_Z13test_functionbahstijxyfd", "test_function(bool, signed char, unsigned char, short, unsigned short, int, unsigned int, long long, unsigned long long, float, double)" },
  { "_Z13test_functionbahstijlmfd", "test_function(bool, signed char, unsigned char, short, unsigned short, int, unsigned int, long, unsigned long, float, double)" },
  { "_Z13test_functionbahstijxyfd", "test_function(bool, signed char, unsigned char, short, unsigned short, int, unsigned int, long long, unsigned long long, float, double)" },
  { "_Z13test_functionbahstijxyfd", "test_function(bool, signed char, unsigned char, short, unsigned short, int, unsigned int, long, unsigned long, float, double)" },
  { "\001?test_function@@YAH_NCEFGHI_J_KMN@Z", "test_function(bool, signed char, unsigned char, short, unsigned short, int, unsigned int, long long, unsigned long long, float, double)" },
  { "\001?test_function@@YAH_NCEFGHI_J_KMN@Z", "test_function(bool, signed char, unsigned char, short, unsigned short, int, unsigned int, long long, unsigned long long, float, double)" },
};

const char *many_type_subs_itanium = "_Z13test_functionPN14test_namespace2s0EPNS_2s1EPNS_2s2EPNS_2s3EPNS_2s4EPNS_2s5EPNS_2s6EPNS_2s7EPNS_2s8EPNS_2s9EPNS_3s10EPNS_3s11EPNS_3s12EPNS_3s13EPNS_3s14EPNS_3s15EPNS_3s16EPNS_3s17EPNS_3s18EPNS_3s19EPNS_3s20EPNS_3s21EPNS_3s22EPNS_3s23EPNS_3s24EPNS_3s25EPNS_3s26EPNS_3s27EPNS_3s28EPNS_3s29EPNS_3s30EPNS_3s31EPNS_3s32EPNS_3s33EPNS_3s34EPNS_3s35EPNS_3s36EPNS_3s37EPNS_3s38EPNS_3s39EPNS_3s40EPNS_3s41EPNS_3s42EPNS_3s43EPNS_3s44EPNS_3s45EPNS_3s46EPNS_3s47EPNS_3s48EPNS_3s49EPNS_3s50EPNS_3s51EPNS_3s52EPNS_3s53EPNS_3s54EPNS_3s55EPNS_3s56EPNS_3s57EPNS_3s58EPNS_3s59EPNS_3s60EPNS_3s61EPNS_3s62EPNS_3s63EPNS_3s64EPNS_3s65EPNS_3s66EPNS_3s67EPNS_3s68EPNS_3s69EPNS_3s70EPNS_3s71EPNS_3s72EPNS_3s73EPNS_3s74EPNS_3s75EPNS_3s76EPNS_3s77EPNS_3s78EPNS_3s79EPNS_3s80EPNS_3s81EPNS_3s82EPNS_3s83EPNS_3s84EPNS_3s85EPNS_3s86EPNS_3s87EPNS_3s88EPNS_3s89EPNS_3s90EPNS_3s91EPNS_3s92EPNS_3s93EPNS_3s94EPNS_3s95EPNS_3s96EPNS_3s97EPNS_3s98EPNS_3s99ES1_S3_S5_S7_S9_SB_SD_SF_SH_SJ_SL_SN_SP_SR_ST_SV_SX_SZ_S11_S13_S15_S17_S19_S1B_S1D_S1F_S1H_S1J_S1L_S1N_S1P_S1R_S1T_S1V_S1X_S1Z_S21_S23_S25_S27_S29_S2B_S2D_S2F_S2H_S2J_S2L_S2N_S2P_S2R_S2T_S2V_S2X_S2Z_S31_S33_S35_S37_S39_S3B_S3D_S3F_S3H_S3J_S3L_S3N_S3P_S3R_S3T_S3V_S3X_S3Z_S41_S43_S45_S47_S49_S4B_S4D_S4F_S4H_S4J_S4L_S4N_S4P_S4R_S4T_S4V_S4X_S4Z_S51_S53_S55_S57_S59_S5B_S5D_S5F_S5H_S5J_";

const char *many_type_subs_win32 = "\001?test_function@@YAHPAUs0@test_namespace@@PAUs1@2@PAUs2@2@PAUs3@2@PAUs4@2@PAUs5@2@PAUs6@2@PAUs7@2@PAUs8@2@PAUs9@2@PAUs10@2@PAUs11@2@PAUs12@2@PAUs13@2@PAUs14@2@PAUs15@2@PAUs16@2@PAUs17@2@PAUs18@2@PAUs19@2@PAUs20@2@PAUs21@2@PAUs22@2@PAUs23@2@PAUs24@2@PAUs25@2@PAUs26@2@PAUs27@2@PAUs28@2@PAUs29@2@PAUs30@2@PAUs31@2@PAUs32@2@PAUs33@2@PAUs34@2@PAUs35@2@PAUs36@2@PAUs37@2@PAUs38@2@PAUs39@2@PAUs40@2@PAUs41@2@PAUs42@2@PAUs43@2@PAUs44@2@PAUs45@2@PAUs46@2@PAUs47@2@PAUs48@2@PAUs49@2@PAUs50@2@PAUs51@2@PAUs52@2@PAUs53@2@PAUs54@2@PAUs55@2@PAUs56@2@PAUs57@2@PAUs58@2@PAUs59@2@PAUs60@2@PAUs61@2@PAUs62@2@PAUs63@2@PAUs64@2@PAUs65@2@PAUs66@2@PAUs67@2@PAUs68@2@PAUs69@2@PAUs70@2@PAUs71@2@PAUs72@2@PAUs73@2@PAUs74@2@PAUs75@2@PAUs76@2@PAUs77@2@PAUs78@2@PAUs79@2@PAUs80@2@PAUs81@2@PAUs82@2@PAUs83@2@PAUs84@2@PAUs85@2@PAUs86@2@PAUs87@2@PAUs88@2@PAUs89@2@PAUs90@2@PAUs91@2@PAUs92@2@PAUs93@2@PAUs94@2@PAUs95@2@PAUs96@2@PAUs97@2@PAUs98@2@PAUs99@2@0123456789PAUs10@2@PAUs11@2@PAUs12@2@PAUs13@2@PAUs14@2@PAUs15@2@PAUs16@2@PAUs17@2@PAUs18@2@PAUs19@2@PAUs20@2@PAUs21@2@PAUs22@2@PAUs23@2@PAUs24@2@PAUs25@2@PAUs26@2@PAUs27@2@PAUs28@2@PAUs29@2@PAUs30@2@PAUs31@2@PAUs32@2@PAUs33@2@PAUs34@2@PAUs35@2@PAUs36@2@PAUs37@2@PAUs38@2@PAUs39@2@PAUs40@2@PAUs41@2@PAUs42@2@PAUs43@2@PAUs44@2@PAUs45@2@PAUs46@2@PAUs47@2@PAUs48@2@PAUs49@2@PAUs50@2@PAUs51@2@PAUs52@2@PAUs53@2@PAUs54@2@PAUs55@2@PAUs56@2@PAUs57@2@PAUs58@2@PAUs59@2@PAUs60@2@PAUs61@2@PAUs62@2@PAUs63@2@PAUs64@2@PAUs65@2@PAUs66@2@PAUs67@2@PAUs68@2@PAUs69@2@PAUs70@2@PAUs71@2@PAUs72@2@PAUs73@2@PAUs74@2@PAUs75@2@PAUs76@2@PAUs77@2@PAUs78@2@PAUs79@2@PAUs80@2@PAUs81@2@PAUs82@2@PAUs83@2@PAUs84@2@PAUs85@2@PAUs86@2@PAUs87@2@PAUs88@2@PAUs89@2@PAUs90@2@PAUs91@2@PAUs92@2@PAUs93@2@PAUs94@2@PAUs95@2@PAUs96@2@PAUs97@2@PAUs98@2@PAUs99@2@@Z";

const char *many_type_subs_win64 = "\001?test_function@@YAHPEAUs0@test_namespace@@PEAUs1@2@PEAUs2@2@PEAUs3@2@PEAUs4@2@PEAUs5@2@PEAUs6@2@PEAUs7@2@PEAUs8@2@PEAUs9@2@PEAUs10@2@PEAUs11@2@PEAUs12@2@PEAUs13@2@PEAUs14@2@PEAUs15@2@PEAUs16@2@PEAUs17@2@PEAUs18@2@PEAUs19@2@PEAUs20@2@PEAUs21@2@PEAUs22@2@PEAUs23@2@PEAUs24@2@PEAUs25@2@PEAUs26@2@PEAUs27@2@PEAUs28@2@PEAUs29@2@PEAUs30@2@PEAUs31@2@PEAUs32@2@PEAUs33@2@PEAUs34@2@PEAUs35@2@PEAUs36@2@PEAUs37@2@PEAUs38@2@PEAUs39@2@PEAUs40@2@PEAUs41@2@PEAUs42@2@PEAUs43@2@PEAUs44@2@PEAUs45@2@PEAUs46@2@PEAUs47@2@PEAUs48@2@PEAUs49@2@PEAUs50@2@PEAUs51@2@PEAUs52@2@PEAUs53@2@PEAUs54@2@PEAUs55@2@PEAUs56@2@PEAUs57@2@PEAUs58@2@PEAUs59@2@PEAUs60@2@PEAUs61@2@PEAUs62@2@PEAUs63@2@PEAUs64@2@PEAUs65@2@PEAUs66@2@PEAUs67@2@PEAUs68@2@PEAUs69@2@PEAUs70@2@PEAUs71@2@PEAUs72@2@PEAUs73@2@PEAUs74@2@PEAUs75@2@PEAUs76@2@PEAUs77@2@PEAUs78@2@PEAUs79@2@PEAUs80@2@PEAUs81@2@PEAUs82@2@PEAUs83@2@PEAUs84@2@PEAUs85@2@PEAUs86@2@PEAUs87@2@PEAUs88@2@PEAUs89@2@PEAUs90@2@PEAUs91@2@PEAUs92@2@PEAUs93@2@PEAUs94@2@PEAUs95@2@PEAUs96@2@PEAUs97@2@PEAUs98@2@PEAUs99@2@0123456789PEAUs10@2@PEAUs11@2@PEAUs12@2@PEAUs13@2@PEAUs14@2@PEAUs15@2@PEAUs16@2@PEAUs17@2@PEAUs18@2@PEAUs19@2@PEAUs20@2@PEAUs21@2@PEAUs22@2@PEAUs23@2@PEAUs24@2@PEAUs25@2@PEAUs26@2@PEAUs27@2@PEAUs28@2@PEAUs29@2@PEAUs30@2@PEAUs31@2@PEAUs32@2@PEAUs33@2@PEAUs34@2@PEAUs35@2@PEAUs36@2@PEAUs37@2@PEAUs38@2@PEAUs39@2@PEAUs40@2@PEAUs41@2@PEAUs42@2@PEAUs43@2@PEAUs44@2@PEAUs45@2@PEAUs46@2@PEAUs47@2@PEAUs48@2@PEAUs49@2@PEAUs50@2@PEAUs51@2@PEAUs52@2@PEAUs53@2@PEAUs54@2@PEAUs55@2@PEAUs56@2@PEAUs57@2@PEAUs58@2@PEAUs59@2@PEAUs60@2@PEAUs61@2@PEAUs62@2@PEAUs63@2@PEAUs64@2@PEAUs65@2@PEAUs66@2@PEAUs67@2@PEAUs68@2@PEAUs69@2@PEAUs70@2@PEAUs71@2@PEAUs72@2@PEAUs73@2@PEAUs74@2@PEAUs75@2@PEAUs76@2@PEAUs77@2@PEAUs78@2@PEAUs79@2@PEAUs80@2@PEAUs81@2@PEAUs82@2@PEAUs83@2@PEAUs84@2@PEAUs85@2@PEAUs86@2@PEAUs87@2@PEAUs88@2@PEAUs89@2@PEAUs90@2@PEAUs91@2@PEAUs92@2@PEAUs93@2@PEAUs94@2@PEAUs95@2@PEAUs96@2@PEAUs97@2@PEAUs98@2@PEAUs99@2@@Z";

MangleResult many_type_subs[] = {
  { many_type_subs_itanium, "The expanded prototype is very long." },
  { many_type_subs_itanium, "No really, too large to put here." },
  { many_type_subs_itanium, "wc -l says 4394 characters." },
  { many_type_subs_itanium, "Feel free to run c++filt if you want to..." },
  { many_type_subs_win32, "Not gonna do it." },
  { many_type_subs_win64, "Wouldn't be prudent." } };

const char *many_name_subs_itanium = "_Z13test_functionPN15test_namespace01sEPN15test_namespace11sEPN15test_namespace21sEPN15test_namespace31sEPN15test_namespace41sEPN15test_namespace51sEPN15test_namespace61sEPN15test_namespace71sEPN15test_namespace81sEPN15test_namespace91sEPN16test_namespace101sEPN16test_namespace111sEPN16test_namespace121sEPN16test_namespace131sEPN16test_namespace141sEPN16test_namespace151sEPN16test_namespace161sEPN16test_namespace171sEPN16test_namespace181sEPN16test_namespace191sEPN16test_namespace201sEPN16test_namespace211sEPN16test_namespace221sEPN16test_namespace231sEPN16test_namespace241sES1_S4_S7_SA_SD_SG_SJ_SM_SP_SS_SV_SY_S11_S14_S17_S1A_S1D_S1G_S1J_S1M_S1P_S1S_S1V_S1Y_S21_";

const char *many_name_subs_win32 = "\001?test_function@@YAHPAUs@test_namespace0@@PAU1test_namespace1@@PAU1test_namespace2@@PAU1test_namespace3@@PAU1test_namespace4@@PAU1test_namespace5@@PAU1test_namespace6@@PAU1test_namespace7@@PAU1test_namespace8@@PAU1test_namespace9@@PAU1test_namespace10@@PAU1test_namespace11@@PAU1test_namespace12@@PAU1test_namespace13@@PAU1test_namespace14@@PAU1test_namespace15@@PAU1test_namespace16@@PAU1test_namespace17@@PAU1test_namespace18@@PAU1test_namespace19@@PAU1test_namespace20@@PAU1test_namespace21@@PAU1test_namespace22@@PAU1test_namespace23@@PAU1test_namespace24@@0123456789PAU1test_namespace10@@PAU1test_namespace11@@PAU1test_namespace12@@PAU1test_namespace13@@PAU1test_namespace14@@PAU1test_namespace15@@PAU1test_namespace16@@PAU1test_namespace17@@PAU1test_namespace18@@PAU1test_namespace19@@PAU1test_namespace20@@PAU1test_namespace21@@PAU1test_namespace22@@PAU1test_namespace23@@PAU1test_namespace24@@@Z";

const char *many_name_subs_win64 = "\001?test_function@@YAHPEAUs@test_namespace0@@PEAU1test_namespace1@@PEAU1test_namespace2@@PEAU1test_namespace3@@PEAU1test_namespace4@@PEAU1test_namespace5@@PEAU1test_namespace6@@PEAU1test_namespace7@@PEAU1test_namespace8@@PEAU1test_namespace9@@PEAU1test_namespace10@@PEAU1test_namespace11@@PEAU1test_namespace12@@PEAU1test_namespace13@@PEAU1test_namespace14@@PEAU1test_namespace15@@PEAU1test_namespace16@@PEAU1test_namespace17@@PEAU1test_namespace18@@PEAU1test_namespace19@@PEAU1test_namespace20@@PEAU1test_namespace21@@PEAU1test_namespace22@@PEAU1test_namespace23@@PEAU1test_namespace24@@0123456789PEAU1test_namespace10@@PEAU1test_namespace11@@PEAU1test_namespace12@@PEAU1test_namespace13@@PEAU1test_namespace14@@PEAU1test_namespace15@@PEAU1test_namespace16@@PEAU1test_namespace17@@PEAU1test_namespace18@@PEAU1test_namespace19@@PEAU1test_namespace20@@PEAU1test_namespace21@@PEAU1test_namespace22@@PEAU1test_namespace23@@PEAU1test_namespace24@@@Z";

const char *many_name_subs_proto = "test_function(test_namespace0::s*, test_namespace1::s*, test_namespace2::s*, test_namespace3::s*, test_namespace4::s*, test_namespace5::s*, test_namespace6::s*, test_namespace7::s*, test_namespace8::s*, test_namespace9::s*, test_namespace10::s*, test_namespace11::s*, test_namespace12::s*, test_namespace13::s*, test_namespace14::s*, test_namespace15::s*, test_namespace16::s*, test_namespace17::s*, test_namespace18::s*, test_namespace19::s*, test_namespace20::s*, test_namespace21::s*, test_namespace22::s*, test_namespace23::s*, test_namespace24::s*, test_namespace0::s*, test_namespace1::s*, test_namespace2::s*, test_namespace3::s*, test_namespace4::s*, test_namespace5::s*, test_namespace6::s*, test_namespace7::s*, test_namespace8::s*, test_namespace9::s*, test_namespace10::s*, test_namespace11::s*, test_namespace12::s*, test_namespace13::s*, test_namespace14::s*, test_namespace15::s*, test_namespace16::s*, test_namespace17::s*, test_namespace18::s*, test_namespace19::s*, test_namespace20::s*, test_namespace21::s*, test_namespace22::s*, test_namespace23::s*, test_namespace24::s*)";

MangleResult many_name_subs[] = {
  { many_name_subs_itanium, many_name_subs_proto },
  { many_name_subs_itanium, many_name_subs_proto },
  { many_name_subs_itanium, many_name_subs_proto },
  { many_name_subs_itanium, many_name_subs_proto },
  { many_name_subs_win32, many_name_subs_proto },
  { many_name_subs_win64, many_name_subs_proto } };

MangleResult stacked_indirections[] = {
  { "_Z13test_functionPKiPKS0_PKS2_PKS4_PKS6_PKS8_PKSA_PKSC_", "" },
  { "_Z13test_functionPKiPKS0_PKS2_PKS4_PKS6_PKS8_PKSA_PKSC_", "" },
  { "_Z13test_functionPKiPKS0_PKS2_PKS4_PKS6_PKS8_PKSA_PKSC_", "" },
  { "_Z13test_functionPKiPKS0_PKS2_PKS4_PKS6_PKS8_PKSA_PKSC_", "" },
  { "\001?test_function@@YAHPBHPBQBHPBQBQBHPBQBQBQBHPBQBQBQBQBHPBQBQBQBQBQBHPBQBQBQBQBQBQBHPBQBQBQBQBQBQBQBH@Z", "" },
  { "\001?test_function@@YAHPEBHPEBQEBHPEBQEBQEBHPEBQEBQEBQEBHPEBQEBQEBQEBQEBHPEBQEBQEBQEBQEBQEBHPEBQEBQEBQEBQEBQEBQEBHPEBQEBQEBQEBQEBQEBQEBQEBH@Z", "" } };

MangleResult all_mods_itanium[] = {
  { "_Z13test_function1sRS_OS_", "test_function(s, s&, s&&)" },
  { "_Z13test_function1sRKS_OS0_", "test_function(s, s const&, s const&&)" },
  { "_Z13test_function1sRVS_OS0_", "test_function(s, s volatile&, s volatile&&)" },
  { "_Z13test_function1sRVKS_OS0_", "test_function(s, s const volatile&, s const volatile&&)" },
  { "_Z13test_function1sRrS_OS0_", "test_function(s, s restrict&, s restrict&&)" },
  { "_Z13test_function1sRrKS_OS0_", "test_function(s, s const restrict&, s const restrict&&)" },
  { "_Z13test_function1sRrVS_OS0_", "test_function(s, s volatile restrict&, s volatile restrict&&)" },
  { "_Z13test_function1sRrVKS_OS0_", "test_function(s, s const volatile restrict&, s const volatile restrict&&)" },
  { "_Z13test_functionP1sRS0_OS0_", "test_function(s*, s*&, s*&&)" },
  { "_Z13test_functionPK1sRS1_OS1_", "test_function(s const*, s const*&, s const*&&)" },
  { "_Z13test_functionPV1sRS1_OS1_", "test_function(s volatile*, s volatile*&, s volatile*&&)" },
  { "_Z13test_functionPVK1sRS1_OS1_", "test_function(s const volatile*, s const volatile*&, s const volatile*&&)" },
  { "_Z13test_functionPr1sRS1_OS1_", "test_function(s restrict*, s restrict*&, s restrict*&&)" },
  { "_Z13test_functionPrK1sRS1_OS1_", "test_function(s const restrict*, s const restrict*&, s const restrict*&&)" },
  { "_Z13test_functionPrV1sRS1_OS1_", "test_function(s volatile restrict*, s volatile restrict*&, s volatile restrict*&&)" },
  { "_Z13test_functionPrVK1sRS1_OS1_", "test_function(s const volatile restrict*, s const volatile restrict*&, s const volatile restrict*&&)" } };

MangleResult all_mods_win32[] = {
  { "\001?test_function@@YAHUs@@AAU1@$$QAU1@@Z", "test_function(s, s&, s&&)" },
  { "\001?test_function@@YAHUs@@ABU1@$$QBU1@@Z", "test_function(s, s const&, s const&&)" },
  { "\001?test_function@@YAHUs@@ACU1@$$QCU1@@Z", "test_function(s, s volatile&, s volatile&&)" },
  { "\001?test_function@@YAHUs@@ADU1@$$QDU1@@Z", "test_function(s, s const volatile&, s const volatile&&)" },
  { "\001?test_function@@YAHUs@@AAU1@$$QAU1@@Z", "test_function(s, s restrict&, s restrict&&)" },
  { "\001?test_function@@YAHUs@@ABU1@$$QBU1@@Z", "test_function(s, s const restrict&, s const restrict&&)" },
  { "\001?test_function@@YAHUs@@ACU1@$$QCU1@@Z", "test_function(s, s volatile restrict&, s volatile restrict&&)" },
  { "\001?test_function@@YAHUs@@ADU1@$$QDU1@@Z", "test_function(s, s const volatile restrict&, s const volatile restrict&&)" },
  { "\001?test_function@@YAHPAUs@@AAPAU1@$$QAPAU1@@Z", "test_function(s*, s*&, s*&&)" },
  { "\001?test_function@@YAHPBUs@@AAPBU1@$$QAPBU1@@Z", "test_function(s const*, s const*&, s const*&&)" },
  { "\001?test_function@@YAHPCUs@@AAPCU1@$$QAPCU1@@Z", "test_function(s volatile*, s volatile*&, s volatile*&&)" },
  { "\001?test_function@@YAHPDUs@@AAPDU1@$$QAPDU1@@Z", "test_function(s const volatile*, s const volatile*&, s const volatile*&&)" },
  { "\001?test_function@@YAHPAUs@@AAPAU1@$$QAPAU1@@Z", "test_function(s restrict*, s restrict*&, s restrict*&&)" },
  { "\001?test_function@@YAHPBUs@@AAPBU1@$$QAPBU1@@Z", "test_function(s const restrict*, s const restrict*&, s const restrict*&&)" },
  { "\001?test_function@@YAHPCUs@@AAPCU1@$$QAPCU1@@Z", "test_function(s volatile restrict*, s volatile restrict*&, s volatile restrict*&&)" },
  { "\001?test_function@@YAHPDUs@@AAPDU1@$$QAPDU1@@Z", "test_function(s const volatile restrict*, s const volatile restrict*&, s const volatile restrict*&&)" } };

MangleResult all_mods_win64[] = {
  { "\001?test_function@@YAHUs@@AEAU1@$$QEAU1@@Z", "test_function(s, s&, s&&)" },
  { "\001?test_function@@YAHUs@@AEBU1@$$QEBU1@@Z", "test_function(s, s const&, s const&&)" },
  { "\001?test_function@@YAHUs@@AECU1@$$QECU1@@Z", "test_function(s, s volatile&, s volatile&&)" },
  { "\001?test_function@@YAHUs@@AEDU1@$$QEDU1@@Z", "test_function(s, s const volatile&, s const volatile&&)" },
  { "\001?test_function@@YAHUs@@AEAU1@$$QEAU1@@Z", "test_function(s, s restrict&, s restrict&&)" },
  { "\001?test_function@@YAHUs@@AEBU1@$$QEBU1@@Z", "test_function(s, s const restrict&, s const restrict&&)" },
  { "\001?test_function@@YAHUs@@AECU1@$$QECU1@@Z", "test_function(s, s volatile restrict&, s volatile restrict&&)" },
  { "\001?test_function@@YAHUs@@AEDU1@$$QEDU1@@Z", "test_function(s, s const volatile restrict&, s const volatile restrict&&)" },
  { "\001?test_function@@YAHPEAUs@@AEAPEAU1@$$QEAPEAU1@@Z", "test_function(s*, s*&, s*&&)" },
  { "\001?test_function@@YAHPEBUs@@AEAPEBU1@$$QEAPEBU1@@Z", "test_function(s const*, s const*&, s const*&&)" },
  { "\001?test_function@@YAHPECUs@@AEAPECU1@$$QEAPECU1@@Z", "test_function(s volatile*, s volatile*&, s volatile*&&)" },
  { "\001?test_function@@YAHPEDUs@@AEAPEDU1@$$QEAPEDU1@@Z", "test_function(s const volatile*, s const volatile*&, s const volatile*&&)" },
  { "\001?test_function@@YAHPEAUs@@AEAPEAU1@$$QEAPEAU1@@Z", "test_function(s restrict*, s restrict*&, s restrict*&&)" },
  { "\001?test_function@@YAHPEBUs@@AEAPEBU1@$$QEAPEBU1@@Z", "test_function(s const restrict*, s const restrict*&, s const restrict*&&)" },
  { "\001?test_function@@YAHPECUs@@AEAPECU1@$$QEAPECU1@@Z", "test_function(s volatile restrict*, s volatile restrict*&, s volatile restrict*&&)" },
  { "\001?test_function@@YAHPEDUs@@AEAPEDU1@$$QEAPEDU1@@Z", "test_function(s const volatile restrict*, s const volatile restrict*&, s const volatile restrict*&&)" },
};

MangleResult two_void_stars_itanium[] = {
  { "_Z13test_functionPvS_", "test_function(void *, void *)" },
};

MangleResult two_void_stars_win64[] = {
  { "\001?test_function@@YAHPEAX0@Z", "test_function(void *, void *)" },
};

MangleResult two_void_stars_win32[] = {
  { "\001?test_function@@YAHPAX0@Z", "test_function(void *, void *)" },
};

void check_result(const MangleResult *expecteds, size_t &expected_index,
                  const Target &target, const std::string &mangled_name) {
    internal_assert(mangled_name == expecteds[expected_index].expected) << "Mangling for " <<
            expecteds[expected_index].label << " expected\n    " << expecteds[expected_index].expected <<
            " got\n    " << mangled_name << "\nfor target " << target.to_string();
        expected_index++;
}

void main_tests(const MangleResult *expecteds, const Target &target) {
    size_t expecteds_index = 0;
    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { }, Int(32), { }, target));

    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "foo" }, Int(32), { }, target));

    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "foo", "bar" }, Int(32), { }, target));

    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "foo", "bar" }, Int(32),
                                                 { ExternFuncArgument(42) }, target));

    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "foo", "bar" }, Int(32),
                                                 { ExternFuncArgument(42), ExternFuncArgument(Buffer<>()) }, target));

    halide_handle_cplusplus_type enclosed_type_info(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "test_struct"),
        { "test_namespace", "test_namespace" },
        { halide_cplusplus_type_name(halide_cplusplus_type_name::Class,
                                     "enclosing_class") }));
    Type test_type(Handle(1, &enclosed_type_info));
    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "test_namespace", "test_namespace" }, Int(32),
                                                 { ExternFuncArgument(make_zero(test_type)) }, target));

    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "foo", "bar" }, Int(32),
                                                 { ExternFuncArgument(42), ExternFuncArgument(Buffer<>()),
                                                   ExternFuncArgument(Buffer<>()) }, target));

    halide_handle_cplusplus_type qual1(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "test_struct"),
        { "test_namespace", }, { }, { halide_handle_cplusplus_type::Pointer }));
    Type qual1_type(Handle(1, &qual1));
    halide_handle_cplusplus_type qual2(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "test_struct"),
        { "test_namespace", }, { }, { halide_handle_cplusplus_type::Pointer |
                                      halide_handle_cplusplus_type::Const}));
    Type qual2_type(Handle(1, &qual2));
    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "test_namespace", "test_namespace" }, Int(32),
                                                 { ExternFuncArgument(make_zero(qual1_type)),
                                                   ExternFuncArgument(make_zero(qual2_type)) }, target));

    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "test_namespace", "test_namespace" }, Int(32),
                                                 { ExternFuncArgument(make_zero(test_type)),
                                                   ExternFuncArgument(make_zero(test_type)) }, target));

    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "std" }, Int(32), { }, target));

    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "std", "foo" }, Int(32), { }, target));

    halide_handle_cplusplus_type std_enclosed_type_info(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "test_struct"), { "std" },
                                   { halide_cplusplus_type_name(halide_cplusplus_type_name::Class, "enclosing_class") }));
     Type std_test_type(Handle(1, &std_enclosed_type_info));
     check_result(expecteds, expecteds_index, target,
                  cplusplus_function_mangled_name("test_function", { "std" }, Int(32),
                                                  { ExternFuncArgument(make_zero(std_test_type)) }, target));

    halide_handle_cplusplus_type class_type_info(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Class, "test_class"),
        { "test_namespace", }, { }, { halide_handle_cplusplus_type::Pointer }));
    Type class_type(Handle(1, &class_type_info));
    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "test_namespace", "test_namespace" }, Int(32),
                                                 { ExternFuncArgument(make_zero(class_type)), }, target));

    halide_handle_cplusplus_type union_type_info(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Union, "test_union"),
        { "test_namespace", }, { }, { halide_handle_cplusplus_type::Pointer }));
    Type union_type(Handle(1, &union_type_info));
    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "test_namespace", "test_namespace" }, Int(32),
                                                 { ExternFuncArgument(make_zero(union_type)), }, target));

    halide_handle_cplusplus_type enum_type_info(halide_handle_cplusplus_type(
        halide_cplusplus_type_name(halide_cplusplus_type_name::Class, "test_enum"),
        { "test_namespace", }, { }, { halide_handle_cplusplus_type::Pointer }));
    Type enum_type(Handle(1, &enum_type_info));
    check_result(expecteds, expecteds_index, target,
                 cplusplus_function_mangled_name("test_function", { "test_namespace", "test_namespace" }, Int(32),
                                                 { ExternFuncArgument(make_zero(enum_type)), }, target));
}

}

void cplusplus_mangle_test() {
    Target targets[]{ Target(Target::Linux, Target::X86, 32),
                                 Target(Target::Linux, Target::X86, 64),
                                 Target(Target::OSX, Target::X86, 32),
                                 Target(Target::OSX, Target::X86, 64),
                                 Target(Target::Windows, Target::X86, 32),
                                 Target(Target::Windows, Target::X86, 64) };
    MangleResult *expecteds[]{ ItaniumABIMangling_main, ItaniumABIMangling_main, ItaniumABIMangling_main,
                               ItaniumABIMangling_main, win32_expecteds, win64_expecteds };
    size_t i = 0;
    for (const auto &target : targets) {
        main_tests(expecteds[i++], target);
    }

    {
        // Test all primitive types.
        std::vector<ExternFuncArgument> args;
        args.push_back(ExternFuncArgument(make_zero(Bool())));
        args.push_back(ExternFuncArgument(make_zero(Int(8))));
        args.push_back(ExternFuncArgument(make_zero(UInt(8))));
        args.push_back(ExternFuncArgument(make_zero(Int(16))));
        args.push_back(ExternFuncArgument(make_zero(UInt(16))));
        args.push_back(ExternFuncArgument(make_zero(Int(32))));
        args.push_back(ExternFuncArgument(make_zero(UInt(32))));
        args.push_back(ExternFuncArgument(make_zero(Int(64))));
        args.push_back(ExternFuncArgument(make_zero(UInt(64))));
        args.push_back(ExternFuncArgument(make_zero(Float(32))));
        args.push_back(ExternFuncArgument(make_zero(Float(64))));

        size_t expecteds_index = 0;
        for (const auto &target : targets) {
            check_result(all_types_by_target, expecteds_index, target,
                         cplusplus_function_mangled_name("test_function", { }, Int(32), args, target));
        }
    }

    {
        // Test a whole ton of substitutions on type.
        std::vector<halide_handle_cplusplus_type> type_info;
        std::vector<ExternFuncArgument> args;
        for (int i = 0; i < 100; i++) {
          std::stringstream oss;
          oss << i;
          halide_handle_cplusplus_type t(halide_handle_cplusplus_type(
               halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "s" + oss.str()),
               { "test_namespace", }, { }, { halide_handle_cplusplus_type::Pointer }));
          type_info.push_back(t);
        }
        for (int i = 0; i < 200; i++) {
            args.push_back(make_zero(Handle(1, &type_info[i % 100])));
        }

        size_t expecteds_index = 0;
        for (const auto &target : targets) {
            check_result(many_type_subs, expecteds_index, target,
                         cplusplus_function_mangled_name("test_function", { }, Int(32), args, target));
        }
    }

    {
        // Test a whole ton of substitutions on names.
        std::vector<halide_handle_cplusplus_type> type_info;
        std::vector<ExternFuncArgument> args;
        for (int i = 0; i < 25; i++) {
          std::stringstream oss;
          oss << i;
          halide_handle_cplusplus_type t(halide_handle_cplusplus_type(
               halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "s"),
               { "test_namespace"  + oss.str(), }, { }, { halide_handle_cplusplus_type::Pointer }));
          type_info.push_back(t);
        }
        for (int i = 0; i < 50; i++) {
            args.push_back(make_zero(Handle(1, &type_info[i % 25])));
        }

        size_t expecteds_index = 0;
        for (const auto &target : targets) {
            check_result(many_name_subs, expecteds_index, target,
                         cplusplus_function_mangled_name("test_function", { }, Int(32), args, target));
        }
    }

    {
        // Stack up a bunch of pointers and qualifiers.
        // int test_function(int * const, int *const*const, int *const*const*const*, ...);
        std::vector<halide_handle_cplusplus_type> type_info;
        std::vector<ExternFuncArgument> args;
        for (size_t i = 1; i <= 8; i++) {
          std::vector<uint8_t> mods;
          for (size_t j = 0; j < i; j++) {
            mods.push_back(halide_handle_cplusplus_type::Pointer | halide_handle_cplusplus_type::Const);
          }
          halide_handle_cplusplus_type t(halide_handle_cplusplus_type(
               halide_cplusplus_type_name(halide_cplusplus_type_name::Simple, "int32_t"),
               { }, { }, mods));
          type_info.push_back(t);
        }
        for (const auto &ti : type_info) {
          args.push_back(ExternFuncArgument(make_zero(Handle(1, &ti))));
        }
        size_t expecteds_index = 0;
        for (const auto &target : targets) {
            check_result(stacked_indirections, expecteds_index, target,
                         cplusplus_function_mangled_name("test_function", { }, Int(32), args, target));
        }
    }

    {
        // Test all qualifiers and all ref arguments
        for (const auto &target : targets) {
            size_t expecteds_index = 0;
            for (uint8_t mods = 0; mods < 16; mods++) {
                halide_handle_cplusplus_type t1(halide_handle_cplusplus_type(
                    halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "s"), { }, { }, { mods }));
                halide_handle_cplusplus_type t2(halide_handle_cplusplus_type(
                    halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "s"), { }, { }, { mods }, halide_handle_cplusplus_type::LValueReference));
                halide_handle_cplusplus_type t3(halide_handle_cplusplus_type(
                    halide_cplusplus_type_name(halide_cplusplus_type_name::Struct, "s"), { }, { }, { mods }, halide_handle_cplusplus_type::RValueReference));
                std::vector<ExternFuncArgument> args;
                args.push_back(make_zero(Handle(1, &t1)));
                args.push_back(make_zero(Handle(1, &t2)));
                args.push_back(make_zero(Handle(1, &t3)));

                MangleResult *expecteds = (target.os == Target::Windows) ? (target.bits == 64 ? all_mods_win64 : all_mods_win32) : all_mods_itanium;
                check_result(expecteds, expecteds_index, target,
                         cplusplus_function_mangled_name("test_function", { }, Int(32), args, target));
            }
        }
    }

    {
        // Test two void * arguments to ensure substititon handles void * correctly.
        // (This is a special case as "void *" is represented using nullptr for the type info.)
        for (const auto &target : targets) {
            size_t expecteds_index = 0;
            std::vector<ExternFuncArgument> args;
            args.push_back(make_zero(Handle(1, nullptr)));
            args.push_back(make_zero(Handle(1, nullptr)));

            MangleResult *expecteds = (target.os == Target::Windows) ? (target.bits == 64 ? two_void_stars_win64 : two_void_stars_win32) : two_void_stars_itanium;
            check_result(expecteds, expecteds_index, target,
                         cplusplus_function_mangled_name("test_function", { }, Int(32), args, target));
        }
    }
}

} // namespace Internal

} // namespace Halide
