#!/bin/bash
# Build Halide generators for HelloiOS
# This script is called from the Xcode build phase

set -e  # Exit on error

echo "Building Halide generators for HelloiOS"
echo "SRCROOT: ${SRCROOT}"
echo "DERIVED_FILE_DIR: ${DERIVED_FILE_DIR}"
echo "EFFECTIVE_PLATFORM_NAME: ${EFFECTIVE_PLATFORM_NAME}"
echo "ARCHS: ${ARCHS}"

# Detect Halide installation (use absolute path)
if [ -d "${SRCROOT}/../../install" ]; then
  HALIDE_PREFIX="$(cd "${SRCROOT}/../../install" && pwd)"
  HALIDE_LIB_DIR="${HALIDE_PREFIX}/lib"
  HALIDE_INCLUDE_DIR="${HALIDE_PREFIX}/include"
elif [ -d "${SRCROOT}/../../build" ]; then
  HALIDE_PREFIX="$(cd "${SRCROOT}/../../build" && pwd)"
  HALIDE_LIB_DIR="${HALIDE_PREFIX}/lib"
  HALIDE_INCLUDE_DIR="${HALIDE_PREFIX}/include"
elif [ -d "${SRCROOT}/../../bin" ]; then
  # Legacy make-based build
  HALIDE_PREFIX="$(cd "${SRCROOT}/../.." && pwd)"
  HALIDE_LIB_DIR="${HALIDE_PREFIX}/bin"
  HALIDE_INCLUDE_DIR="${HALIDE_PREFIX}/include"
else
  echo "ERROR: Could not find Halide library. Please build Halide first."
  echo "Expected one of:"
  echo "  ../../install/lib/libHalide.dylib (CMake install tree)"
  echo "  ../../build/lib/libHalide.dylib (CMake build tree)"
  echo "  ../../bin/libHalide.dylib (Make-based build)"
  exit 1
fi

echo "Using Halide from: ${HALIDE_PREFIX}"

# Verify library exists
if [ ! -f "${HALIDE_LIB_DIR}/libHalide.dylib" ]; then
  echo "ERROR: libHalide.dylib not found at ${HALIDE_LIB_DIR}"
  exit 1
fi

cd "${SRCROOT}/HelloiOS"

# Build generator executable for host (macOS)
# Use xcrun to get the right compiler and explicitly target macOS
echo "Compiling generator for host (macOS)..."
# Get macOS SDK path and use explicit target triple to avoid iOS environment pollution
MACOSX_SDK="$(xcrun -sdk macosx --show-sdk-path)"
xcrun -sdk macosx clang++ \
  -target arm64-apple-macosx11.0 \
  -isysroot "${MACOSX_SDK}" \
  reaction_diffusion_2_generator.cpp \
  "${SRCROOT}/../../tools/GenGen.cpp" \
  -std=c++17 \
  -fno-rtti \
  -I "${HALIDE_INCLUDE_DIR}" \
  -I "${SRCROOT}/../../tools" \
  -L "${HALIDE_LIB_DIR}" \
  -lHalide \
  -o "${DERIVED_FILE_DIR}/reaction_diffusion_2_generator"

echo "Generator built successfully"

