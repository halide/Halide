#ifndef HALIDE_DESERIALIZATION_H
#define HALIDE_DESERIALIZATION_H

#include "Pipeline.h"
#include <istream>
#include <string>

namespace Halide {

/**
 * Deserialize a Halide pipeline from a file.
 * filename should always end in .hlpipe suffix.
 * external_params is an optional map, all parameters in the map
 * will be treated as external parameters so won't be deserialized.
 */
Pipeline deserialize_pipeline(const std::string &filename, const std::map<std::string, Parameter> &external_params);

Pipeline deserialize_pipeline(std::istream &in, const std::map<std::string, Parameter> &external_params);

}  // namespace Halide

#endif
