# Description:
#  Private BUILD file for libjpeg use inside Halide.
#  Should not be used by code outside of Halide itself.
#  Adapted directly from TensorFlow's JPEG support, with SIMD specializations
# removed to simplify build requirements.

libjpegturbo_nocopts = "-[W]error"

libjpegturbo_copts = select({
    ":android": [
        "-O2",
        "-fPIE",
        "-w",
    ],
    ":windows": [
        "-w",
        #"/Ox",
        "/wd4711",  # function 'function' selected for inline expansion
        "/wd4710",  # 'function' : function not inlined
        "-D_CRT_SECURE_NO_WARNINGS",
    ],
    "//conditions:default": [
        "-O3",
        "-w",
    ],
}) + select({
    ":armeabi-v7a": [
        "-D__ARM_NEON__",
        "-march=armv7-a",
        "-mfloat-abi=softfp",
        "-fprefetch-loop-arrays",
    ],
    "//conditions:default": [],
})

cc_library(
    name = "jpeg",
    srcs = [
        "jaricom.c",
        "jcapimin.c",
        "jcapistd.c",
        "jcarith.c",
        "jccoefct.c",
        "jccolor.c",
        "jcdctmgr.c",
        "jchuff.c",
        "jchuff.h",
        "jcinit.c",
        "jcmainct.c",
        "jcmarker.c",
        "jcmaster.c",
        "jcomapi.c",
        "jconfig.h",
        "jconfigint.h",
        "jcparam.c",
        "jcphuff.c",
        "jcprepct.c",
        "jcsample.c",
        "jctrans.c",
        "jdapimin.c",
        "jdapistd.c",
        "jdarith.c",
        "jdatadst.c",
        "jdatasrc.c",
        "jdcoefct.c",
        "jdcoefct.h",
        "jdcolor.c",
        "jdct.h",
        "jddctmgr.c",
        "jdhuff.c",
        "jdhuff.h",
        "jdinput.c",
        "jdmainct.c",
        "jdmainct.h",
        "jdmarker.c",
        "jdmaster.c",
        "jdmaster.h",
        "jdmerge.c",
        "jdphuff.c",
        "jdpostct.c",
        "jdsample.c",
        "jdsample.h",
        "jdtrans.c",
        "jerror.c",
        "jfdctflt.c",
        "jfdctfst.c",
        "jfdctint.c",
        "jidctflt.c",
        "jidctfst.c",
        "jidctint.c",
        "jidctred.c",
        "jinclude.h",
        "jmemmgr.c",
        "jmemnobs.c",
        "jmemsys.h",
        "jpeg_nbits_table.h",
        "jpegcomp.h",
        "jquant1.c",
        "jquant2.c",
        "jutils.c",
        "jversion.h",
    ],
    hdrs = [
        "jccolext.c",  # should have been named .inc
        "jdcol565.c",  # should have been named .inc
        "jdcolext.c",  # should have been named .inc
        "jdmrg565.c",  # should have been named .inc
        "jdmrgext.c",  # should have been named .inc
        "jerror.h",
        "jmorecfg.h",
        "jpegint.h",
        "jpeglib.h",
        "jstdhuff.c",  # should have been named .inc
    ],
    copts = libjpegturbo_copts,
    nocopts = libjpegturbo_nocopts,
    visibility = ["//visibility:public"],
    deps = [":simd_none"],
)

cc_library(
    name = "simd_none",
    srcs = [
        "jchuff.h",
        "jconfig.h",
        "jdct.h",
        "jerror.h",
        "jinclude.h",
        "jmorecfg.h",
        "jpegint.h",
        "jpeglib.h",
        "jsimd.h",
        "jsimd_none.c",
        "jsimddct.h",
    ],
    copts = libjpegturbo_copts,
    nocopts = libjpegturbo_nocopts,
)

genrule(
    name = "configure",
    outs = ["jconfig.h"],
    cmd = "cat <<'EOF' >$@\n" +
          "#define JPEG_LIB_VERSION 62\n" +
          "#define LIBJPEG_TURBO_VERSION 1.5.1\n" +
          "#define LIBJPEG_TURBO_VERSION_NUMBER 1005001\n" +
          "#define C_ARITH_CODING_SUPPORTED 1\n" +
          "#define D_ARITH_CODING_SUPPORTED 1\n" +
          "#define BITS_IN_JSAMPLE 8\n" +
          "#define HAVE_LOCALE_H 1\n" +
          "#define HAVE_STDDEF_H 1\n" +
          "#define HAVE_STDLIB_H 1\n" +
          "#define HAVE_UNSIGNED_CHAR 1\n" +
          "#define HAVE_UNSIGNED_SHORT 1\n" +
          "#define MEM_SRCDST_SUPPORTED 1\n" +
          "#define NEED_SYS_TYPES_H 1\n" +
          select({
              ":k8": "#define WITH_SIMD 1\n",
              ":armeabi-v7a": "#define WITH_SIMD 1\n",
              ":arm64-v8a": "#define WITH_SIMD 1\n",
              "//conditions:default": "",
          }) +
          "EOF",
)

genrule(
    name = "configure_internal",
    outs = ["jconfigint.h"],
    cmd = "cat <<'EOF' >$@\n" +
          "#define BUILD \"20161115\"\n" +
          "#ifdef _MSC_VER  /* Windows */\n" +
          "#define INLINE __inline\n" +
          "#else\n" +
          "#define INLINE inline __attribute__((always_inline))\n" +
          "#endif\n" +
          "#define PACKAGE_NAME \"libjpeg-turbo\"\n" +
          "#define VERSION \"1.5.1\"\n" +
          "#if (__WORDSIZE==64 && !defined(__native_client__)) || defined(_WIN64)\n" +
          "#define SIZEOF_SIZE_T 8\n" +
          "#else\n" +
          "#define SIZEOF_SIZE_T 4\n" +
          "#endif\n" +
          "EOF",
)

