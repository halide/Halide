# Note that there are 3 distinct tags for autoscheduler tests:
#
# autoschedulers_cpu: this autoscheduler is expected to work for CPU targets
# autoschedulers_gpu: this autoscheduler is expected to work for GPU targets
# autoschedulers_cuda: this autoscheduler is expected to work on Cuda targets (but probably not other GPU targets)

add_subdirectory(adams2019)
add_subdirectory(anderson2021)
add_subdirectory(li2018)
add_subdirectory(mullapudi2016)
add_subdirectory(sioutas2020)
