#include "runtime_internal.h"
#include "objc_apple_foundation_stubs.h"

typedef size_t NSUInteger;

@interface NSObject
+ (instancetype)alloc;
@end

@interface NSAutoreleasePool : NSObject
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

WEAK void *halide_ns_create_autorelease_pool() {
    return [[NSAutoreleasePool alloc] init];
}

WEAK void halide_ns_release_and_free_autorelease_pool(void *pool) {
    [(NSAutoreleasePool *)pool drain];
}

WEAK void halide_ns_log_utf8_string(const char *str) {
    NSString *message = [[NSString alloc] initWithBytesNoCopy: const_cast<char *>(str) length: strlen(str) encoding: 4 freeWhenDone: 0];
    NSLog(@"%@", message);
    [message release];
}

}

