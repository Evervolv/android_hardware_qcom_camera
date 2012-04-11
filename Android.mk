LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ifneq (,$(findstring $(TARGET_BOARD_PLATFORM),qsd8k msm7k))

LOCAL_MODULE_TAGS      := optional
LOCAL_MODULE_PATH      := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE           := camera.$(TARGET_BOARD_PLATFORM)

LOCAL_SRC_FILES        := QcomCamera.cpp

LOCAL_SHARED_LIBRARIES := liblog libutils libcamera_client libbinder \
                          libcutils libhardware libcamera

LOCAL_C_INCLUDES       := frameworks/base/services \
                          frameworks/base/include \
                          hardware/libhardware/include \
                          hardware/qcom/display/libgralloc

LOCAL_PRELINK_MODULE   := false

include $(BUILD_SHARED_LIBRARY)

endif
