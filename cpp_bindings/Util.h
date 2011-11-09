#ifndef FIMAGE_UTIL_H
#define FIMAGE_UTIL_H

#include <string>
#include "MLVal.h"

namespace FImage {
    // Generate a unique name
    std::string uniqueName(char prefix);
    
    // Make ML lists
    MLVal makeList();
    MLVal addToList(const MLVal &list, const MLVal &item);
}

#endif
