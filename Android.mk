ifeq ($(call my-dir),$(call project-path-for,qcom-camera))

ifneq ($(strip $(USE_DEVICE_SPECIFIC_CAMERA)),true)
# TODO:  Find a better way to separate build configs for ADP vs non-ADP devices
ifneq ($(TARGET_BOARD_AUTO),true)
  ifneq ($(filter msm8996,$(TARGET_BOARD_PLATFORM)),)
    ifneq ($(strip $(USE_CAMERA_STUB)),true)
      ifneq ($(BUILD_TINY_ANDROID),true)
        include $(call all-subdir-makefiles)
      endif
    endif
  endif
endif
endif

endif
