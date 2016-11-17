LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := HelloAndroid
LOCAL_ARM_MODE  := arm
LOCAL_SRC_FILES := hello_wrapper.cpp
LOCAL_LDFLAGS   := -L$(LOCAL_PATH)/../jni
LOCAL_LDLIBS    := -lm -llog -landroid $(LOCAL_PATH)/../bin/$(TARGET_ARCH_ABI)/hello.a
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../include $(LOCAL_PATH)/../../../build/include $(LOCAL_PATH)/../bin/$(TARGET_ARCH_ABI)/

include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
