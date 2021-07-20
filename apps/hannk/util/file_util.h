#ifndef HANNK_FILE_UTIL_H
#define HANNK_FILE_UTIL_H

#include <fstream>
#include <memory>
#include <vector>

#include "util/status.h"

namespace hannk {

inline Status read_entire_file(const std::string &filename, std::vector<char> *result) {
    std::ifstream f(filename, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return Status::Error;
    }

    f.seekg(0, std::ifstream::end);
    size_t size = f.tellg();
    result->resize(size);
    f.seekg(0, std::ifstream::beg);
    f.read(result->data(), result->size());
    if (!f.good()) {
        return Status::Error;
    }
    f.close();
    return Status::OK;
}

}  // namespace hannk

#endif  // HANNK_FILE_UTIL_H
