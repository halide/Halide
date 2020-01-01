#ifndef HALIDE_OBJC_SUPPORT_H
#define HALIDE_OBJC_SUPPORT_H

extern "C" {
typedef void *objc_id;
typedef void *objc_sel;
extern objc_id objc_getClass(const char *name);
extern objc_sel sel_getUid(const char *string);
extern objc_id objc_msgSend(objc_id self, objc_sel op, ...);

void NSLog(objc_id /* NSString * */ format, ...);
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK objc_id create_autorelease_pool() {
    objc_id pool =
        objc_msgSend(objc_msgSend(objc_getClass("NSAutoreleasePool"),
                                  sel_getUid("alloc")),
                     sel_getUid("init"));
    return pool;
}

WEAK void drain_autorelease_pool(objc_id pool) {
    objc_msgSend(pool, sel_getUid("drain"));
}

WEAK void retain_ns_object(objc_id obj) {
    objc_msgSend(obj, sel_getUid("retain"));
}

WEAK void release_ns_object(objc_id obj) {
    objc_msgSend(obj, sel_getUid("release"));
}

WEAK objc_id wrap_string_as_ns_string(const char *string, size_t length) {
    typedef objc_id (*init_with_bytes_no_copy_method)(objc_id ns_string, objc_sel sel, const char *string, size_t length, size_t encoding, uint8_t freeWhenDone);
    objc_id ns_string = objc_msgSend(objc_getClass("NSString"), sel_getUid("alloc"));
    init_with_bytes_no_copy_method method = (init_with_bytes_no_copy_method)&objc_msgSend;
    return (*method)(ns_string, sel_getUid("initWithBytesNoCopy:length:encoding:freeWhenDone:"),
                     string, length, 4, 0);
}

extern "C" size_t strlen(const char *string);

WEAK void ns_log_utf8_string(const char *string) {
    objc_id format_string = wrap_string_as_ns_string("%@", 2);
    objc_id ns_string = wrap_string_as_ns_string(string, strlen(string));
    NSLog(format_string, ns_string);
    release_ns_object(ns_string);
    release_ns_object(format_string);
}

WEAK void ns_log_object(objc_id obj) {
    objc_id format_string = wrap_string_as_ns_string("%@", 2);
    NSLog(format_string, obj);
    release_ns_object(format_string);
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif
