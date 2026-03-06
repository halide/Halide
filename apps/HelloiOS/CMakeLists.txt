cmake_minimum_required(VERSION 3.28)
project(HelloiOS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(HalideHelpers REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)

add_halide_generator(
    reaction_diffusion_2_generator
    SOURCES Generators/reaction_diffusion_2_generator.cpp
)

# A host configure only needs to export the generator package for the iOS build.
if (NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    return()
endif ()

enable_language(OBJC)
enable_language(OBJCXX)

set(HELLOIOS_DEVELOPMENT_TEAM "" CACHE STRING
    "Optional Apple development team identifier for code signing")

if (NOT HelloiOS-halide_generators_FOUND)
    message(FATAL_ERROR
            "HelloiOS iOS builds require host-built generators. "
            "Set -DHelloiOS-halide_generators_ROOT=... or add the host build "
            "directory to CMAKE_PREFIX_PATH.")
endif ()

set(helloios_generator_target HelloiOS::halide_generators::reaction_diffusion_2_generator)
foreach (stage IN ITEMS init update render)
    add_halide_library(reaction_diffusion_2_${stage}
                       FROM ${helloios_generator_target}
                       GENERATOR reaction_diffusion_2_${stage}
                       FUNCTION_NAME reaction_diffusion_2_${stage}
                       FEATURES user_context)

    add_halide_library(reaction_diffusion_2_metal_${stage}
                       FROM ${helloios_generator_target}
                       GENERATOR reaction_diffusion_2_${stage}
                       FUNCTION_NAME reaction_diffusion_2_metal_${stage}
                       FEATURES metal user_context)
endforeach ()

find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(UIKIT_FRAMEWORK UIKit REQUIRED)

add_executable(
    HelloiOS MACOSX_BUNDLE
    HelloiOS/AppDelegate.h
    HelloiOS/AppDelegate.mm
    HelloiOS/HalideView.h
    HelloiOS/HalideView.mm
    HelloiOS/HalideViewController.h
    HelloiOS/HalideViewController.mm
    HelloiOS/SceneDelegate.h
    HelloiOS/SceneDelegate.mm
    HelloiOS/main.mm
    HelloiOS/Images.xcassets
    HelloiOS/en.lproj/InfoPlist.strings
)

target_link_libraries(
    HelloiOS PRIVATE
    Halide::Runtime
    reaction_diffusion_2_init
    reaction_diffusion_2_update
    reaction_diffusion_2_render
    reaction_diffusion_2_metal_init
    reaction_diffusion_2_metal_update
    reaction_diffusion_2_metal_render
    ${COREGRAPHICS_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${METAL_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
    ${UIKIT_FRAMEWORK}
)

set_target_properties(
    HelloiOS PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/HelloiOS/HelloiOS-Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.halide.HelloiOS"
    MACOSX_BUNDLE_BUNDLE_NAME "HelloiOS"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    OBJCXX_STANDARD 17
    OBJCXX_STANDARD_REQUIRED YES
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.halide.HelloiOS"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
)

if (HELLOIOS_DEVELOPMENT_TEAM)
    set_target_properties(
        HelloiOS PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${HELLOIOS_DEVELOPMENT_TEAM}"
    )
else ()
    set_target_properties(
        HelloiOS PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    )
endif ()
