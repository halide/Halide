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

clang::QualType halide_type_to_clang_type(clang::ASTContext &context, Type type) {
    if (type.is_int()) {
        return context.getIntTypeForBitwidth(type.bits(), true);
    } else if (type.is_uint()) {
        return context.getIntTypeForBitwidth(type.bits(), false);
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
                if (type.handle_type->inner_name.name == "bool") {
                    base_type = context.BoolTy;
                } else if (type.handle_type->inner_name.name == "int8_t") {
                    base_type = context.getIntTypeForBitwidth(8, true);
                } else if (type.handle_type->inner_name.name == "int16_t") {
                    base_type = context.getIntTypeForBitwidth(16, true);
                } else if (type.handle_type->inner_name.name == "int32_t") {
                    base_type = context.getIntTypeForBitwidth(32, true);
                } else if (type.handle_type->inner_name.name == "int64_t") {
                    base_type = context.getIntTypeForBitwidth(64, true);
                } else if (type.handle_type->inner_name.name == "uint8_t") {
                    base_type = context.getIntTypeForBitwidth(8, false);
                } else if (type.handle_type->inner_name.name == "uint16_t") {
                    base_type = context.getIntTypeForBitwidth(16, false);
                } else if (type.handle_type->inner_name.name == "uint32_t") {
                    base_type = context.getIntTypeForBitwidth(32, false);
                } else if (type.handle_type->inner_name.name == "uint64_t") {
                    base_type = context.getIntTypeForBitwidth(64, false);
                } else if (type.handle_type->inner_name.name == "half") { // TODO: decide if this is a good idea
                    base_type = context.getRealTypeForBitwidth(16);
                } else if (type.handle_type->inner_name.name == "float") {
                    base_type = context.getRealTypeForBitwidth(32);
                } else if (type.handle_type->inner_name.name == "double") {
                    base_type = context.getRealTypeForBitwidth(64);
                }
                user_error << "Unknown simple handle type " << type.handle_type->inner_name.name << "\n";
            } else {
                clang::DeclContext *decl_context = namespaced_decl_scope(context, type.handle_type->namespaces);
                for (auto &scope_inner_name : type.handle_type->enclosing_types) {
                    user_assert(scope_inner_name.cpp_type_type != halide_cplusplus_type_name::Enum) <<
                        "Enums canot scope other types. (Enum name is " << scope_inner_name.name << ")\n";
                    decl_context = clang::CXXRecordDecl::Create(context, map_tag_decl_kind(scope_inner_name.cpp_type_type),
                                                                decl_context, clang::SourceLocation(), clang::SourceLocation(),
                                                                &context.Idents.get(scope_inner_name.name));
                }

                if (type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Enum) {
                    clang::EnumDecl *enum_decl = clang::EnumDecl::Create(context, decl_context,
                                                                         clang::SourceLocation(), clang::SourceLocation(),
                                                                         &context.Idents.get(type.handle_type->inner_name.name),
                                                                         NULL, false, false, false);
                    base_type = context.getEnumType(enum_decl);
                } else {
                    clang::CXXRecordDecl *record_decl = clang::CXXRecordDecl::Create(context, map_tag_decl_kind(type.handle_type->inner_name.cpp_type_type),
                                                                                     decl_context, clang::SourceLocation(), clang::SourceLocation(),
                                                                                     &context.Idents.get(type.handle_type->inner_name.name));
                    base_type = context.getRecordType(record_decl);
                }
            }

            if (type.handle_type->inner_name.cpp_type_qualifiers && halide_cplusplus_type_name::Const) {
                base_type.addConst();
            }
            if (type.handle_type->inner_name.cpp_type_qualifiers && halide_cplusplus_type_name::Volatile) {
                base_type.addVolatile();
            }
            for (int32_t i = 0; i <= type.handle_type->extra_indirection_levels; i++)  {
                base_type = context.getPointerType(base_type);
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
#define COMPILER_INSTANCE 1
#if COMPILER_INSTANCE
    clang::CompilerInstance compiler_instance;

    compiler_instance.createDiagnostics();
    std::shared_ptr<clang::TargetOptions> target_options(new clang::TargetOptions);
    target_options->Triple = "x86_64-unknown-unknown-unknown"; // Have to provide something so compilation is independent of host platform
    compiler_instance.setTarget(clang::TargetInfo::CreateTargetInfo(compiler_instance.getDiagnostics(), target_options));
    compiler_instance.createFileManager();
    compiler_instance.createSourceManager(compiler_instance.getFileManager());
    compiler_instance.getLangOpts().CPlusPlus = true;
    compiler_instance.getLangOpts().CPlusPlus11 = true;
    compiler_instance.createPreprocessor(clang::TU_Complete);
    compiler_instance.createASTContext();

    clang::DiagnosticsEngine &diags(compiler_instance.getDiagnostics());
    clang::ASTContext &context(compiler_instance.getASTContext());
#else
    clang::DiagnosticOptions diagnostic_options;
    clang::IntrusiveRefCntPtr<clang::DiagnosticIDs> diagIDs(new clang::DiagnosticIDs);
    clang::DiagnosticsEngine diags(diagIDs, &diagnostic_options);
    clang::FileSystemOptions filesystem_options;
    clang::FileManager file_manager(filesystem_options);
    clang::SourceManager source_manager(diags, file_manager);
    clang::LangOptions lang_opts;
    clang::IdentifierTable id_table(lang_opts);
    clang::SelectorTable selector_table;
    clang::Builtin::Context builtins_context;

    clang::ASTContext context(lang_opts, source_manager, id_table, selector_table, builtins_context);
#endif
    std::unique_ptr<clang::MangleContext> mangle_context;
    if (target.os == Target::Windows) {
        mangle_context.reset(clang::MicrosoftMangleContext::create(context, diags));
    } else {
        mangle_context.reset(clang::ItaniumMangleContext::create(context, diags));
    }

    clang::DeclContext *decl_context = namespaced_decl_scope(context, namespaces);

    clang::CXXRecordDecl *buffer_t_decl = nullptr;
    std::vector<clang::QualType> clang_args;
    for (auto &arg : args) {
        if (arg.is_expr()) {
            clang_args.push_back(halide_type_to_clang_type(context, arg.expr.type()));
        } else { // Otherwise, struct buffer_t *
            if (buffer_t_decl == nullptr) {
                buffer_t_decl = clang::CXXRecordDecl::Create(context, clang::TTK_Struct, context.getTranslationUnitDecl(),
                                                             clang::SourceLocation(), clang::SourceLocation(),
                                                             &context.Idents.get("buffer_t"));
            }
            clang_args.push_back(context.getPointerType(context.getRecordType(buffer_t_decl)));
        }
    }

    clang::QualType function_type = context.getFunctionType(halide_type_to_clang_type(context, return_type), clang_args,
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
