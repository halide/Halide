#include "Util.h"
#include <sstream>
#include <stdio.h>

namespace Halide {
    ML_FUNC0(makeList); 
    ML_FUNC2(addToList); // cons

    std::string uniqueName(char prefix) {
        // arrays with static storage duration should be initialized to zero automatically
        static int instances[256]; 
        std::ostringstream ss;
        ss << prefix;
        ss << int_to_str(instances[(unsigned char)prefix]++);
        return ss.str();
    }

    std::string int_to_str(int x) {
        char buf[256];
        snprintf(buf, 256, "%d", x);
        return std::string(buf);
    }
}
