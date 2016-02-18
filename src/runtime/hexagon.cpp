#include "runtime_internal.h"
#include "device_interface.h"
#include "HalideRuntimeHexagon.h"
#include "printer.h"

extern "C" {
WEAK int halide_hexagon_run(void *user_context,
                            const char* entry_name,
                            size_t arg_sizes[],
                            void* args[],
                            int8_t arg_is_buffer[]) {

    debug(user_context) << "Hexagon: halide_hexagon_run ("
                        << "user_context: " << user_context << ", "
                        << "entry: " << entry_name << ")\n";

    size_t num_args = 0;
    while (arg_sizes[num_args] != 0) {
        debug(user_context) << "    halide_hexagon_run " << (int)num_args
                            << " " << (int)arg_sizes[num_args]
                            << " [" << (*((void **)args[num_args])) << " ...] "
                            << arg_is_buffer[num_args] << "\n";
        num_args++;
    }

    void** translated_args = (void **)__builtin_alloca(num_args * sizeof(void *));
    uint64_t *dev_handles = (uint64_t *)__builtin_alloca(num_args * sizeof(uint64_t));
    for (size_t i = 0; i < num_args; i++) {
        if (arg_is_buffer[i]) {
            halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
            dev_handles[i] = halide_get_device_handle(*(uint64_t *)args[i]);
            translated_args[i] = &(dev_handles[i]);
            debug(user_context) << "    halide_hexagon_run translated arg" << (int)i
                                << " [" << (*((void **)translated_args[i])) << " ...]\n";
        } else {
            translated_args[i] = args[i];
        }
    }

    debug(user_context) << "    not implemented!\n";

    return 0;
}

namespace {
__attribute__((destructor))
WEAK void halide_hexagon_cleanup() {
}
}

} // extern "C" linkage
