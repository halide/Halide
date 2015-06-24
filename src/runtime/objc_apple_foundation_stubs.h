#ifndef HALIDE_OBJC_APPLE_FOUNDATION_STUBS_H
#define HALIDE_OBJC_APPLE_FOUNDATION_STUBS_H

namespace Halide { namespace Runtime { namespace Internal {

class AutoreleasePool {
private:
    void *pool;
public:
    AutoreleasePool();
    ~AutoreleasePool();
};

void ns_log_utf8_string(const char *str);

}}}

#endif // HALIDE_OBJC_APPLE_FOUNDATION_STUBS_H



