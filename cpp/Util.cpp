#include "Util.h"
#include <sstream>

namespace Halide { namespace Internal {
    std::string unique_name(char prefix) {
        // arrays with static storage duration should be initialized to zero automatically
        static int instances[256]; 
        std::ostringstream str;
        str << prefix << instances[(unsigned char)prefix]++;
        return str.str();
    }
}}
