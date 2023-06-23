#Set a number of variables dependent on the Hexagon SDK
if (DEFINED ENV{HEXAGON_SDK_ROOT})
  set(HEXAGON_SDK_ROOT $ENV{HEXAGON_SDK_ROOT})
else ()
  set(HEXAGON_SDK_ROOT $ENV{HOME}/Qualcomm/Hexagon_SDK/4.3.0.0)
endif()
if (DEFINED ENV{HEXAGON_TOOLS_VER})
  set (HEXAGON_TOOLS_ROOT ${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/$ENV{HEXAGON_TOOLS_VER})
else ()
  set (HEXAGON_TOOLS_ROOT ${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/8.4.11)
endif()

set(ANDROID_NDK_ROOT ${HEXAGON_SDK_ROOT}/tools/android-ndk-r19c)

message(STATUS "Using Hexagon SDK at ${HEXAGON_SDK_ROOT}")
message(STATUS "Using Android NDK at ${ANDROID_NDK_ROOT}")
message(STATUS "Using Hexagon LLVM TOols at ${HEXAGON_TOOLS_ROOT}")

set(FASTRPC_PATH ${HEXAGON_SDK_ROOT}/ipc/fastrpc)
set(CC_HEXAGON  ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-clang)
set(CXX_HEXAGON ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-clang++)
set(AR_HEXAGON  ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-ar)
set(HEXAGON_QAIC ${HEXAGON_SDK_ROOT}/ipc/fastrpc/qaic/Ubuntu16/qaic)
set(LD_HEXAGON  ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-link)

set(CC_ARM_64_ANDROID ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang)
set(CXX_ARM_64_ANDROID ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++)
set(CCFLAGS_ARM_64_ANDROID "-target" "aarch64-linux-android21")

set(CC_ARM_32_ANDROID ${CC_ARM_64_ANDROID})
set(CXX_ARM_32_ANDROID ${CXX_ARM_64_ANDROID})
set(CCFLAGS_ARM_32_ANDROID "-target" "armv7a-linux-androideabi21")


set(HEXAGON_ARCH 65)
set(HVX_LENGTH 128)
set(MV -mv${HEXAGON_ARCH})
set(QURT_INCLUDES "${HEXAGON_SDK_ROOT}/rtos/qurt/computev${HEXAGON_ARCH}/include/qurt")
#set(POSIX_INCLUDES "${HEXAGON_SDK_ROOT}/rtos/qurt/computev${HEXAGON_ARCH}/include/posix")
set(HEXAGON_SDK_INCLUDES "${HEXAGON_SDK_ROOT}/incs")
set(COMMON_INCLUDE_FLAGS "-I" "${HEXAGON_SDK_INCLUDES}/stddef" "-I" "${CMAKE_CURRENT_SOURCE_DIR}/.." "-I" "${HEXAGON_SDK_INCLUDES}" "-I" "${HEXAGON_SDK_ROOT}/libs/qprintf/inc")
set(COMMON_CCFLAGS "${COMMON_INCLUDE_FLAGS}" "-O3")
#set(CCFLAGS_HEXAGON "${COMMON_CCFLAGS}" "-I" "${QURT_INCLUDES}" "-I" "${POSIX_INCLUDES}" "-mhvx" "-mhvx-length=${HVX_LENGTH}B" "-fno-exceptions" "${MV}")
set(CCFLAGS_HEXAGON "${COMMON_CCFLAGS}" "-I" "${QURT_INCLUDES}"  "-mhvx" "-mhvx-length=${HVX_LENGTH}B" "-fno-exceptions" "${MV}")

list(APPEND CCFLAGS_ARM_64_ANDROID  ${COMMON_CCFLAGS} -llog -fPIE -pie "-L${FASTRPC_PATH}/remote/ship/android_aarch64")
list(APPEND CCFLAGS_ARM_32_ANDROID ${COMMON_CCFLAGS}  -llog -fPIE -pie "-L${FASTRPC_PATH}/remote/ship/android")

macro(get_hexagon_value OBJECT OUTPUT_VAR)
  execute_process(COMMAND ${CXX_HEXAGON}
    -mv65 -G0 -print-file-name=${OBJECT}
    OUTPUT_VARIABLE ${OUTPUT_VAR}
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endmacro()

get_hexagon_value(crt0_standalone.o CRT0_STANDALONE)
get_hexagon_value(crt0.o CRT0)
get_hexagon_value(init.o INIT)
get_hexagon_value(libstandalone.a LIB_STANDALONE)
get_hexagon_value(libc.a LIB_C)
get_hexagon_value(libgcc.a LIB_GCC)
get_hexagon_value(fini.o FINI)
set(LIBDL ${HEXAGON_TOOLS_ROOT}/Tools/target/hexagon/lib/v65/G0/libdl.a)

# *************** Building hexagon_sim_remote ******************************
add_custom_command(OUTPUT halide_hexagon_remote.h halide_hexagon_remote_skel.c halide_hexagon_remote_stub.c
  COMMAND ${HEXAGON_QAIC} ${COMMON_INCLUDE_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/halide_hexagon_remote.idl
  MAIN_DEPENDENCY halide_hexagon_remote.idl
  VERBATIM)

add_custom_command(OUTPUT known_symbols.o
  COMMAND "${CXX_HEXAGON}" ${CCFLAGS_HEXAGON} "-fPIC" "-c" ${CMAKE_CURRENT_SOURCE_DIR}/known_symbols.cpp
  MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/known_symbols.cpp
  VERBATIM)

add_custom_command(OUTPUT sim_remote.o
  COMMAND ${CXX_HEXAGON} ${CCFLAGS_HEXAGON} -I ${CMAKE_CURRENT_SOURCE_DIR} -I ${CMAKE_CURRENT_BINARY_DIR} -c  ${CMAKE_CURRENT_SOURCE_DIR}/sim_remote.cpp
  MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/sim_remote.cpp
  DEPENDS known_symbols.h log.h sim_protocol.h halide_hexagon_remote.h
  VERBATIM)

add_custom_command(OUTPUT sim_qurt.o
  COMMAND ${CXX_HEXAGON} ${CCFLAGS_HEXAGON}  -c  ${CMAKE_CURRENT_SOURCE_DIR}/sim_qurt.cpp
  MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/sim_qurt.cpp
  VERBATIM)

add_custom_command(OUTPUT sim_qurt_vtcm.o
  COMMAND ${CXX_HEXAGON} ${CCFLAGS_HEXAGON}  -c  ${CMAKE_CURRENT_SOURCE_DIR}/sim_qurt_vtcm.cpp
  MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/sim_qurt_vtcm.cpp
  VERBATIM)

add_custom_command(OUTPUT libsim_qurt.a
  COMMAND ${AR_HEXAGON} rcs libsim_qurt.a sim_qurt.o sim_qurt_vtcm.o
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/sim_qurt.o ${CMAKE_CURRENT_BINARY_DIR}/sim_qurt_vtcm.o
  VERBATIM)

add_custom_command(OUTPUT hexagon_sim_remote
  COMMAND ${LD_HEXAGON} ${MV} -G0 -o hexagon_sim_remote "${CRT0_STANDALONE}" "${CRT0}" "${INIT}" sim_remote.o known_symbols.o "${LIBDL}" --start-group ${LIB_STANDALONE} --whole-archive ${LIB_C} libsim_qurt.a --no-whole-archive ${LIB_GCC} --end-group ${FINI} --dynamic-linker= -E --force-dynamic
  DEPENDS libsim_qurt.a sim_remote.o known_symbols.o
  VERBATIM)

add_custom_target(sim_remote ALL DEPENDS hexagon_sim_remote)

# *************** Building libhalide_hexagon_remote_skel.so ******************************

add_custom_command(OUTPUT halide_remote.o
  COMMAND ${CXX_HEXAGON} ${CCFLAGS_HEXAGON} -I ${CMAKE_CURRENT_SOURCE_DIR} -I ${CMAKE_CURRENT_BINARY_DIR} -fPIC -c  ${CMAKE_CURRENT_SOURCE_DIR}/halide_remote.cpp
  MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/halide_remote.cpp
  DEPENDS known_symbols.h log.h halide_hexagon_remote.h
  VERBATIM)

macro(compile_object_for_skel COMPILER OBJECT SRC)
  add_custom_command(OUTPUT ${OBJECT}
    COMMAND ${COMPILER} ${CCFLAGS_HEXAGON} -fPIC -c ${SRC}
    MAIN_DEPENDENCY ${SRC}
    VERBATIM)
endmacro()

compile_object_for_skel(${CC_HEXAGON} halide_hexagon_remote_skel.o ${CMAKE_CURRENT_BINARY_DIR}/halide_hexagon_remote_skel.c)
compile_object_for_skel(${CXX_HEXAGON} nearbyint.o ${CMAKE_CURRENT_SOURCE_DIR}/nearbyint.cpp)
compile_object_for_skel(${CXX_HEXAGON} c11_stubs.o ${CMAKE_CURRENT_SOURCE_DIR}/c11_stubs.cpp)
compile_object_for_skel(${CXX_HEXAGON} log.o ${CMAKE_CURRENT_SOURCE_DIR}/log.cpp)
compile_object_for_skel(${CXX_HEXAGON} known_symbols.o ${CMAKE_CURRENT_SOURCE_DIR}/known_symbols.cpp)

add_custom_command(OUTPUT libhalide_hexagon_remote_skel.so
  COMMAND ${CC_HEXAGON} ${MV} -mG0lib -G0 -fPIC -shared -lc halide_remote.o halide_hexagon_remote_skel.o nearbyint.o c11_stubs.o log.o known_symbols.o
  -Wl,-soname=libhalide_hexagon_remote_skel.so -Wl,--no-threads -o libhalide_hexagon_remote_skel.so
  -Wl,-Bsymbolic -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free
  -Wl,--wrap=realloc -Wl,--wrap=memalign -Wl,--wrap=__stack_chk_fail -Wl,--whole-archive ${HEXAGON_SDK_ROOT}/libs/qprintf/prebuilt/hexagon_toolv84_v65/libqprintf.a
  DEPENDS halide_remote.o halide_hexagon_remote_skel.o nearbyint.o c11_stubs.o log.o known_symbols.o
  VERBATIM)

add_custom_target(remote_skel_so ALL DEPENDS libhalide_hexagon_remote_skel.so)

# ************* Building host/libhalide_hexagon_host.so **********************************

# We do not need to use add_custom_command and add_custom_target because this libhalide_hexagon_host.so is built with the native compiler.
add_library(halide_hexagon_host SHARED sim_host.cpp sim_protocol.h)
target_compile_options(halide_hexagon_host PRIVATE -std=c++-17 -fPIC)
target_include_directories(halide_hexagon_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${HEXAGON_TOOLS_ROOT}/Tools/include/iss)
target_link_directories(halide_hexagon_host PRIVATE ${HEXAGON_TOOLS_ROOT}/Tools/lib/iss)
target_link_libraries(halide_hexagon_host PRIVATE wrapper)

# ***********Building arm-64-android/libhalide_hexagon_host.so

macro(build_libhalide_hexagon_host_so BITS PLATFORM) # OBJECT SRC DEPENDENCIES)
  if (${BITS} EQUAL 32)
    set(CXX_CROSS_COMPILER ${CXX_ARM_32_ANDROID})
    set(CC_CROSS_COMPILER ${CC_ARM_32_ANDROID})
    set(CCFLAGS_CROSS_COMPILER ${CCFLAGS_ARM_32_ANDROID})
    set(BIN_DIR arm-32-android)
    set(TARGET_NAME arm_32_android_hexagon_host_so)
  else()
    set(CXX_CROSS_COMPILER ${CXX_ARM_64_ANDROID})
    set(CC_CROSS_COMPILER ${CC_ARM_64_ANDROID})
    set(CCFLAGS_CROSS_COMPILER ${CCFLAGS_ARM_64_ANDROID})
    set(BIN_DIR arm-64-android)
    set(TARGET_NAME arm_64_android_hexagon_host_so)
  endif()

  add_custom_command(OUTPUT ${BIN_DIR}/host_shim.o
    COMMAND ${CXX_CROSS_COMPILER} ${CCFLAGS_CROSS_COMPILER} -I ${CMAKE_CURRENT_BINARY_DIR} -fPIC
    -c ${CMAKE_CURRENT_SOURCE_DIR}/host_shim.cpp -o ${BIN_DIR}/host_shim.o
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/host_shim.cpp
    DEPENDS halide_hexagon_remote.h
    VERBATIM)
  add_custom_command(OUTPUT ${BIN_DIR}/host_malloc.o
    COMMAND ${CXX_CROSS_COMPILER} ${CCFLAGS_CROSS_COMPILER} -I ${CMAKE_CURRENT_BINARY_DIR} -fPIC
    -c ${CMAKE_CURRENT_SOURCE_DIR}/host_malloc.cpp -o ${BIN_DIR}/host_malloc.o
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/host_malloc.cpp
    VERBATIM)
  add_custom_command(OUTPUT ${BIN_DIR}/libadsprpc_shim.o
    COMMAND ${CXX_CROSS_COMPILER} ${CCFLAGS_CROSS_COMPILER} -I ${CMAKE_CURRENT_BINARY_DIR} -fPIC
    -c ${CMAKE_CURRENT_SOURCE_DIR}/libadsprpc_shim.cpp -o ${BIN_DIR}/libadsprpc_shim.o
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/libadsprpc_shim.cpp
    VERBATIM)
  add_custom_command(OUTPUT ${BIN_DIR}/libhalide_hexagon_host.so
    COMMAND ${CC_CROSS_COMPILER} ${CCFLAGS_CROSS_COMPILER} halide_hexagon_remote_stub.c ${BIN_DIR}/host_malloc.o ${BIN_DIR}/host_shim.o ${BIN_DIR}/libadsprpc_shim.o
    -Wl,-soname,libhalide_hexagon_host.so ${EXTRA_LDFLAGS} -shared -o ${BIN_DIR}/libhalide_hexagon_host.so -lcdsprpc
    DEPENDS halide_hexagon_remote_stub.c ${BIN_DIR}/host_malloc.o ${BIN_DIR}/host_shim.o ${BIN_DIR}/libadsprpc_shim.o
    VERBATIM)
  add_custom_target(${TARGET_NAME} ALL DEPENDS ${BIN_DIR}/libhalide_hexagon_host.so)
endmacro()

build_libhalide_hexagon_host_so(32 "Android")
build_libhalide_hexagon_host_so(64 "Android")
