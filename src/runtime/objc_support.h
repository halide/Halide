#ifndef HALIDE_OBJC_SUPPORT_H
#define HALIDE_OBJC_SUPPORT_H

extern "C" {
typedef void *objc_id;
typedef void *objc_sel;
extern objc_id objc_getClass(const char *name);
extern objc_sel sel_getUid(const char *string);

// Recent versions of macOS have changed the signature
// for objc_msgSend(), since its implementation is ABI-dependent.
// With this signature, users must always cast the call to the
// correct function type, and any calls without casting to the right
// function type will be caught at compile time.
//
// A good explanation of why is here: https://www.mikeash.com/pyblog/objc_msgsends-new-prototype.html
//
// The TL;DR is that objc_msgSend is special in that what it does is not touch
// the registers at all and 1) loads the ObjC class corresponding to the object, 2)
// looks up the selector in the class’s method cache, and 3) jumps to the location of
// the method if it’s in the cache. objc_msgSend takes as parameters an object,
// the method selector, then all the other parameters to the method; when it
// jumps to the method implementation, it's as if the method implementation was called
// directly.
// C doesn’t have a way to express this kind of function: the method implementation (usually) uses the ABI
// calling convention for a normal non-variadic function, but the C prototype for
// this has to be variadic. That used to be okay(ish) on x64 because the variadic and
// normal conventions are basically the same, but on ARM a variadic function passes
// all params on the stack, which would be a huge amount of overhead given how many
// times its called (every single method invocation in ObjC).
// The other problem with using a variadic signature is that C does type promotion on a variadic
// call.
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
