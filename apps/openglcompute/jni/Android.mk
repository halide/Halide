LOCAL_PATH:= $(call my-dir)

# === oglc_run ===

include $(CLEAR_VARS)

LOCAL_MODULE           := oglc_run
LOCAL_SRC_FILES        := oglc_run.cpp
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_STATIC_LIBRARIES += libOpengl
LOCAL_LDLIBS           := -lm -llog -landroid  -lEGL -lGLESv2 avg_filter.o avg_filter_arm.o
LOCAL_ARM_MODE         := arm

LOCAL_CPPFLAGS += -std=c++11 -I../support -I../../include

LOCAL_C_INCLUDES += ./

include $(BUILD_EXECUTABLE)

$(call import-module,android/native_app_glue)
