#ifndef HALIDE_RPC_PROTOCOL_H
#define HALIDE_RPC_PROTOCOL_H

namespace Message {
enum {
    None = 0,
    Alloc,
    Free,
    InitKernels,
    GetSymbol,
    Run,
    ReleaseKernels,
    Break,
};
}

#endif
