if (APPLE)
    # This changes how find_xxx() commands work; the default is to find frameworks before
    # standard libraries or headers, but this can be a problem on systems that have Mono
    # installed, as it has a framework with the libjpeg and libpng  headers present -- so
    # CMake finds the headers from Mono but the libraries from Homebrew, and hilarity ensues.
    # Setting this to "last" means we always try the standard libraries before the frameworks.
    set(CMAKE_FIND_FRAMEWORK LAST)
endif ()

find_package(JPEG)
if (TARGET JPEG::JPEG)
    set_target_properties(JPEG::JPEG PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()