# generators must be added first, as the build rules in apps/
# and correctness/ depend on it
add_subdirectory(generators)

add_subdirectory(apps)
add_subdirectory(correctness)
