#ifndef HALIDE_STMT_TO_JSON
#define HALIDE_STMT_TO_JSON

/** \file
 * Defines a function to dump an stmt in JSON format.
 */

#include "Module.h"

namespace Halide {
namespace Internal {

/**
  * Dump a JSON-formatted Stmt to filename.
  */
void print_to_json(const std::string &filename, const Stmt &s);

/** Dump an HTML-formatted print of a Module to filename. */
void print_to_json(const std::string &filename, const Module &m);

}  // namespace Internal
}  // namespace Halide

#endif
