#ifndef HALIDE_UTIL_H
#define HALIDE_UTIL_H

#include <string>
#include "MLVal.h"

namespace Halide {
    // Generate a unique name
    std::string uniqueName(char prefix);
    
    // Make ML lists
    MLVal makeList();
    MLVal addToList(const MLVal &list, const MLVal &item);
}

#endif
