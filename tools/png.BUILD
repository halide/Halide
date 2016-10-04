# Description:
#  Private BUILD file for libpng use inside Halide.
#  Should not be used by code outside of Halide itself.

cc_library(
    name = "png",
    srcs = [
        "png.c",
        "pngerror.c",
        "pngget.c",
        "pngmem.c",
        "pngpread.c",
        "pngread.c",
        "pngrio.c",
        "pngrtran.c",
        "pngrutil.c",
        "pngset.c",
        "pngtrans.c",
        "pngwio.c",
        "pngwrite.c",
        "pngwtran.c",
        "pngwutil.c",
    ],
    hdrs = [
        "png.h",
        "pngconf.h",
    ],
    includes = ["."],
    linkopts = ["-lm"],
    visibility = ["//visibility:public"],
    deps = ["@zlib_archive//:zlib"],
)
