cmake_minimum_required(VERSION 3.19)

include("shared-Release/CPackConfig.cmake")

## General setup

set(CPACK_PACKAGE_CONTACT "Alex Reinking <alex_reinking@berkeley.edu>")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/pre_build.cmake")

##############################
## Components configuration ##
##############################

# This is a mapping from CPack component names to CMake install() components.
# We use the identity mapping here for simplicity; some advanced configurations
# with GUI installers require these to diverge.
set(CPACK_COMPONENTS_HALIDE_RUNTIME Halide_Runtime)
set(CPACK_COMPONENTS_HALIDE_DEVELOPMENT Halide_Development)
set(CPACK_COMPONENTS_HALIDE_DOCUMENTATION Halide_Documentation)

set(CPACK_COMPONENTS_ALL Halide_Runtime Halide_Development Halide_Documentation)

set(CPACK_INSTALL_CMAKE_PROJECTS
    static-Release Halide ALL /
    shared-Release Halide ALL /)

###################################
## Ubuntu-specific configuration ##
###################################

# We set every variable documented here: https://cmake.org/cmake/help/latest/cpack_gen/deb.html
# even if it's just to the default. That way there are no surprises.

set(CPACK_DEB_COMPONENT_INSTALL YES)

set(CPACK_DEBIAN_HALIDE_RUNTIME_PACKAGE_NAME libHalide${CPACK_PACKAGE_VERSION_MAJOR})
set(CPACK_DEBIAN_HALIDE_DEVELOPMENT_PACKAGE_NAME libHalide${CPACK_PACKAGE_VERSION_MAJOR}-dev)
set(CPACK_DEBIAN_HALIDE_DOCUMENTATION_PACKAGE_NAME libHalide${CPACK_PACKAGE_VERSION_MAJOR}-doc)

set(CPACK_DEBIAN_HALIDE_RUNTIME_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_HALIDE_DEVELOPMENT_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_HALIDE_DOCUMENTATION_FILE_NAME DEB-DEFAULT)

# Debian package versions look like: <epoch>:<version>-<release>
# <epoch>   is a number that increases when changing the whole versioning schema.
#           We would ideally _never_ have to set this since we're using semver.
# <version> is the version number of the actual software being packaged.
# <release> is the version number of the _package_. Set/increment this when fixing
#           bugs in the package itself. This should also not be incremented too
#           frequently. It's always safe to bump the patch version when in doubt.
unset(CPACK_DEBIAN_PACKAGE_EPOCH)
set(CPACK_DEBIAN_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION}")
unset(CPACK_DEBIAN_PACKAGE_RELEASE)

# The default here is the host system architecture. It will generally be best
# to package for ARM on ARM, for x86 on x86, etc. The documentation gets the
# pseudo-architecture "all" to indicate that it has no binaries (ie. is arch
# independent).
unset(CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
set(CPACK_DEBIAN_HALIDE_DOCUMENTATION_PACKAGE_ARCHITECTURE all)

# Package dependencies.
# TODO: figure out how to get LLVM major version piped in here.
set(CPACK_DEBIAN_HALIDE_RUNTIME_PACKAGE_DEPENDS "llvm-12 (>= 12.0.0)")
set(CPACK_DEBIAN_HALIDE_DEVELOPMENT_PACKAGE_DEPENDS "llvm-12-dev (>= 12.0.0), liblld-12-dev (>= 12.0.0)")
set(CPACK_DEBIAN_HALIDE_DOCUMENTATION_PACKAGE_DEPENDS "")

# Sets up package dependencies based on CPack component dependencies
set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON)

# Uses CPACK_PACKAGE_CONTACT as default
unset(CPACK_DEBIAN_PACKAGE_MAINTAINER)

# These inherit their values from cpack cpack_add_component
unset(CPACK_DEBIAN_HALIDE_RUNTIME_DESCRIPTION)
unset(CPACK_DEBIAN_HALIDE_DEVELOPMENT_DESCRIPTION)
unset(CPACK_DEBIAN_HALIDE_DOCUMENTATION_DESCRIPTION)

# The Debian repository package section.
# See: https://packages.debian.org/unstable/
# libs     = Libraries to make other programs work. They provide special features to developers.
# libdevel = Libraries necessary for developers to write programs that use them.
# doc      = FAQs, HOWTOs and other documents trying to explain everything related to
#            Debian, and software needed to browse documentation (man, info, etc).
set(CPACK_DEBIAN_HALIDE_RUNTIME_PACKAGE_SECTION libs)
set(CPACK_DEBIAN_HALIDE_DEVELOPMENT_PACKAGE_SECTION libdevel)
set(CPACK_DEBIAN_HALIDE_DOCUMENTATION_PACKAGE_SECTION doc)

# Deprecated: do not use
unset(CPACK_DEBIAN_ARCHIVE_TYPE)

# Could also choose from lzma, xz, or bzip2 if one gave a better ratio.
set(CPACK_DEBIAN_COMPRESSION_TYPE "gzip")

# Optional just means that it is optional for the safe running of
# a Debian system to have our package installed. The other categories
# do not apply to us: required (won't boot without), important (core
# system utils), and standard (basic niceties for a character-mode
# system).
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# Uses CMAKE_PROJECT_HOMEPAGE_URL as default.
unset(CPACK_DEBIAN_PACKAGE_HOMEPAGE)

# Call dpkg-shlibdeps to get dependencies on system libraries.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
unset(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS)  # CMake 3.20+ only

# Disable debug messaging
unset(CPACK_DEBIAN_PACKAGE_DEBUG)

# Special variables for package constraints. We don't have any yet.
unset(CPACK_DEBIAN_PACKAGE_PREDEPENDS)
unset(CPACK_DEBIAN_PACKAGE_ENHANCES)
unset(CPACK_DEBIAN_PACKAGE_BREAKS)
unset(CPACK_DEBIAN_PACKAGE_CONFLICTS)
unset(CPACK_DEBIAN_PACKAGE_PROVIDES)
unset(CPACK_DEBIAN_PACKAGE_REPLACES)
unset(CPACK_DEBIAN_PACKAGE_RECOMMENDS)
unset(CPACK_DEBIAN_PACKAGE_SUGGESTS)

# Generate debian/shlibs control file; require exact versions.
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS YES)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS_POLICY "=")

# Add custom scripts to package. Used to ensure ldconfig runs.
unset(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA)
set(CPACK_DEBIAN_HALIDE_RUNTIME_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_LIST_DIR}/triggers")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

# Name the source package for this one. TODO?
unset(CPACK_DEBIAN_PACKAGE_SOURCE)

# Name the package containing debug symbols for this one. TODO?
unset(CPACK_DEBIAN_DEBUGINFO_PACKAGE)
