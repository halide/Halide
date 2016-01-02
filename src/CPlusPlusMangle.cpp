#include "CPlusPlusMangle.h"

#include "LLVM_Headers.h"
#include <clang/AST/ASTContext.h>
#include <clang/Basic/Builtins.h>
#include <clang/Frontend/CompilerInstance.h>

#include "IR.h"
#include "Type.h"

namespace Halide {

namespace Internal {

std::string cplusplus_mangled_name(const Call *op, const Target &target) {
    clang::CompilerInstance compiler_instance;

#if 0
    Diags = compiler_instance.createDiagnostics(new DiagnosticOptions(),
                                                Client,
                                                /*ShouldOwnClient=*/true);
#endif
    clang::LangOptions lang_opts;
    clang::IdentifierTable id_table(lang_opts);
    clang::SelectorTable selector_table;
    clang::Builtin::Context builtins_context;
    clang::ASTContext context(lang_opts, compiler_instance.getSourceManager(), id_table, selector_table, builtins_context);

    return "";  
}

}

}
