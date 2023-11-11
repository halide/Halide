# Note that there are 3 distinct tags for autoscheduler tests:
#
# autoschedulers_cpu: this autoscheduler is expected to work for CPU targets
# autoschedulers_gpu: this autoscheduler is expected to work for GPU targets
# autoschedulers_cuda: this autoscheduler is expected to work on Cuda targets (but probably not other GPU targets)

add_subdirectory(adams2019)
if (TARGET_NVPTX)
  add_subdirectory(anderson2021)
else()
  message(STATUS "Not testing anderson2021 because PTX is not enabled for this build of Halide")
endif()
add_subdirectory(li2018)
add_subdirectory(mullapudi2016)