# Detect target for Halide code generation
if [[ "${EFFECTIVE_PLATFORM_NAME}" == "-iphonesimulator" ]]; then
  # Simulator build
  # For multi-arch builds, generate for each architecture
  IFS=' ' read -ra ARCH_ARRAY <<< "${ARCHS}"

  if [ ${#ARCH_ARRAY[@]} -eq 1 ]; then
    # Single architecture
    ARCH="${ARCH_ARRAY[0]}"
    if [[ "${ARCH}" == "x86_64" ]]; then
      TARGET="x86-64-ios-simulator"
    else
      TARGET="arm-64-ios-simulator"
    fi
    echo "Generating libraries for simulator (${ARCH}): target=${TARGET}"

    # Generate all 6 libraries
    for GEN in init update render; do
      echo "  Generating ${GEN} (CPU)..."
      (cd "${DERIVED_FILE_DIR}"; \
       DYLD_LIBRARY_PATH="${HALIDE_LIB_DIR}" \
       ./reaction_diffusion_2_generator \
         -g reaction_diffusion_2_${GEN} \
         -f reaction_diffusion_2_${GEN} \
         -n reaction_diffusion_2_${GEN} \
         -o . \
         target=${TARGET}-user_context)
    done

    for GEN in init update render; do
      echo "  Generating ${GEN} (Metal)..."
      (cd "${DERIVED_FILE_DIR}"; \
       DYLD_LIBRARY_PATH="${HALIDE_LIB_DIR}" \
       ./reaction_diffusion_2_generator \
         -g reaction_diffusion_2_${GEN} \
         -f reaction_diffusion_2_metal_${GEN} \
         -n reaction_diffusion_2_metal_${GEN} \
         -o . \
         target=${TARGET}-metal-user_context)
    done
  else
    # Multi-architecture build - generate for each arch and use lipo
    echo "Multi-architecture build (${ARCHS})"

    for ARCH in "${ARCH_ARRAY[@]}"; do
      if [[ "${ARCH}" == "x86_64" ]]; then
        TARGET="x86-64-ios-simulator"
      else
        TARGET="arm-64-ios-simulator"
      fi

      echo "  Generating libraries for ${ARCH} (target=${TARGET})..."
      ARCH_DIR="${DERIVED_FILE_DIR}/${ARCH}"
      mkdir -p "${ARCH_DIR}"

      # Generate all 6 libraries for this architecture
      for GEN in init update render; do
        (cd "${ARCH_DIR}"; \
         DYLD_LIBRARY_PATH="${HALIDE_LIB_DIR}" \
         "${DERIVED_FILE_DIR}/reaction_diffusion_2_generator" \
           -g reaction_diffusion_2_${GEN} \
           -f reaction_diffusion_2_${GEN} \
           -n reaction_diffusion_2_${GEN} \
           -o . \
           target=${TARGET}-user_context)
      done

      for GEN in init update render; do
        (cd "${ARCH_DIR}"; \
         DYLD_LIBRARY_PATH="${HALIDE_LIB_DIR}" \
         "${DERIVED_FILE_DIR}/reaction_diffusion_2_generator" \
           -g reaction_diffusion_2_${GEN} \
           -f reaction_diffusion_2_metal_${GEN} \
           -n reaction_diffusion_2_metal_${GEN} \
           -o . \
           target=${TARGET}-metal-user_context)
      done
    done

    # Combine architectures using lipo
    echo "  Creating universal binaries with lipo..."
    for LIB in reaction_diffusion_2_init reaction_diffusion_2_update reaction_diffusion_2_render \
               reaction_diffusion_2_metal_init reaction_diffusion_2_metal_update reaction_diffusion_2_metal_render; do
      LIPO_INPUTS=""
      for ARCH in "${ARCH_ARRAY[@]}"; do
        LIPO_INPUTS="${LIPO_INPUTS} ${DERIVED_FILE_DIR}/${ARCH}/${LIB}.a"
      done
      xcrun lipo -create ${LIPO_INPUTS} -output "${DERIVED_FILE_DIR}/${LIB}.a"
    done

    # Copy headers from first architecture
    FIRST_ARCH="${ARCH_ARRAY[0]}"
    cp "${DERIVED_FILE_DIR}/${FIRST_ARCH}"/*.h "${DERIVED_FILE_DIR}/"
  fi
else
  # Device build - always arm64-ios
  TARGET="arm-64-ios"
  echo "Generating libraries for device: target=${TARGET}"

  for GEN in init update render; do
    echo "  Generating ${GEN} (CPU)..."
    (cd "${DERIVED_FILE_DIR}"; \
     DYLD_LIBRARY_PATH="${HALIDE_LIB_DIR}" \
     ./reaction_diffusion_2_generator \
       -g reaction_diffusion_2_${GEN} \
       -f reaction_diffusion_2_${GEN} \
       -n reaction_diffusion_2_${GEN} \
       -o . \
       target=${TARGET}-user_context)
  done

  for GEN in init update render; do
    echo "  Generating ${GEN} (Metal)..."
    (cd "${DERIVED_FILE_DIR}"; \
     DYLD_LIBRARY_PATH="${HALIDE_LIB_DIR}" \
     ./reaction_diffusion_2_generator \
       -g reaction_diffusion_2_${GEN} \
       -f reaction_diffusion_2_metal_${GEN} \
       -n reaction_diffusion_2_metal_${GEN} \
       -o . \
       target=${TARGET}-metal-user_context)
  done
fi

echo "✅ Successfully generated 6 Halide libraries (3 CPU + 3 Metal)"
ls -lh "${DERIVED_FILE_DIR}"/*.a | awk '{print "   " $9 " (" $5 ")"}'
