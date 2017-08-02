def cc_failure_test(name, srcs, deps, copts, linkopts):
  native.cc_binary(
    name = "error_%s" % name,
    srcs = srcs,
    deps = deps,
    copts = copts,
    linkopts = linkopts,
    visibility = ["//visibility:private"]
  )

  native.sh_test(
    name = name,
    srcs = ["//test/common:expect_failure"],
    args = ["$(location :error_%s)" % name],
    data = [":error_%s" % name],
  )

