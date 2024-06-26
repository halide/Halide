// Halide tutorial lesson 11: Cross-compilation

// This lesson demonstrates how to use Halide as a cross-compiler to
// generate code for any platform from any platform.

// On linux, you can compile and run it like so:
// g++ lesson_11*.cpp -g -std=c++17 -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -lpthread -ldl -o lesson_11
// LD_LIBRARY_PATH=<path/to/libHalide.so> ./lesson_11

// On os x:
// g++ lesson_11*.cpp -g -std=c++17 -I <path/to/Halide.h> -L <path/to/libHalide.so> -lHalide -o lesson_11
// DYLD_LIBRARY_PATH=<path/to/libHalide.dylib> ./lesson_11

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_11_cross_compilation
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>
using namespace Halide;

int main(int argc, char **argv) {

    // We'll define the simple one-stage pipeline that we used in lesson 10.
    Func brighter;
    Var x, y;

    // Declare the arguments.
    Param<uint8_t> offset;
    ImageParam input(type_of<uint8_t>(), 2);
    std::vector<Argument> args(2);
    args[0] = input;
    args[1] = offset;

    // Define the Func.
    brighter(x, y) = input(x, y) + offset;

    // Schedule it.
    brighter.vectorize(x, 16).parallel(y);

    // The following line is what we did in lesson 10. It compiles an
    // object file suitable for the system that you're running this
    // program on.  For example, if you compile and run this file on
    // 64-bit linux on an x86 cpu with sse4.1, then the generated code
    // will be suitable for 64-bit linux on x86 with sse4.1.
    brighter.compile_to_file("lesson_11_host", args, "brighter");

    // We can also compile object files suitable for other cpus and
    // operating systems. You do this with an optional third argument
    // to compile_to_file which specifies the target to compile for.

    // Let's use this to compile a 32-bit arm android version of this code:
    Target target;
    target.os = Target::Android;                // The operating system
    target.arch = Target::ARM;                  // The CPU architecture
    target.bits = 32;                           // The bit-width of the architecture
    std::vector<Target::Feature> arm_features;  // A list of features to set
    target.set_features(arm_features);
    // We then pass the target as the last argument to compile_to_file.
    brighter.compile_to_file("lesson_11_arm_32_android", args, "brighter", target);

    // And now a Windows object file for 64-bit x86 with AVX and SSE 4.1:
    target.os = Target::Windows;
    target.arch = Target::X86;
    target.bits = 64;
    std::vector<Target::Feature> x86_features;
    x86_features.push_back(Target::AVX);
    x86_features.push_back(Target::SSE41);
    target.set_features(x86_features);
    brighter.compile_to_file("lesson_11_x86_64_windows", args, "brighter", target);

    // And finally an iOS mach-o object file for one of Apple's 32-bit
    // ARM processors - the A6. It's used in the iPhone 5. The A6 uses
    // a slightly modified ARM architecture called ARMv7s. We specify
    // this using the target features field.  Support for Apple's
    // 64-bit ARM processors is very new in llvm, and still somewhat
    // flaky.
    target.os = Target::IOS;
    target.arch = Target::ARM;
    target.bits = 32;
    std::vector<Target::Feature> armv7s_features;
    armv7s_features.push_back(Target::ARMv7s);
    target.set_features(armv7s_features);
    brighter.compile_to_file("lesson_11_arm_32_ios", args, "brighter", target);

    // Now let's check these files are what they claim, by examining
    // their first few bytes.

    // 32-arm android object files start with the magic bytes:
    uint8_t arm_32_android_magic[] = {0x7f, 'E', 'L', 'F',  // ELF format
                                      1,                    // 32-bit
                                      1,                    // 2's complement little-endian
                                      1};                   // Current version of elf

    FILE *f = fopen("lesson_11_arm_32_android.o", "rb");
    uint8_t header[32];
    if (!f || fread(header, 32, 1, f) != 1) {
        printf("Object file not generated\n");
        return -1;
    }
    fclose(f);

    if (memcmp(header, arm_32_android_magic, sizeof(arm_32_android_magic))) {
        printf("Unexpected header bytes in 32-bit arm object file.\n");
        return -1;
    }

    // 64-bit windows object files start with the magic 16-bit value 0x8664
    // (presumably referring to x86-64)
    uint8_t win_64_magic[] = {0x64, 0x86};

    f = fopen("lesson_11_x86_64_windows.obj", "rb");
    if (!f || fread(header, 32, 1, f) != 1) {
        printf("Object file not generated\n");
        return -1;
    }
    fclose(f);

    if (memcmp(header, win_64_magic, sizeof(win_64_magic))) {
        printf("Unexpected header bytes in 64-bit windows object file.\n");
        return -1;
    }

    // 32-bit arm iOS mach-o files start with the following magic bytes:
    uint32_t arm_32_ios_magic[] = {0xfeedface,  // Mach-o magic bytes
                                   12,          // CPU type is ARM
                                   11,          // CPU subtype is ARMv7s
                                   1};          // It's a relocatable object file.
    f = fopen("lesson_11_arm_32_ios.o", "rb");
    if (!f || fread(header, 32, 1, f) != 1) {
        printf("Object file not generated\n");
        return -1;
    }
    fclose(f);

    if (memcmp(header, arm_32_ios_magic, sizeof(arm_32_ios_magic))) {
        printf("Unexpected header bytes in 32-bit arm ios object file.\n");
        return -1;
    }

    // It looks like the object files we produced are plausible for
    // those targets. We'll count that as a success for the purposes
    // of this tutorial. For a real application you'd then need to
    // figure out how to integrate Halide into your cross-compilation
    // toolchain. There are several small examples of this in the
    // Halide repository under the apps folder. See HelloAndroid and
    // HelloiOS here:
    // https://github.com/halide/Halide/tree/main/apps/
    printf("Success!\n");
    return 0;
}
