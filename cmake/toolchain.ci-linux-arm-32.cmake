set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

set(CMAKE_C_FLAGS_INIT "-mfp16-format=ieee -Wno-psabi")
set(CMAKE_CXX_FLAGS_INIT "-mfp16-format=ieee -Wno-psabi")

set(VCPKG_TARGET_TRIPLET arm-linux)
set(CMAKE_CROSSCOMPILING FALSE)
