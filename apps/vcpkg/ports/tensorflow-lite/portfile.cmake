vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tensorflow/tensorflow
    REF a344c313f75bbe28d00abe63f5d49cd6f233b6d0  # 2.21.0-rc0
    SHA512 48463c1dde438f18bf8d17104537258d6edf308be9f48af4fb40abee011a6f8c1c8aa50220fd1928bdfb8f251b8a9c764a1db50c9b7b9b0fe34d9e11d5273e4e
    PATCHES
    fix-neon2sse-config.patch
    fix-msvc-friend-access.patch
)

# Protobuf is used by subdirectories (profiling/proto, tools/benchmark).
# Injecting find_package at TFLite's top-level scope ensures that
# protobuf::libprotobuf is visible to all subdirectories regardless
# how upstream structures its find_package/add_subdirectory ordering.
file(WRITE "${CURRENT_BUILDTREES_DIR}/inject_protobuf.cmake"
     "find_package(Protobuf REQUIRED)\n")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/tensorflow/lite"
    OPTIONS
    -DTENSORFLOW_SOURCE_DIR="${SOURCE_PATH}"
    -DBUILD_SHARED_LIBS=OFF
    -DTFLITE_ENABLE_INSTALL=ON
    -DTFLITE_ENABLE_LABEL_IMAGE=OFF
    -DTFLITE_ENABLE_BENCHMARK_MODEL=OFF
    -DTFLITE_ENABLE_RUY=OFF
    -DTFLITE_ENABLE_XNNPACK=OFF
    -DTFLITE_ENABLE_EXTERNAL_DELEGATE=OFF
    -DTFLITE_ENABLE_GPU=OFF
    -DTFLITE_ENABLE_METAL=OFF
    -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON
    "-DCMAKE_PROJECT_tensorflow-lite_INCLUDE=${CURRENT_BUILDTREES_DIR}/inject_protobuf.cmake"
    # farmhash, fft2d, and ml-dtypes are source-provider ports; redirect
    # TFLite's FetchContent to the vcpkg-installed source trees so they
    # compile in-tree and are exported into tensorflow-liteTargets.
    "-DFETCHCONTENT_SOURCE_DIR_FARMHASH=${CURRENT_INSTALLED_DIR}/share/farmhash/src"
    "-DFETCHCONTENT_SOURCE_DIR_FFT2D=${CURRENT_INSTALLED_DIR}/share/fft2d/src"
    "-DFETCHCONTENT_SOURCE_DIR_ML_DTYPES=${CURRENT_INSTALLED_DIR}/share/ml-dtypes/src"
)

vcpkg_cmake_install()

# Upstream install omits headers used by Hannk delegate/logging.
file(
    INSTALL "${SOURCE_PATH}/tensorflow/lite/tools"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite"
    FILES_MATCHING
    PATTERN "*.h"
)
file(
    INSTALL "${SOURCE_PATH}/tensorflow/compiler/mlir/lite"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir"
    FILES_MATCHING
    PATTERN "*.h"
)

vcpkg_cmake_config_fixup(PACKAGE_NAME tensorflow-lite CONFIG_PATH lib/cmake/tensorflow-lite)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

file(
    REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    # The tools and mlir/lite header installs above replicate the full source
    # directory tree, including subdirectories with no headers (testdata, tests,
    # Android/iOS app scaffolding, cmake module scripts, etc.).
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/experimental/tac/g3doc"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/experimental/tac/tests"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/integrations"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/metrics/testdata"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/quantization/lite/testdata"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/quantization/tensorflow/tests"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/quantization/tests"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/quantization/tools"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/sparsity/testdata"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/stablehlo/odml_converter/tests"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/stablehlo/odml_converter/transforms"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/stablehlo/tests"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/stablehlo/transforms/mhlo_passes"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/stablehlo/transforms/torch"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/testdata"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/compiler/mlir/lite/tests"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/benchmark/android"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/benchmark/experimental"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/benchmark/ios"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/benchmark/proto"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/cmake"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/delegates/compatibility/protos"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/delegates/experimental"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/evaluation/proto"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/evaluation/stages/testdata"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/evaluation/tasks/coco_object_detection"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/evaluation/tasks/imagenet_image_classification"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/evaluation/tasks/inference_diff"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/evaluation/tasks/ios"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/evaluation/testdata"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/make"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/optimize/debugging"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/optimize/g3doc"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/optimize/python"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/optimize/sparsity"
    "${CURRENT_PACKAGES_DIR}/include/tensorflow/lite/tools/pip_package"
)
