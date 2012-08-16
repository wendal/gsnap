LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := png
LOCAL_SRC_FILES := libpng.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := jpeg
LOCAL_SRC_FILES := libjpeg.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE:= gsnap
LOCAL_SRC_FILES := gsnap.c
LOCAL_LDLIBS := -lz
LOCAL_STATIC_LIBRARIES += jpeg png
include $(BUILD_EXECUTABLE)





