vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://dawn.googlesource.com/dawn
    REF 40ddf3b135d952082cdd6e0d03539e924c18221b # chromium/7350
    FETCH_REF chromium/${VERSION}
    PATCHES
        patches/add-dawn-node-install.patch
)

vcpkg_find_acquire_program(PYTHON3)
vcpkg_find_acquire_program(GIT)

function(clone_submodule)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "SOURCE_PATH;URL;REF" "")
    get_filename_component(name "${ARG_SOURCE_PATH}" NAME)
    vcpkg_execute_required_process(
        COMMAND "${GIT}" clone "${ARG_URL}" "${ARG_SOURCE_PATH}"
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME "clone-${name}"
    )
    vcpkg_execute_required_process(
        COMMAND "${GIT}" -C "${ARG_SOURCE_PATH}" checkout "${ARG_REF}"
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME "checkout-${name}"
    )
endfunction()

# Fetch Dawn's dependencies using their script
vcpkg_execute_required_process(
    COMMAND "${PYTHON3}" tools/fetch_dawn_dependencies.py
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME fetch-deps
)

# Manually clone the remaining dependencies that fetch_dawn_dependencies.py doesn't handle
# using the specific commit hashes that chromium/7350 expects
clone_submodule(
    SOURCE_PATH third_party/gpuweb
    URL https://chromium.googlesource.com/external/github.com/gpuweb/gpuweb
    REF a2637f7b880c2556919cdb288fe89815e0ed1c41
)

clone_submodule(
    SOURCE_PATH third_party/node-addon-api
    URL https://chromium.googlesource.com/external/github.com/nodejs/node-addon-api
    REF 1e26dcb52829a74260ec262edb41fc22998669b6
)

clone_submodule(
    SOURCE_PATH third_party/node-api-headers
    URL https://chromium.googlesource.com/external/github.com/nodejs/node-api-headers
    REF d5cfe19da8b974ca35764dd1c73b91d57cd3c4ce
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED
        -DDAWN_BUILD_NODE_BINDINGS=ON
        -DDAWN_BUILD_PROTOBUF=OFF
        -DDAWN_BUILD_SAMPLES=OFF
        -DDAWN_BUILD_TESTS=OFF
        -DDAWN_ENABLE_INSTALL=ON
        -DDAWN_ENABLE_PIC=ON
        -DTINT_BUILD_CMD_TOOLS=OFF
        -DTINT_BUILD_TESTS=OFF
    OPTIONS_RELEASE
        -DBUILD_SHARED_LIBS=OFF
    OPTIONS_DEBUG
        -DBUILD_SHARED_LIBS=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/dawn)

vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
