#include "Util.h"
#include <sstream>

namespace FImage {
    ML_FUNC0(makeList); 
    ML_FUNC2(addToList); // cons

    std::string uniqueName(char prefix) {
        static int instances = 0;
        std::ostringstream ss;
        ss << prefix;
        ss << instances++;
        return ss.str();
    }
}
