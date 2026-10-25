#ifndef HALIDE_STMT_TO_HTML
#define HALIDE_STMT_TO_HTML

/** \file
 * Defines a function to dump an HTML-formatted visualization to a file.
 */

#include <string>

namespace Halide {

class Module;

namespace Internal {

struct StmtSourceMap;

/** Dump an HTML-formatted visualization of a Module to filename. The IR is
 * shown exactly as in stmt, a printing of the Module (see
 * print_with_source_map). If assembly_input_filename is not empty, it is
 * expected to be the path to assembly output, compiled with debug info that
 * points into stmt. If empty, the code will attempt to find such a file based
 * on output_filename (replacing ".stmt.html" with ".s"), and will assert-fail
 * if no such file is found. */
void print_to_stmt_html(const std::string &html_output_filename,
                        const Module &m,
                        const std::string &assembly_input_filename,
                        const StmtSourceMap &stmt);

/** Dump an HTML-formatted visualization of a Module's conceptual Stmt code to
 * filename. The IR is shown exactly as in conceptual_stmt, a printing of the
 * conceptual Stmt. The assembly is as for print_to_stmt_html. */
void print_to_conceptual_stmt_html(const std::string &html_output_filename,
                                   const Module &m,
                                   const std::string &assembly_input_filename,
                                   const StmtSourceMap &stmt,
                                   const StmtSourceMap &conceptual_stmt);

}  // namespace Internal
}  // namespace Halide

#endif
