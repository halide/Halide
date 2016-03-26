#include "halide_hexagon_remote.h"
#include "rpc_protocol.h"
#include "../HalideRuntime.h"

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;

extern "C" {

#include "stdlib.h"

// The global symbols with which we pass RPC commands and results.
int rpc_call = 0;
int rpc_args[16];
int rpc_ret = 0;

}

int main(int argc, const char **argv) {
    while(true) {
        switch (rpc_call) {
        case Message::Alloc:
            rpc_ret = reinterpret_cast<int>(halide_malloc(NULL, rpc_args[0]));
            break;
        case Message::Free:
            halide_free(NULL, reinterpret_cast<void*>(rpc_args[0]));
            break;
        case Message::InitKernels:
            rpc_ret = halide_hexagon_remote_initialize_kernels(
                reinterpret_cast<unsigned char*>(rpc_args[0]),
                rpc_args[1],
                reinterpret_cast<handle_t*>(rpc_args[2]));
            break;
        case Message::Run:
            rpc_ret = halide_hexagon_remote_run(
                static_cast<handle_t>(rpc_args[0]),
                static_cast<handle_t>(rpc_args[1]),
                reinterpret_cast<const buffer*>(rpc_args[2]),
                rpc_args[3],
                reinterpret_cast<const buffer*>(rpc_args[4]),
                rpc_args[5],
                reinterpret_cast<buffer*>(rpc_args[6]),
                rpc_args[7]);
            break;
        case Message::ReleaseKernels:
            rpc_ret = halide_hexagon_remote_release_kernels(
                static_cast<handle_t>(rpc_args[0]),
                rpc_args[1]);
            break;
        case Message::Break:
            return 0;
        default:
            return -1;
        }
        // Setting the message to zero indicates to the caller that
        // we're done processing the message.
        rpc_call = 0;
    }
    return 0;
}
