load("@halide//:halide.bzl", "halide_library")

halide_library(
  name="bazeldemo",
  srcs=["bazeldemo_generator.cpp"],
  extra_outputs=["stmt", "schedule"],
)

cc_binary(
    name = "main",
    srcs = ["main.cpp"],
    deps = [
      ":bazeldemo",
      "@halide//:halide_buffer"
    ],
)
