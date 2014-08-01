#include "runtime_internal.h"

typedef void *objc_id;
typedef void *objc_sel;
extern "C" objc_id objc_getClass(const char *name);
extern "C" objc_sel sel_getUid(const char *str);
extern "C" objc_id objc_msgSend(objc_id self, objc_sel op, ...);

extern "C" void NSLog(objc_id fmt, ...);
// To allocate a constant string, use: __builtin___CFStringMakeConstantString

extern "C" {
WEAK void __halide_print(void *user_context, const char *str) {
    // Buy an autorelease pool because this is not perf critical and it is the
    // really safe thing to do.
    objc_id pool =
        objc_msgSend(objc_msgSend(objc_getClass("NSAutoreleasePool"),
                                  sel_getUid("alloc")), sel_getUid("init"));
    objc_id ns_str =
        objc_msgSend(objc_msgSend(objc_getClass("NSString"),
                                  sel_getUid("alloc")),
                     sel_getUid("initWithUTF8String:"), str);

    NSLog(ns_str);

    objc_msgSend(ns_str, sel_getUid("release"));

    objc_msgSend(pool, sel_getUid("drain"));
}

}
