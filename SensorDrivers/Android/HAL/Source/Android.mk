LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := sensors.default

LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

# include any shared library dependencies
LOCAL_SHARED_LIBRARIES := liblog libcutils libdl

LOCAL_SRC_FILES := \
			sensors.cpp \
		 	SensorBase.cpp \
			ProximitySensor.cpp \
			InputEventReader.cpp \
			LightSensor.cpp

include $(BUILD_SHARED_LIBRARY)
