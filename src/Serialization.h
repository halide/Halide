#ifndef HALIDE_SERIALIZATION_H
#define HALIDE_SERIALIZATION_H

#include "Pipeline.h"

namespace Halide {

/// @brief Serialize a Halide pipeline into the given data buffer.
/// @param pipeline The Halide pipeline to serialize.
/// @param data The data buffer to store the serialized Halide pipeline into. Any existing contents will be destroyed.
/// @param params Map of named parameters which will get populated during serialization (can be used to bind external parameters to objects in the pipeline by name).
void serialize_pipeline(const Pipeline &pipeline, std::vector<uint8_t> &data);

/// @brief Serialize a Halide pipeline into the given data buffer.
/// @param pipeline The Halide pipeline to serialize.
/// @param data The data buffer to store the serialized Halide pipeline into. Any existing contents will be destroyed.
/// @param params Map of named parameters which will get populated during serialization (can be used to bind external parameters to objects in the pipeline by name).
void serialize_pipeline(const Pipeline &pipeline, std::vector<uint8_t> &data, std::map<std::string, Parameter> &params);

/// @brief Serialize a Halide pipeline into the given filename.
/// @param pipeline The Halide pipeline to serialize.
/// @param filename The location of the file to write into to store the serialized pipeline.  Any existing contents will be destroyed.
void serialize_pipeline(const Pipeline &pipeline, const std::string &filename);

/// @brief Serialize a Halide pipeline into the given filename.
/// @param pipeline The Halide pipeline to serialize.
/// @param filename The location of the file to write into to store the serialized pipeline.  Any existing contents will be destroyed.
/// @param params Map of named parameters which will get populated during serialization (can be used to bind external parameters to objects in the pipeline by name).
void serialize_pipeline(const Pipeline &pipeline, const std::string &filename, std::map<std::string, Parameter> &params);

}  // namespace Halide

#endif
