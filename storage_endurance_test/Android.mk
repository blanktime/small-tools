LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_LDLIBS += -llog
LOCAL_CFLAGS += -pie -fPIE
LOCAL_LDFLAGS += -pie -fPIE
LOCAL_MODULE := storage_endurance_test
LOCAL_C_INCLUDES += ./
LOCAL_SRC_FILES += ./storage_endurance_test.c
include $(BUILD_EXECUTABLE)
include $(CLEAR_VARS)