# jiminy cricket the way this file is generated is completely outrageous
genrule(
    name = "configure_simd",
    outs = ["simd/jsimdcfg.inc"],
    cmd = "cat <<'EOF' >$@\n" +
          "%define DCTSIZE 8\n" +
          "%define DCTSIZE2 64\n" +
          "%define RGB_RED 0\n" +
          "%define RGB_GREEN 1\n" +
          "%define RGB_BLUE 2\n" +
          "%define RGB_PIXELSIZE 3\n" +
          "%define EXT_RGB_RED 0\n" +
          "%define EXT_RGB_GREEN 1\n" +
          "%define EXT_RGB_BLUE 2\n" +
          "%define EXT_RGB_PIXELSIZE 3\n" +
          "%define EXT_RGBX_RED 0\n" +
          "%define EXT_RGBX_GREEN 1\n" +
          "%define EXT_RGBX_BLUE 2\n" +
          "%define EXT_RGBX_PIXELSIZE 4\n" +
          "%define EXT_BGR_RED 2\n" +
          "%define EXT_BGR_GREEN 1\n" +
          "%define EXT_BGR_BLUE 0\n" +
          "%define EXT_BGR_PIXELSIZE 3\n" +
          "%define EXT_BGRX_RED 2\n" +
          "%define EXT_BGRX_GREEN 1\n" +
          "%define EXT_BGRX_BLUE 0\n" +
          "%define EXT_BGRX_PIXELSIZE 4\n" +
          "%define EXT_XBGR_RED 3\n" +
          "%define EXT_XBGR_GREEN 2\n" +
          "%define EXT_XBGR_BLUE 1\n" +
          "%define EXT_XBGR_PIXELSIZE 4\n" +
          "%define EXT_XRGB_RED 1\n" +
          "%define EXT_XRGB_GREEN 2\n" +
          "%define EXT_XRGB_BLUE 3\n" +
          "%define EXT_XRGB_PIXELSIZE 4\n" +
          "%define RGBX_FILLER_0XFF 1\n" +
          "%define JSAMPLE byte ; unsigned char\n" +
          "%define SIZEOF_JSAMPLE SIZEOF_BYTE ; sizeof(JSAMPLE)\n" +
          "%define CENTERJSAMPLE 128\n" +
          "%define JCOEF word ; short\n" +
          "%define SIZEOF_JCOEF SIZEOF_WORD ; sizeof(JCOEF)\n" +
          "%define JDIMENSION dword ; unsigned int\n" +
          "%define SIZEOF_JDIMENSION SIZEOF_DWORD ; sizeof(JDIMENSION)\n" +
          "%define JSAMPROW POINTER ; JSAMPLE * (jpeglib.h)\n" +
          "%define JSAMPARRAY POINTER ; JSAMPROW * (jpeglib.h)\n" +
          "%define JSAMPIMAGE POINTER ; JSAMPARRAY * (jpeglib.h)\n" +
          "%define JCOEFPTR POINTER ; JCOEF * (jpeglib.h)\n" +
          "%define SIZEOF_JSAMPROW SIZEOF_POINTER ; sizeof(JSAMPROW)\n" +
          "%define SIZEOF_JSAMPARRAY SIZEOF_POINTER ; sizeof(JSAMPARRAY)\n" +
          "%define SIZEOF_JSAMPIMAGE SIZEOF_POINTER ; sizeof(JSAMPIMAGE)\n" +
          "%define SIZEOF_JCOEFPTR SIZEOF_POINTER ; sizeof(JCOEFPTR)\n" +
          "%define DCTELEM word ; short\n" +
          "%define SIZEOF_DCTELEM SIZEOF_WORD ; sizeof(DCTELEM)\n" +
          "%define float FP32 ; float\n" +
          "%define SIZEOF_FAST_FLOAT SIZEOF_FP32 ; sizeof(float)\n" +
          "%define ISLOW_MULT_TYPE word ; must be short\n" +
          "%define SIZEOF_ISLOW_MULT_TYPE SIZEOF_WORD ; sizeof(ISLOW_MULT_TYPE)\n" +
          "%define IFAST_MULT_TYPE word ; must be short\n" +
          "%define SIZEOF_IFAST_MULT_TYPE SIZEOF_WORD ; sizeof(IFAST_MULT_TYPE)\n" +
          "%define IFAST_SCALE_BITS 2 ; fractional bits in scale factors\n" +
          "%define FLOAT_MULT_TYPE FP32 ; must be float\n" +
          "%define SIZEOF_FLOAT_MULT_TYPE SIZEOF_FP32 ; sizeof(FLOAT_MULT_TYPE)\n" +
          "%define JSIMD_NONE 0x00\n" +
          "%define JSIMD_MMX 0x01\n" +
          "%define JSIMD_3DNOW 0x02\n" +
          "%define JSIMD_SSE 0x04\n" +
          "%define JSIMD_SSE2 0x08\n" +
          "EOF",
)

config_setting(
    name = "k8",
    values = {"cpu": "k8"},
)

config_setting(
    name = "android",
    values = {"crosstool_top": "//external:android/crosstool"},
)

config_setting(
    name = "armeabi-v7a",
    values = {"android_cpu": "armeabi-v7a"},
)

config_setting(
    name = "arm64-v8a",
    values = {"android_cpu": "arm64-v8a"},
)

config_setting(
    name = "windows",
    values = {"cpu": "x64_windows_msvc"},
)
