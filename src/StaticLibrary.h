#ifndef HALIDE_STATIC_LIBRARY_H
#define HALIDE_STATIC_LIBRARY_H

/** \file
 * Methods combining object files into static libraries.
 * TODO(srj): add method for combing .COFF files (.obj -> .lib) for Windows.
 */

#include <string>
#include <vector>

#include "Util.h"

namespace Halide {
namespace Internal {

/**
 * Concatenate the list of src_files into dst_file, using Unix ar format.
 * If deterministic is true, emit 0 for all GID/UID/timestamps, and 0644 for
 * all modes (equivalent to the ar -D option).
 */
EXPORT void create_ar_file(const std::vector<std::string> &src_files, 
                           const std::string &dst_file, bool deterministic = true);

/**
 * Given a list of "files" (really, names and data), create an ar file.
 * This always emits 0 for all GID/UID/timestamps, and 0644 for
 * all modes (equivalent to the ar -D option). 
 */
struct ArInput {
    std::string name;
    std::vector<uint8_t> data;
};
EXPORT void create_ar_file(const std::vector<ArInput> &src_files, 
                           const std::string &dst_file);

EXPORT void static_library_test();

}  // namespace Halide
}  // namespace Internal

#endif  // HALIDE_STATIC_LIBRARY_H
