#include $(call all-subdir-makefiles)

# Note that symlinking source dirs is a terrible idea which can create a huge mess when trying to open files,
#  esp. when debugging
include $(call my-dir)/../../../../../Makefile
