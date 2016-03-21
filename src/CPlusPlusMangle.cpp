#include "CPlusPlusMangle.h"

#include "LLVM_Headers.h"
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Mangle.h>
#include <clang/Basic/Builtins.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include "IR.h"
#include "Type.h"

namespace Halide {

namespace Internal {

namespace {

clang::DeclContext *namespaced_decl_scope(clang::ASTContext &context, const std::vector<std::string> &namespaces) {
    clang::DeclContext *decl_context = context.getTranslationUnitDecl();
    for (const auto &namespace_name : namespaces) {
        decl_context = clang::NamespaceDecl::Create(context, decl_context, false, clang::SourceLocation(), clang::SourceLocation(),
                                                    &context.Idents.get(namespace_name), NULL);
    }
    return decl_context;
}

clang::TagDecl::TagKind map_tag_decl_kind(halide_cplusplus_type_name::CPPTypeType halide_val) {
    switch (halide_val) {
    case halide_cplusplus_type_name::Simple:
        internal_error << "Simple types should have already been handled.\n";
    case halide_cplusplus_type_name::Struct:
        return clang::TTK_Struct;
    case halide_cplusplus_type_name::Class:
        return clang::TTK_Class;
    case halide_cplusplus_type_name::Union:
        return clang::TTK_Union;
    case halide_cplusplus_type_name::Enum:
        return clang::TTK_Enum;
    }
}

struct PreviousDeclarations {
    struct class_or_struct {
        halide_cplusplus_type_name::CPPTypeType cpp_type_type;
        clang::CXXRecordDecl *decl{nullptr};
    };
    std::map<std::string, class_or_struct> classes_and_structs;

    std::map<std::string, clang::EnumDecl *> enums;

    clang::CXXRecordDecl *DeclareRecord(clang::ASTContext &context, clang::DeclContext *decl_context,
                                        const halide_cplusplus_type_name &inner_name) {
        class_or_struct &entry(classes_and_structs[inner_name.name]);
        if (entry.decl != nullptr) {
            const char *map_to_name[] { "simple (unexpected", "struct", "class", "union", "enum (unexpected)" };
            user_assert(entry.cpp_type_type == inner_name.cpp_type_type) << "C++ type info for " << inner_name.name << " originally declared as " <<
                map_to_name[entry.cpp_type_type] << " and redeclared as " << map_to_name[inner_name.cpp_type_type] << ".\n";
        } else {
            entry. decl = clang::CXXRecordDecl::Create(context, map_tag_decl_kind(inner_name.cpp_type_type),
                                                       decl_context, clang::SourceLocation(), clang::SourceLocation(),
                                                       &context.Idents.get(inner_name.name))->getCanonicalDecl();
            entry.cpp_type_type = inner_name.cpp_type_type;
      }
      return entry.decl;
    }

