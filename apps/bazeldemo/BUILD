load("@halide//:halide.bzl", "halide_library")

halide_library(
  name="bazeldemo", 
  includes=["."],
  srcs=["bazeldemo_generator.cpp"]
)

cc_binary(
    name = "main",
    srcs = ["main.cpp"],
    deps = [
      ":bazeldemo",
      "@halide//:halide_buffer"
    ],
)
