#ifndef BASE_H
#define BASE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

void panic(const char *fmt, ...);
void Assert(bool condition, const char *fmt, ...);

inline bool fits32(int64_t x) {
    return (int64_t)((int32_t)x) == x;
}



#endif
