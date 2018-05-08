#ifndef HALIDE_STMT_TO_HTML
#define HALIDE_STMT_TO_HTML

/** \file
 * Defines a function to dump an HTML-formatted stmt to a file.
 */

#include "Module.h"

namespace Halide {
namespace Internal {

/**
 * Dump an HTML-formatted print of a Stmt to filename.
 */
void print_to_html(std::string filename, Stmt s);

/** Dump an HTML-formatted print of a Module to filename. */
void print_to_html(std::string filename, const Module &m);

}  // namespace Internal
}  // namespace Halide

#endif
