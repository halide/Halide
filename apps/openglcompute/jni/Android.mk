LOCAL_PATH:= $(call my-dir)

# === oglc_run ===

include $(CLEAR_VARS)

LOCAL_MODULE           := oglc_run
LOCAL_SRC_FILES        := oglc_run.cpp
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_STATIC_LIBRARIES += libOpengl
LOCAL_LDLIBS           := -lm -llog -landroid -lEGL -lGLESv2 avg_filter_uint32t.o avg_filter_uint32t_arm.o avg_filter_float.o avg_filter_float_arm.o
LOCAL_ARM_MODE         := arm

LOCAL_CPPFLAGS += -std=c++11 -I../support -I../../include

LOCAL_C_INCLUDES += ./

include $(BUILD_EXECUTABLE)

# === oglc library ===

include $(CLEAR_VARS)

LOCAL_MODULE           := oglc
LOCAL_SRC_FILES        := oglc_run.cpp
LOCAL_STATIC_LIBRARIES += libOpengl
LOCAL_LDLIBS           := -lm -llog -landroid -lEGL -lGLESv2 avg_filter_uint32t.o avg_filter_uint32t_arm.o avg_filter_float.o avg_filter_float_arm.o
LOCAL_ARM_MODE         := arm

LOCAL_CPPFLAGS += -std=c++11 -I../support -I../../include

LOCAL_C_INCLUDES += ./

include $(BUILD_SHARED_LIBRARY)

# === oglc_two_kernels_run ===

include $(CLEAR_VARS)

LOCAL_MODULE           := oglc_two_kernels_run
LOCAL_SRC_FILES        := oglc_two_kernels_run.cpp
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_STATIC_LIBRARIES += libOpengl
LOCAL_LDLIBS           := -lm -llog -landroid -lEGL -lGLESv2 two_kernels_filter.o
LOCAL_ARM_MODE         := arm

LOCAL_CPPFLAGS += -std=c++11 -I../support -I../../include

LOCAL_C_INCLUDES += ./

include $(BUILD_EXECUTABLE)

# === oglc_two_kernels library ===

include $(CLEAR_VARS)

LOCAL_MODULE           := oglc_two_kernels
LOCAL_SRC_FILES        := oglc_two_kernels_run.cpp
LOCAL_STATIC_LIBRARIES += libOpengl
LOCAL_LDLIBS           := -lm -llog -landroid -lEGL -lGLESv2 two_kernels_filter.o
LOCAL_ARM_MODE         := arm

LOCAL_CPPFLAGS += -std=c++11 -I../support -I../../include

LOCAL_C_INCLUDES += ./

include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
