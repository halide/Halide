#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {
// Resets param_buf's fake device fields (set below to get past bounds query
// checks) no matter how the enclosing scope is exited, so its destructor
// doesn't freak out over a non-null device field with a null host field.
struct DeviceFieldResetter {
    Buffer<uint8_t> &buf;
    ~DeviceFieldResetter() {
        buf.raw_buffer()->device = 0;
        buf.raw_buffer()->device_interface = 0;
    }
};
}  // namespace

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not yet support non-host buffers.\n");
        return 0;
    }

    return error_test("null_host_field", "The host pointer of Input buffer ", []() {
        Func f;
        Var x, y;
        ImageParam in(UInt(8), 2);

        // Give the input a device field (to get past bounds query checks)
        // but no host field. If not for the assert failure, we'd segfault
        // (which isn't accepted as correct behavior from the testing
        // infrastructure).
        Buffer<uint8_t> param_buf(10, 10);
        DeviceFieldResetter resetter{param_buf};
        param_buf.raw_buffer()->device = 3;
        param_buf.raw_buffer()->device_interface = (halide_device_interface_t *)(3);
        param_buf.raw_buffer()->host = nullptr;

        f(x, y) = in(x, y);
        f.compute_root();

        in.set(param_buf);
        Buffer<uint8_t> result = f.realize({10, 10});
        (void)result;
    });
}
