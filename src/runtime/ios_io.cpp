#include "mini_stdint.h"

#define WEAK __attribute__((weak))

typedef void *objc_id;
typedef void *objc_sel;
extern "C" objc_id objc_getClass(const char *name);
extern "C" objc_sel sel_getUid(const char *str);
extern "C" objc_id objc_msgSend(objc_id self, objc_sel op, ...);

extern "C" void NSLogv(objc_id fmt, __builtin_va_list args);
// To allocate a constant string, use: __builtin___CFStringMakeConstantString

extern "C" {
WEAK int halide_printf(void *user_context, const char * fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    // Buy an autorelease pool because this is not perf critical and it is the
    // really safe thing to do.
    objc_id pool =
        objc_msgSend(objc_msgSend(objc_getClass("NSAutoreleasePool"),
                                  sel_getUid("alloc")), sel_getUid("init"));
    objc_id ns_fmt =
        objc_msgSend(objc_msgSend(objc_getClass("NSString"),
                                  sel_getUid("alloc")),
                     sel_getUid("initWithUTF8String:"), fmt);

    NSLogv(ns_fmt, args);

    objc_msgSend(ns_fmt, sel_getUid("release"));

    objc_msgSend(pool, sel_getUid("drain"));

    __builtin_va_end(args);

    // On most systems, vprintf returns the number of characters
    // printed, but on ios, NSLogV returns no such information, so
    // just return 1 to keep any assertions wrapped around this happy.
    return 1;


}

}
