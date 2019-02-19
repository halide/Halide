#ifndef HALIDE_JAVASCRIPT_EXECUTOR_H
#define HALIDE_JAVASCRIPT_EXECUTOR_H

/** \file
 *
 * Support for running Halide compiled JavaScript code in-process.
 * Bindings for parameters, extern calls, etc. are established and the
 * JavaScript code is executed. Allows calls to relaize to work
 * exactly as if native code had been run, but via JavaScript. This is
 * largely used to run all JIT tests with the JavaScript backend, but
 * could have other uses in the future. Currently the SpiderMonkey and
 * V8 JavaScript engines are supported.
 */

#include "JITModule.h"
#include "Parameter.h"
#include "Target.h"
#include "Type.h"

namespace Halide { namespace Internal {

/** Remove any externs that will be handled by codegen and thus do not need to be
 * backed by a function pointer in the address space.
 */
std::map<std::string, JITExtern> filter_externs(const std::map<std::string, JITExtern> &externs);

struct JavaScriptModuleContents;

/** Handle to compiled JavaScript code which can be called later. */
struct JavaScriptModule {
    Internal::IntrusivePtr<JavaScriptModuleContents> contents;
};

/** Compile generated JavaScript code with a set of externs. Target is
 * used to choose the JavaScript engine. (Default is V8 if both V8 and
 * SpiderMonkey are enabled.) */
JavaScriptModule compile_javascript(const Target &target, const std::string &source, const std::string &fn_name,
                                           const std::map<std::string, JITExtern> &externs,
                                           const std::vector<JITModule> &extern_deps);

/** Run generated previously compiled JavaScript code with a set of arguments. */
int run_javascript(JavaScriptModule module, const std::vector<std::pair<Argument, const void *>> &args);

}}

#endif
