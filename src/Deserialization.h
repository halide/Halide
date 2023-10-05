#ifndef HALIDE_DESERIALIZATION_H
#define HALIDE_DESERIALIZATION_H

#include "Pipeline.h"
#include <istream>
#include <string>

namespace Halide {

/// @brief Deserialize a Halide pipeline from a file.
/// @param filename The location of the file to deserialize.  Must use .hlpipe extension.
/// @param external_params Map of named input/output parameters to bind with the resulting pipeline (used to avoid deserializing specific objects and enable the use of externally defined ones instead).
/// @return Returns a newly constructed deserialized Pipeline object/
Pipeline deserialize_pipeline(const std::string &filename, const std::map<std::string, Parameter> &external_params);

/// @brief Deserialize a Halide pipeline from an input stream.
/// @param in The input stream to read from containing a serialized Halide pipeline
/// @param external_params Map of named input/output parameters to bind with the resulting pipeline (used to avoid deserializing specific objects and enable the use of externally defined ones instead).
/// @return Returns a newly constructed deserialized Pipeline object/
Pipeline deserialize_pipeline(std::istream &in, const std::map<std::string, Parameter> &external_params);

/// @brief Deserialize a Halide pipeline from a byte buffer containing a serizalized pipeline in binary format
/// @param data The data buffer containing a serialized Halide pipeline
/// @param external_params Map of named input/output parameters to bind with the resulting pipeline (used to avoid deserializing specific objects and enable the use of externally defined ones instead).
/// @return Returns a newly constructed deserialized Pipeline object/
Pipeline deserialize_pipeline(const std::vector<uint8_t> &data, const std::map<std::string, Parameter> &external_params);

}  // namespace Halide

#endif
