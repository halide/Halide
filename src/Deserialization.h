#ifndef HALIDE_DESERIALIZATION_H
#define HALIDE_DESERIALIZATION_H

#include "Pipeline.h"
#include <istream>
#include <string>

namespace Halide {

/// @brief Deserialize a Halide pipeline from a file.
/// @param filename The location of the file to deserialize.  Must use .hlpipe extension.
/// @param user_params Map of named input/output parameters to bind with the resulting pipeline (used to avoid deserializing specific objects and enable the use of externally defined ones instead).
/// @return Returns a newly constructed deserialized Pipeline object/
Pipeline deserialize_pipeline(const std::string &filename, const std::map<std::string, Parameter> &user_params);

/// @brief Deserialize a Halide pipeline from an input stream.
/// @param in The input stream to read from containing a serialized Halide pipeline
/// @param user_params Map of named input/output parameters to bind with the resulting pipeline (used to avoid deserializing specific objects and enable the use of externally defined ones instead).
/// @return Returns a newly constructed deserialized Pipeline object/
Pipeline deserialize_pipeline(std::istream &in, const std::map<std::string, Parameter> &user_params);

/// @brief Deserialize a Halide pipeline from a byte buffer containing a serizalized pipeline in binary format
/// @param data The data buffer containing a serialized Halide pipeline
/// @param user_params Map of named input/output parameters to bind with the resulting pipeline (used to avoid deserializing specific objects and enable the use of externally defined ones instead).
/// @return Returns a newly constructed deserialized Pipeline object/
Pipeline deserialize_pipeline(const std::vector<uint8_t> &data, const std::map<std::string, Parameter> &user_params);

/// @brief Deserialize the extenal parameters for the Halide pipeline from a file.
///        This method allows a minimal deserialization of just the external pipeline parameters, so they can be
///        remapped and overridden with user parameters prior to deserializing the pipeline definition.
/// @param filename The location of the file to deserialize.  Must use .hlpipe extension.
/// @return Returns a map containing the names and description of external parameters referenced in the pipeline
std::map<std::string, Parameter> deserialize_parameters(const std::string &filename);

/// @brief Deserialize the extenal parameters for the Halide pipeline from input stream.
///        This method allows a minimal deserialization of just the external pipeline parameters, so they can be
///        remapped and overridden with user parameters prior to deserializing the pipeline definition.
/// @param in The input stream to read from containing a serialized Halide pipeline
/// @return Returns a map containing the names and description of external parameters referenced in the pipeline
std::map<std::string, Parameter> deserialize_parameters(std::istream &in);

/// @brief Deserialize the extenal parameters for the Halide pipeline from a byte buffer containing a serialized
///        pipeline in binary format.  This method allows a minimal deserialization of just the external pipeline
///        parameters, so they can be remapped and overridden with user parameters prior to deserializing the
///        pipeline definition.
/// @param data The data buffer containing a serialized Halide pipeline
/// @return Returns a map containing the names and description of external parameters referenced in the pipeline
std::map<std::string, Parameter> deserialize_parameters(const std::vector<uint8_t> &data);

}  // namespace Halide

#endif
