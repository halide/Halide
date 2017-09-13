#ifndef SIM_PROTOCOL_H
#define SIM_PROTOCOL_H

namespace Message {
enum {
    None = 0,
    Alloc,
    Free,
    LoadLibrary,
    GetSymbol,
    Run,
    ReleaseLibrary,
    Break,
};
}

#endif  // SIM_PROTOCOL_H
