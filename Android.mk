ifeq ($(call my-dir),$(call project-path-for,qcom-camera))

ifneq ($(strip $(USE_DEVICE_SPECIFIC_CAMERA)),true)
# TODO:  Find a better way to separate build configs for ADP vs non-ADP devices
ifneq ($(TARGET_BOARD_AUTO),true)
  ifneq ($(strip $(USE_CAMERA_STUB)),true)
    ifneq ($(BUILD_TINY_ANDROID),true)
      ifneq ($(filter msm8996,$(TARGET_BOARD_PLATFORM)),)
        include $(call all-subdir-makefiles)
      endif
      ifneq ($(filter msmcobalt,$(TARGET_BOARD_PLATFORM)),)
        include $(call all-makefiles-under,$(call my-dir)/msmcobalt)
      endif
    endif
  endif
endif
endif

endif
