#ifndef HALIDE_UTIL_H
#define HALIDE_UTIL_H

#include <string>
#include "MLVal.h"

namespace Halide {
    // Generate a unique name
    std::string uniqueName(char prefix);

    // Make sure a name has no invalid characters
    std::string sanitizeName(const std::string &name);
    
    // Make ML lists
    MLVal makeList();
    MLVal addToList(const MLVal &list, const MLVal &item);

    std::string int_to_str(int);          // Connelly: workaround function for ostringstream << int failing in Python binding
}

#endif
