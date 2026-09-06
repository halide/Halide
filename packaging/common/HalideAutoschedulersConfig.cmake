cmake_minimum_required(VERSION 3.28)
@PACKAGE_INIT@

include("${CMAKE_CURRENT_LIST_DIR}/HalideAutoschedulers-targets.cmake")

# Autoscheduler plugins are dlopen()'d by the generator executable at
# Generator run time -- never linked -- so this package exposes only their
# IMPORTED MODULE targets (for their $<TARGET_FILE:...>), never a compile or
# link interface. See the ARCH_INDEPENDENT comment in packaging/CMakeLists.txt
# for why that's safe to resolve even while cross-compiling for a different
# word size than the host.
