#ifndef FILE_UTIL_H_
#define FILE_UTIL_H_

#include <fstream>
#include <memory>
#include <vector>

#include "app_util.h"

namespace interpret_nn {

inline std::vector<char> read_entire_file(const std::string &filename) {
    std::ifstream f(filename, std::ios::in | std::ios::binary);
    APP_CHECK(f.is_open()) << "Unable to open file: " << filename;

    std::vector<char> result;

    f.seekg(0, std::ifstream::end);
    size_t size = f.tellg();
    result.resize(size);
    f.seekg(0, std::ifstream::beg);
    f.read(result.data(), result.size());
    APP_CHECK(f.good()) << "Unable to read file: " << filename;
    f.close();
    return result;
}

inline void write_entire_file(const std::string &filename, const void *source, size_t source_len) {
    std::ofstream f(filename, std::ios::out | std::ios::binary);
    APP_CHECK(f.is_open()) << "Unable to open file: " << filename;

    f.write(reinterpret_cast<const char *>(source), source_len);
    f.flush();
    APP_CHECK(f.good()) << "Unable to write file: " << filename;
    f.close();
}

inline void write_entire_file(const std::string &filename, const std::vector<char> &source) {
    write_entire_file(filename, source.data(), source.size());
}

}  // namespace interpret_nn

#endif  // FILE_UTIL_H_
