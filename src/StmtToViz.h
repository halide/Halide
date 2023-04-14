#ifndef HALIDE_STMT_TO_VIZ
#define HALIDE_STMT_TO_VIZ

/** \file
 * Defines a function to dump an HTML-formatted visualization to a file.
 */

#include <string>

namespace Halide {

class Module;

namespace Internal {

struct Stmt;

/** Dump an HTML-formatted visualization of a Module to filename. */
void print_to_viz(const std::string &filename, const Module &m);

}  // namespace Internal
}  // namespace Halide

#endif
