#ifndef HALIDE_JSON_PARSER
#define HALIDE_JSON_PARSER

/** \file
 * Defines a function to read in a JSON-format defined pipeline.
 */

#include "Module.h"

namespace Halide {
  //namespace Internal {

/**
  * Construct a Module from a JSON description.
  */
Module parse_from_json_file(const std::string &filename);

//}  // namespace Internal
}  // namespace Halide

#endif
