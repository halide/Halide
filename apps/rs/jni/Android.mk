LOCAL_PATH:= $(call my-dir)

# === rstest ===

include $(CLEAR_VARS)

LOCAL_MODULE           := rstest
LOCAL_SRC_FILES        := rstest.cpp
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_LDLIBS           := -lm -llog -landroid generated_blur_rs.o generated_blur_vectorized_rs.o generated_blur_arm.o generated_blur_vectorized_arm.o generated_copy_rs.o generated_copy_vectorized_rs.o generated_copy_arm.o generated_copy_vectorized_arm.o
LOCAL_ARM_MODE         := arm

LOCAL_CPPFLAGS += -std=c++11

LOCAL_C_INCLUDES := ./

include $(BUILD_EXECUTABLE)

$(call import-module,android/native_app_glue)