    clang::EnumDecl *DeclareEnum(clang::ASTContext &context, clang::DeclContext *decl_context,
                                 const halide_cplusplus_type_name &inner_name) {
        clang::EnumDecl *&entry(enums[inner_name.name]);
        if (entry == nullptr) {
            entry = clang::EnumDecl::Create(context, decl_context,
                                            clang::SourceLocation(), clang::SourceLocation(),
                                            &context.Idents.get(inner_name.name),
                                            NULL, false, false, false)->getCanonicalDecl();
        }
        return entry;
    }
};

clang::QualType halide_type_to_clang_type(clang::ASTContext &context, PreviousDeclarations &prev_decls, Type type) {
    if (type.is_int()) {
        // TODO: Figure out how to resolve whether platform dependent 64-bit type is long or long long.
        if (type.bits() == 64) {
            return context.LongLongTy;
        } else {
            return context.getIntTypeForBitwidth(type.bits(), true);
        }
    } else if (type.is_uint()) {
        if (type.bits() == 1) {
            return context.BoolTy;
        } else if (type.bits() == 64) {
            return context.UnsignedLongLongTy;
        } else {
          return context.getIntTypeForBitwidth(type.bits(), false);
        }
    } else if (type.is_float()) {
        return context.getRealTypeForBitwidth(type.bits());
    } else {
        internal_assert(type.is_handle()) << "New type of Type that isn't handled.\n";

        clang::QualType base_type;
        if (type.handle_type != NULL) {
            if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Simple) {
                user_assert(type.handle_type->namespaces.empty() && type.handle_type->enclosing_types.empty()) <<
                    "Simple handle type cannot be inside any namespace or type scopes.\n";

                // Only handle explicitly sized types per Halide convention.
                if (type.handle_type->inner_name.name == "void") {
                    base_type = context.VoidTy;
                } else if (type.handle_type->inner_name.name == "bool") {
                    base_type = context.BoolTy;
                } else if (type.handle_type->inner_name.name == "int8_t") {
                    base_type = context.getIntTypeForBitwidth(8, true);
                } else if (type.handle_type->inner_name.name == "int16_t") {
                    base_type = context.getIntTypeForBitwidth(16, true);
                } else if (type.handle_type->inner_name.name == "int32_t") {
                    base_type = context.getIntTypeForBitwidth(32, true);
                } else if (type.handle_type->inner_name.name == "int64_t") {
                    base_type = context.LongLongTy;
                } else if (type.handle_type->inner_name.name == "uint8_t") {
                    base_type = context.getIntTypeForBitwidth(8, false);
                } else if (type.handle_type->inner_name.name == "uint16_t") {
                    base_type = context.getIntTypeForBitwidth(16, false);
                } else if (type.handle_type->inner_name.name == "uint32_t") {
                    base_type = context.getIntTypeForBitwidth(32, false);
                } else if (type.handle_type->inner_name.name == "uint64_t") {
                    base_type = context.UnsignedLongLongTy;
                } else if (type.handle_type->inner_name.name == "half") { // TODO: decide if this is a good idea
                    base_type = context.getRealTypeForBitwidth(16);
                } else if (type.handle_type->inner_name.name == "float") {
                    base_type = context.getRealTypeForBitwidth(32);
                } else if (type.handle_type->inner_name.name == "double") {
                    base_type = context.getRealTypeForBitwidth(64);
                } else {
                    user_error << "Unknown simple handle type " << type.handle_type->inner_name.name << "\n";
                }
            } else {
                clang::DeclContext *decl_context = namespaced_decl_scope(context, type.handle_type->namespaces);
                for (auto &scope_inner_name : type.handle_type->enclosing_types) {
                    user_assert(scope_inner_name.cpp_type_type != halide_cplusplus_type_name::Enum) <<
                        "Enums canot scope other types. (Enum name is " << scope_inner_name.name << ")\n";
                    decl_context = prev_decls.DeclareRecord(context, decl_context, scope_inner_name);
                }

                if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Enum) {
                    base_type = context.getEnumType(prev_decls.DeclareEnum(context, decl_context, type.handle_type->inner_name));
                } else {
                    base_type = context.getRecordType(prev_decls.DeclareRecord(context, decl_context, type.handle_type->inner_name));
                }
            }

            for (uint8_t modifier : type.handle_type->cpp_type_modifiers) {
                if (modifier & halide_handle_cplusplus_type::Const) {
                    base_type.addConst();
                }
                if (modifier & halide_handle_cplusplus_type::Volatile) {
                    base_type.addVolatile();
                }
                if (modifier & halide_handle_cplusplus_type::Restrict) {
                    base_type.addRestrict();
                }
                if (modifier & halide_handle_cplusplus_type::Pointer) {
                    base_type = context.getPointerType(base_type);
                } else {
                    break;
                }
            }
 
            if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
                    base_type = context.getLValueReferenceType(base_type);
            } else if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
                    base_type = context.getRValueReferenceType(base_type);
            }

            return base_type;
        }
        // Otherwise return void *
    }
    return context.VoidPtrTy;
}

}

