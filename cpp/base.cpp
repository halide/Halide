#include "base.h"

void panic(const char *fmt, ...) {
    char message[1024];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(message, 1024, fmt, arglist);
    va_end(arglist);
    printf("%s", message);
    exit(-1);
}

void Assert(bool cond, const char *fmt, ...) {
    if (cond) return;
    char message[1024];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(message, 1024, fmt, arglist);
    va_end(arglist);
    printf("%s", message);
    exit(-1);
}


