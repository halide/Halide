find_package(OpenGL)
if (OpenGL_FOUND)
    tests(GROUPS opengl
            SOURCES
            conv_select.cpp
            copy_pixels.cpp
            copy_to_device.cpp
            copy_to_host.cpp
            float_texture.cpp
            inline_reduction.cpp
            internal.cpp
            lut.cpp
            multiple_stages.cpp
            produce.cpp
            rewrap_texture.cpp
            save_state.cpp
            select.cpp
            set_pixels.cpp
            shifted_domains.cpp
            special_funcs.cpp
            sumcolor_reduction.cpp
            sum_reduction.cpp
            tuples.cpp
            varying.cpp
            )
    foreach (test_name ${TEST_NAMES})
        target_link_libraries("${test_name}" PRIVATE OpenGL::GL)
    endforeach ()
endif ()