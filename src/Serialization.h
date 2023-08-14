#ifndef HALIDE_SERIALIZATION_H
#define HALIDE_SERIALIZATION_H

#include "Pipeline.h"

namespace Halide {

/**
 * Serialize a Halide pipeline into the given data buffer.
 * params is an optional map which can be used to bind external parameters to objects in the pipeline by name
 */
void serialize_pipeline(const Pipeline &pipeline, std::vector<uint8_t> &data, std::map<std::string, Internal::Parameter> &params);

/**
 * Serialize a Halide pipeline into the given filename.
 * params is an optional map which can be used to bind external parameters to objects in the pipeline by name
 */
void serialize_pipeline(const Pipeline &pipeline, const std::string &filename, std::map<std::string, Internal::Parameter> &params);

}  // namespace Halide

#endif
