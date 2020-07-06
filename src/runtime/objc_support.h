#ifndef HALIDE_OBJC_SUPPORT_H
#define HALIDE_OBJC_SUPPORT_H

extern "C" {
typedef void *objc_id;
typedef void *objc_sel;
extern objc_id objc_getClass(const char *name);
extern objc_sel sel_getUid(const char *string);
extern void objc_msgSend(void);

void NSLog(objc_id /* NSString * */ format, ...);
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK objc_id create_autorelease_pool() {
    typedef objc_id (*pool_method)(objc_id obj, objc_sel sel);
    pool_method method = (pool_method)&objc_msgSend;
    objc_id pool = (*method)(objc_getClass("NSAutoreleasePool"), sel_getUid("alloc"));
    pool = (*method)(pool, sel_getUid("init"));
    return pool;
}

WEAK void drain_autorelease_pool(objc_id pool) {
    typedef objc_id (*drain_method)(objc_id obj, objc_sel sel);
    drain_method method = (drain_method)&objc_msgSend;
    (*method)(pool, sel_getUid("drain"));
}

WEAK void retain_ns_object(objc_id obj) {
    typedef objc_id (*retain_method)(objc_id obj, objc_sel sel);
    retain_method method = (retain_method)&objc_msgSend;
    (*method)(obj, sel_getUid("retain"));
}

WEAK void release_ns_object(objc_id obj) {
    typedef objc_id (*release_method)(objc_id obj, objc_sel sel);
    release_method method = (release_method)&objc_msgSend;
    (*method)(obj, sel_getUid("release"));
}

WEAK objc_id wrap_string_as_ns_string(const char *string, size_t length) {
    typedef objc_id (*init_with_bytes_no_copy_method)(objc_id ns_string, objc_sel sel, const char *string, size_t length, size_t encoding, uint8_t freeWhenDone);
    typedef objc_id (*alloc_method)(objc_id objc, objc_sel sel);
    alloc_method method1 = (alloc_method)&objc_msgSend;
    objc_id ns_string = (*method1)(objc_getClass("NSString"), sel_getUid("alloc"));
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
