#!/usr/bin/python3

# Halide tutorial lesson 11.

# This lesson demonstrates how to use Halide as a cross-compiler.

# This lesson can be built by invoking the command:
#    make tutorial_lesson_11_cross_compilation
# in a shell with the current directory at the top of the halide source tree.
# Otherwise, see the platform-specific compiler invocations below.

# On linux, you can compile and run it like so:
# g++ lesson_11*.cpp -g -std=c++11 -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_11
# LD_LIBRARY_PATH=../bin ./lesson_11

# On os x:
# g++ lesson_11*.cpp -g -std=c++11 -I ../include -L ../bin -lHalide -o lesson_11
# DYLD_LIBRARY_PATH=../bin ./lesson_11

#include "Halide.h"
#include <stdio.h>
#using namespace Halide
import halide as hl
from struct import unpack

def main():

    # We'll define the simple one-stage pipeline that we used in lesson 10.
    brighter = hl.Func("brighter")
    x, y = hl.Var("x"), hl.Var("y")

    # Declare the arguments.
    offset = hl.Param(hl.UInt(8))
    input = hl.ImageParam(hl.UInt(8), 2)
    args = [input, offset]

    # Define the hl.Func.
    brighter[x, y] = input[x, y] + offset

    # Schedule it.
    brighter.vectorize(x, 16).parallel(y)

    # The following line is what we did in lesson 10. It compiles an
    # object file suitable for the system that you're running this
    # program on.  For example, if you compile and run this file on
    # 64-bit linux on an x86 cpu with sse4.1, then the generated code
    # will be suitable for 64-bit linux on x86 with sse4.1.
    brighter.compile_to_file("lesson_11_host", args, "lesson_11_host")

    # We can also compile object files suitable for other cpus and
    # operating systems. You do this with an optional third argument
    # to compile_to_file which specifies the target to compile for.

    create_android = True
    create_windows = True
    create_ios = True

    if create_android:
        # Let's use this to compile a 32-bit arm android version of this code:
        target = hl.Target()
        target.os = hl.TargetOS.Android  # The operating system
        target.arch = hl.TargetArch.ARM  # The CPU architecture
        target.bits = 32              # The bit-width of the architecture
        arm_features = []             # A list of features to set
        target.set_features(arm_features)
        # Pass the target as the last argument.
        brighter.compile_to_file("lesson_11_arm_32_android", args, "lesson_11_arm_32_android", target)

    if create_windows:
        # And now a Windows object file for 64-bit x86 with AVX and SSE 4.1:
        target = hl.Target()
        target.os = hl.TargetOS.Windows
        target.arch = hl.TargetArch.X86
        target.bits = 64
        target.set_features([hl.TargetFeature.AVX, hl.TargetFeature.SSE41])
        brighter.compile_to_file("lesson_11_x86_64_windows", args, "lesson_11_x86_64_windows", target)

    if create_ios:
        # And finally an iOS mach-o object file for one of Apple's 32-bit
        # ARM processors - the A6. It's used in the iPhone 5. The A6 uses
        # a slightly modified ARM architecture called ARMv7s. We specify
        # this using the target features field.  Support for Apple's
        # 64-bit ARM processors is very new in llvm, and still somewhat
        # flaky.
        target = hl.Target()
        target.os = hl.TargetOS.IOS
        target.arch = hl.TargetArch.ARM
        target.bits = 32
        target.set_features([hl.TargetFeature.ARMv7s])
        brighter.compile_to_file("lesson_11_arm_32_ios", args, "lesson_11_arm_32_ios", target)


    # Now let's check these files are what they claim, by examining
    # their first few bytes.

    if create_android:
        # 32-arm android object files start with the magic bytes:
        # uint8_t []
        arm_32_android_magic = [0x7f, ord('E'), ord('L'), ord('F'), # ELF format
                                1,       # 32-bit
                                1,       # 2's complement little-endian
                                1]       # Current version of elf

        length = len(arm_32_android_magic)
        f = open("lesson_11_arm_32_android.o", "rb")
        try:
            header_bytes = f.read(length)
        except:
            print("Android object file not generated")
            return -1
        f.close()

        header = list(unpack("B"*length, header_bytes))
        if header != arm_32_android_magic:
            print([x == y for x, y in zip(header, arm_32_android_magic)])
            raise Exception("Unexpected header bytes in 32-bit arm object file.")
            return -1


    if create_windows:
        # 64-bit windows object files start with the magic 16-bit value 0x8664
        # (presumably referring to x86-64)
        # uint8_t  []
        win_64_magic = [0x64, 0x86]

        f = open("lesson_11_x86_64_windows.obj", "rb")
        try:
            header_bytes = f.read(2)
        except:
            print("Windows object file not generated")
            return -1
        f.close()

        header = list(unpack("B"*2, header_bytes))
        if header != win_64_magic:
            raise Exception("Unexpected header bytes in 64-bit windows object file.")
            return -1


    if create_ios:
        # 32-bit arm iOS mach-o files start with the following magic bytes:
        #  uint32_t []
        arm_32_ios_magic = [
            0xfeedface, # Mach-o magic bytes
            #0xfe, 0xed, 0xfa, 0xce, # Mach-o magic bytes
                                       12,  # CPU type is ARM
                                       11,  # CPU subtype is ARMv7s
                                       1]  # It's a relocatable object file.
        f = open("lesson_11_arm_32_ios.o", "rb")
        try:
            header_bytes = f.read(4*4)
        except:
            print("ios object file not generated")
            return -1
        f.close()

        header = list(unpack("I"*4, header_bytes))
        if header != arm_32_ios_magic:
            raise Exception("Unexpected header bytes in 32-bit arm ios object file.")
            return -1


    # It looks like the object files we produced are plausible for
    # those targets. We'll count that as a success for the purposes
    # of this tutorial. For a real application you'd then need to
    # figure out how to integrate Halide into your cross-compilation
    # toolchain. There are several small examples of this in the
    # Halide repository under the apps folder. See HelloAndroid and
    # HelloiOS here:
    # https:#github.com/halide/Halide/tree/master/apps/
    print("Success!")
    return 0


if __name__ == "__main__":
    main()
