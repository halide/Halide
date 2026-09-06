# This instructs vcpkg to do nothing, which causes find_package
# to search the system for LLVM, rather than the vcpkg trees.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
