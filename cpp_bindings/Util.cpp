#include "Util.h"
#include <sstream>
#include <stdio.h>

namespace Halide {
    ML_FUNC0(makeList); 
    ML_FUNC2(addToList); // cons

    std::string uniqueName(char prefix) {
        // arrays with static storage duration should be initialized to zero automatically
        static int instances[256]; 
        char prefix_cstr[2] = {prefix, '\0'};
        return std::string(prefix_cstr) + int_to_str(instances[(unsigned char)prefix]++);
    }

    std::string int_to_str(int x) {
        char buf[256];
        snprintf(buf, 256, "%d", x);
        return std::string(buf);
    }

    std::string sanitizeName(const std::string &name) {
        std::string result = name;
        for (size_t i = 1; i < name.size(); i++) {
            if (result[i] == '.') result[i] = '_';
        }
        return result;
    }
}
