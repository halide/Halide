set(THREADS_PREFER_PTHREAD_FLAG YES)
find_package(Threads REQUIRED)
set_target_properties(Threads::Threads PROPERTIES IMPORTED_GLOBAL TRUE)

find_package(OpenGL)
if (TARGET OpenGL::GL)
    set_target_properties(OpenGL::GL PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

find_package(OpenMP)
if (TARGET OpenMP::OpenMP_CXX)
    set_target_properties(OpenMP::OpenMP_CXX PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

add_subdirectory(clang)
add_subdirectory(llvm)

add_subdirectory(jpeg)
add_subdirectory(png)