#include $(call all-subdir-makefiles)

# Note that symlinking source dirs is a terrible idea which can create a huge mess when trying to open files,
#  esp. when debugging
include /home/mwhite/styluslabs/SDL/Android.mk
include /home/mwhite/styluslabs/syncscribble/Makefile
