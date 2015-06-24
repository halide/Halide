#include "runtime_internal.h"
#include "objc_apple_foundation_stubs.h"

typedef size_t NSUInteger;

@interface NSAutoreleasePool
+(NSAutoreleasePool *)alloc;
-init;
-drain;
@end

@interface NSString;
+(NSString *)alloc;
-(NSString *)initWithBytesNoCopy:(const char *)bytes length:(NSUInteger)length encoding:(NSUInteger)encoding freeWhenDone:(bool)freeWhenDone;
-release;
@end

extern "C" {
void NSLog(NSString *format, ...);
}

namespace Halide { namespace Runtime { namespace Internal {

WEAK AutoreleasePool::AutoreleasePool() {
    pool = [[NSAutoreleasePool alloc] init];
}

WEAK AutoreleasePool::~AutoreleasePool() {
    [(NSAutoreleasePool *)pool drain];
}

WEAK void ns_log_utf8_string(const char *str) {
    NSString *message = [[NSString alloc] initWithBytesNoCopy: const_cast<char *>(str) length: strlen(str) encoding: 4 freeWhenDone: 0];
    NSLog(@"%@", message);
    [message release];
}

}}}

