LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := android_halide_gl_native
LOCAL_ARM_MODE  := arm
LOCAL_SRC_FILES := android_halide_gl_native.cpp
LOCAL_LDFLAGS   := -Ljni
LOCAL_LDLIBS    := -lm -llog -landroid -lEGL -lGLESv2 jni/halide_gl_filter.o
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../include

include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
