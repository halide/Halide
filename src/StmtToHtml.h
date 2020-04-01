#ifndef HALIDE_STMT_TO_HTML
#define HALIDE_STMT_TO_HTML

#include <string>

#include "Expr.h"
/** \file
 * Defines a function to dump an HTML-formatted stmt to a file.
 */

namespace Halide {
class Module;

namespace Internal {

/**
 * Dump an HTML-formatted print of a Stmt to filename.
 */
void print_to_html(const std::string &filename, const Stmt &s);

/** Dump an HTML-formatted print of a Module to filename. */
void print_to_html(const std::string &filename, const Module &m);

}  // namespace Internal
}  // namespace Halide

#endif
