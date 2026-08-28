tests(
    GROUPS json autoschedulers_cpu autoschedulers_gpu
    SOURCES test.cpp
    ARGS $<TARGET_FILE:Halide::JsonSchedule>
)
add_dependencies(json_test Halide::JsonSchedule)