std::string cplusplus_function_mangled_name(const std::string &name, const std::vector<std::string> &namespaces,
                                            Type return_type, const std::vector<ExternFuncArgument> &args,
                                            const Target &target) {
    clang::CompilerInstance compiler_instance;

    compiler_instance.createDiagnostics();
    std::shared_ptr<clang::TargetOptions> target_options(new clang::TargetOptions);

    // Not sure this is necessary given the MangleContext creation
    // below, but just in case. Note handling of mapping of integer
    // types is likely platform dependent. E.g. int64_t could map to
    // long or to long long depending on the platform and these mangle
    // differently.
    if (target.os == Target::Windows) {
        // Have to provide something so compilation is independent of host platform
        target_options->Triple = "x86_64-unknown-unknown-unknown";
    } else {
        target_options->Triple = "x86_64-unknown-win32-msvc";
    }
    compiler_instance.setTarget(clang::TargetInfo::CreateTargetInfo(compiler_instance.getDiagnostics(), target_options));
    compiler_instance.createFileManager();
    compiler_instance.createSourceManager(compiler_instance.getFileManager());
    compiler_instance.getLangOpts().CPlusPlus = true;
    compiler_instance.getLangOpts().CPlusPlus11 = true;
    compiler_instance.createPreprocessor(clang::TU_Complete);
    compiler_instance.createASTContext();

    clang::DiagnosticsEngine &diags(compiler_instance.getDiagnostics());
    clang::ASTContext &context(compiler_instance.getASTContext());

    std::unique_ptr<clang::MangleContext> mangle_context;
    if (target.os == Target::Windows) {
        mangle_context.reset(clang::MicrosoftMangleContext::create(context, diags));
    } else {
        mangle_context.reset(clang::ItaniumMangleContext::create(context, diags));
    }

    clang::DeclContext *decl_context = namespaced_decl_scope(context, namespaces);
    PreviousDeclarations prev_decls;

    std::vector<clang::QualType> clang_args;
    for (auto &arg : args) {
        clang_args.push_back(halide_type_to_clang_type(context, prev_decls,
                                                       arg.is_expr() ? arg.expr.type()
                                                                     : type_of<struct buffer_t *>()));
    }

    clang::QualType clang_return_type = halide_type_to_clang_type(context, prev_decls, return_type);
    clang::QualType function_type = context.getFunctionType(clang_return_type, clang_args,
                                                            clang::FunctionProtoType::ExtProtoInfo());
    clang::FunctionDecl *decl = clang::FunctionDecl::Create(context, decl_context,
                                                            clang::SourceLocation(), clang::SourceLocation(),
                                                            &context.Idents.get(name), function_type, NULL, clang::SC_None);
    std::vector<clang::ParmVarDecl *> param_var_decls;
    for (auto &qual_type : clang_args) {
        param_var_decls.push_back(clang::ParmVarDecl::Create(context, decl, 
                                                             clang::SourceLocation(), clang::SourceLocation(),
                                                             NULL, qual_type, NULL, clang::SC_None, NULL));
    }
    decl->setParams(param_var_decls);
 
    const clang::FunctionType *ft = decl->getType()->getAs<clang::FunctionType>();
    const clang::FunctionProtoType *proto = clang::cast<clang::FunctionProtoType>(ft);
    internal_assert(proto != NULL) << "proto is null\n";

    std::string result;
    llvm::raw_string_ostream out_str(result);
    mangle_context->mangleName(decl, out_str);

    return out_str.str();
}

void cplusplus_mangle_test() {
    std::string simple_name =
        cplusplus_function_mangled_name("test_function", { }, Int(32), { }, Target(Target::Linux, Target::X86, 64));
    internal_assert(simple_name == "_Z13test_functionv") << "Expected mangling  for simple canse to produce _Z13test_functionv but got " << simple_name << "\n";
    std::string with_namespaces =
        cplusplus_function_mangled_name("test_function", { "foo", "bar" }, Int(32), { }, Target(Target::Linux, Target::X86, 64));
    internal_assert(with_namespaces == "_ZN3foo3bar13test_functionEv") << "Expected mangling namespace case to produce _ZN3foo3bar13test_functionEv but got " << simple_name << "\n";
    std::string with_args =
      cplusplus_function_mangled_name("test_function", { "foo", "bar" }, Int(32), { ExternFuncArgument(42) }, Target(Target::Linux, Target::X86, 64));
    internal_assert(with_args == "_ZN3foo3bar13test_functionEi") << "Expected mangling args case to produce _ZN3foo3bar13test_functionEi but got " << simple_name << "\n";
    // TODO: test struct types, Microsoft mangling.
}

}

}
