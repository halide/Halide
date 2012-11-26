#ifndef ARGUMENT_H
#define ARGUMENT_H

#include <string>
#include "Type.h"

namespace HalideInternal {

    struct Argument {
        std::string name;        

        // It's either a primitive type (for uniforms), or a buffer pointer
        bool is_buffer;
        Type type;
    };
}

#endif
