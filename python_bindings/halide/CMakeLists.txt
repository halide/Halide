add_subdirectory(src)

if (WITH_PYTHON_STUBS)
    add_subdirectory(stub)
endif ()

if (WITH_TEST_PYTHON)
    add_subdirectory(apps)
    add_subdirectory(test)
endif ()

if (WITH_TUTORIALS)
    add_subdirectory(tutorial)
endif ()

if (WITH_PACKAGING)
    add_subdirectory(packaging)
endif ()
