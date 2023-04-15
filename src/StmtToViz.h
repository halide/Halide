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

/** Dump an HTML-formatted visualization of a Module to filename.
 * If assembly_input_filename is not empty, it is expected to be the path
 * to assembly output. If empty, the code will attempt to find such a
 * file based on output_filename (replacing ".stmt.html" with ".s"),
 * and will assert-fail if no such file is found. */
void print_to_viz(const std::string &html_output_filename,
                  const Module &m,
                  const std::string &assembly_input_filename = "");

}  // namespace Internal
}  // namespace Halide

#endif
