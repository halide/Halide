#include "Util.h"
#include <sstream>

namespace Halide { 
namespace Internal {

std::string unique_name(char prefix) {
    // arrays with static storage duration should be initialized to zero automatically
    static int instances[256]; 
    std::ostringstream str;
    str << prefix << instances[(unsigned char)prefix]++;
    return str.str();
}

bool starts_with(const std::string &str, const std::string &prefix) {
    if (str.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (str[i] != prefix[i]) return false;
    }
    return true;
}

bool ends_with(const std::string &str, const std::string &suffix) {
    if (str.size() < suffix.size()) return false;
    size_t off = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); i++) {
        if (str[off+i] != suffix[i]) return false;
    }
    return true;
}

}
}
