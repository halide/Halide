#include "Util.h"
#include <sstream>

namespace FImage {
    ML_FUNC0(makeList); 
    ML_FUNC2(addToList); // cons

    std::string uniqueName(char prefix) {
        // arrays with static storage duration should be initialized to zero automatically
        static int instances[256]; 
        std::ostringstream ss;
        ss << prefix;
        ss << instances[(unsigned char)prefix]++;
        return ss.str();
    }
}
