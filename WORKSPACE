workspace(name = "halide")

load("//:halide_workspace.bzl", "halide_workspace")
halide_workspace()

# TODO: this is a workaround for https://github.com/bazelbuild/bazel/issues/1248
local_repository(
    name = "halide",
    path = __workspace_dir__,
)
