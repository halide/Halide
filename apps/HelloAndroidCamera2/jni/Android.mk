LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := HelloAndroidCamera2
LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES := \
    AndroidBufferUtilities.cpp \
    HalideFilters.cpp \
    LockedSurface.cpp \
    YuvBufferT.cpp
LOCAL_LDFLAGS := -L$(LOCAL_PATH)/../jni
LOCAL_LDLIBS := -lm -llog -landroid -latomic
LOCAL_LDLIBS += $(LOCAL_PATH)/../bin/$(TARGET_ARCH_ABI)/deinterleave.a
LOCAL_LDLIBS += $(LOCAL_PATH)/../bin/$(TARGET_ARCH_ABI)/edge_detect.a
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../include $(LOCAL_PATH)/../../../build/include $(LOCAL_PATH)/../bin/$(TARGET_ARCH_ABI)/

include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
