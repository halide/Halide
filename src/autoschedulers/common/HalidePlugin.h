#ifndef HALIDE_HALIDEPLUGIN_H
#define HALIDE_HALIDEPLUGIN_H

#include "Errors.h"

#define REGISTER_AUTOSCHEDULER(NAME)                                  \
    struct HALIDE_EXPORT Register##NAME {                             \
        Register##NAME() {                                            \
            debug(1) << "Registering autoscheduler '" #NAME "'...\n"; \
            Pipeline::add_autoscheduler(#NAME, NAME());               \
        }                                                             \
    } register_##NAME;

#endif  // HALIDE_HALIDEPLUGIN_H
