#ifndef HANNK_FILE_UTIL_H
#define HANNK_FILE_UTIL_H

#include <fstream>
#include <memory>
#include <vector>

#include "util/error_util.h"

namespace hannk {

inline std::vector<char> read_entire_file(const std::string &filename) {
    std::ifstream f(filename, std::ios::in | std::ios::binary);
    HCHECK(f.is_open()) << "Unable to open file: " << filename;

    std::vector<char> result;

    f.seekg(0, std::ifstream::end);
    size_t size = f.tellg();
    result.resize(size);
    f.seekg(0, std::ifstream::beg);
    f.read(result.data(), result.size());
    HCHECK(f.good()) << "Unable to read file: " << filename;
    f.close();
    return result;
}

}  // namespace hannk

#endif  // HANNK_FILE_UTIL_H
