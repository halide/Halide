# Toolchain for cross-compiling to Linux-arm32 on a Linux-x86-64 or Linux-aarch64 host.
#
# If you just want to crosscompile, you need to install:
#
#   apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf
#
# On an aarch64 host, you can also run the results if you:
#
#   dpkg --add-architecture armhf
#   apt-get install libc6:armhf libstdc++6:armhf

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if (NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
endif ()
if (NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
endif ()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# add_custom_command() will make bad decisions about running the command
# when crosscompiling (it won't expand the target into a full path).
# Setting CMAKE_CROSSCOMPILING_EMULATOR to /usr/bin/env tricks it into
# doing the right thing (ie, running it directly). Note that if you want
# to build/run on x86-64 systems, you could set this to some qemu command\
# (though the results will likely be very slow).
set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/env)
