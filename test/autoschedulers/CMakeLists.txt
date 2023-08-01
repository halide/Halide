# Note that there are 3 distinct tags for autoscheduler tests:
#
# autoscheduler_cpu: this autoscheduler is expected to work for CPU targets
# autoscheduler_gpu: this autoscheduler is expected to work for GPU targets
# autoscheduler_cuda: this autoscheduler is expected to work on Cuda targets (but probably not other GPU targets)
#
# There is also a legacy tag, `auto_schedule`, which is present for legacy purposes and will be removed soon.

add_subdirectory(adams2019)
if (TARGET_NVPTX)
  add_subdirectory(anderson2021)
else()
  message(STATUS "Not testing anderson2021 because PTX is not enabled for this build of Halide")
endif()
add_subdirectory(li2018)
add_subdirectory(mullapudi2016)
