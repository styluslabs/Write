# cross-platform C/C++ makefile for Write
# - platform-specific, project-independent stuff moved to Makefile.msvc, .ios, .unix
# - generates files in ./Release (default) or ./Debug (with DEBUG=1 passed to make)
# - while it is tempting to try to move generic code to a shared file, the complete make file
#   is part of a project and should be included with it
# useful ref: https://www.gnu.org/software/make/manual/html_node/Quick-Reference.html

TARGET = Write
# build application.cpp first since that contains impl. for single header libraries
SOURCES = \
  application.cpp \
  resources.cpp \
  basics.cpp \
  strokebuilder.cpp \
  element.cpp \
  page.cpp \
  syncundo.cpp \
  selection.cpp \
  document.cpp \
  scribblemode.cpp \
  scribbleinput.cpp \
  scribbleview.cpp \
  bookmarkview.cpp \
  clippingview.cpp \
  scribblearea.cpp \
  scribbledoc.cpp \
  scribblewidget.cpp \
  scribbleconfig.cpp \
  scribblesync.cpp \
  documentlist.cpp \
  rulingdialog.cpp \
  configdialog.cpp \
  linkdialog.cpp \
  pentoolbar.cpp \
  syncdialog.cpp \
  touchwidgets.cpp \
  mainwindow.cpp \
  scribbleapp.cpp

SOURCES += \
  ../ugui/svggui.cpp \
  ../ugui/widgets.cpp \
  ../ugui/textedit.cpp \
  ../ugui/colorwidgets.cpp

SOURCES += \
  ../ulib/geom.cpp \
  ../ulib/image.cpp \
  ../ulib/path2d.cpp \
  ../ulib/painter.cpp \
  ../usvg/svgnode.cpp \
  ../usvg/svgstyleparser.cpp \
  ../usvg/svgparser.cpp \
  ../usvg/svgpainter.cpp \
  ../usvg/svgwriter.cpp \
  ../usvg/pdfwriter.cpp \
  ../usvg/cssparser.cpp \
  ../nanovgXC/src/nanovg.c \
  ../pugixml/src/pugixml.cpp

# image I/O; stb_image and stb_image_write are included inline in uvg/image.cpp
SOURCES += \
  ../miniz/miniz.c \
  ../miniz/miniz_tdef.c \
  ../miniz/miniz_tinfl.c


# if source contains ../ paths, this should be the <current> directory; if ../../, <parent>/<current>; etc.
# - this ensures object files remain under build directory
TOPDIR = syncscribble

# -I and -isystem include paths
INC = . .. ../nanovgXC/src ../nanovgXC/glad
INCSYS = ../pugixml/src ../stb

MAJORVER := 3
MINORVER := 0
#HGREV := $(shell hg id -n)
GITREV := $(shell git rev-parse --short HEAD)
GITCOUNT := $(shell git rev-list --count HEAD)
APPVERSION := $(MAJORVER).$(MINORVER).$(GITCOUNT)
# defines for pugixml config instead of forking whole repo; use external STB libs in nanovg
DEFS = PUGIXML_NO_XPATH PUGIXML_NO_EXCEPTIONS
DEFS += SCRIBBLE_REV_NUMBER="$(GITREV)"


ifneq ($(windir),)
# Windows

SOURCES += \
  windows/winhelper.cpp \
  windows/wintab/Utils.c \
  ../nanovgXC/glad/glad.c

RESOURCES = windows/resources.rc
INCSYS += ../SDL/include
DEFS += SCRIBBLE_TEST_PATH="\"$(CURDIR)/../scribbletest\""
DEFS += _USE_MATH_DEFINES UNICODE NOMINMAX FONS_WPATH

# only dependencies under this path will be tracked in .d files; note [\\] must be used for "\"
# ensure that no paths containing spaces are included
DEPENDBASE ?= c:[\\]temp[\\]styluslabs

# shell32 for ShellExecute; user32 for clipboard fns; libs below opengl32.lib needed only for static SDL
LIBS = \
  ws2_32.lib \
  shell32.lib \
  user32.lib \
  glu32.lib \
  opengl32.lib \
  gdi32.lib \
  winmm.lib \
  ole32.lib \
  oleaut32.lib \
  advapi32.lib \
  setupapi.lib \
  imm32.lib \
  version.lib

SDL_LIB ?= ../SDL/Release/SDL2.lib
LIBS += $(SDL_LIB)

FORCECPP = ../layout/layout.obj

# distribution package
ZIPFILE = write$(GITCOUNT).zip
ZIPDIR = Write
DISTRES = \
  ../scribbleres/fonts/Roboto-Regular.ttf \
  ../scribbleres/fonts/DroidSansFallback.ttf \
  ../scribbleres/Intro.svg
# installer
WXS = windows/InstallWrite.wxs

include Makefile.msvc

else ifneq ($(MACOS),)
# pass MACOS=1 to make to force macOS build (instead of default of iOS on mac)

APPDIR = Write.app
SOURCES += macos/macoshelper.m ../nanovgXC/glad/glad.c
INCSYS += ../SDL/include
DEFS += SCRIBBLE_TEST_PATH='"$(CURDIR)/../scribbletest"'
LIBS = ../SDL/MacRelease/libSDL2.a -liconv
FRAMEWORKS = CoreServices CoreGraphics IOKit Metal OpenGL AppKit CoreVideo Carbon
#AudioToolbox CoreAudio ForceFeedback
# copied to root of app bundle
INFOPLIST = macos/Info.plist
# copied to Resources/ in app bundle
DISTRES = \
  ../scribbleres/macos/write.icns \
  ../scribbleres/fonts/SanFranciscoDisplay-Regular.otf \
  ../scribbleres/fonts/DroidSansFallback.ttf \
  ../scribbleres/Intro.svg
# copied to root of dmg
DMGRES = ../scribbleres/macos/Readme.txt

include Makefile.mac

else ifneq ($(XPC_FLAGS),)
# iOS (XPC_FLAGS seems to be defined on macOS)

SOURCES += ios/ioshelper.m
DEFS += GLES_SILENCE_DEPRECATION
XIB = ios/LaunchView.xib
# app store now requires app icon be in an asset catalog
XCASSETS = ../scribbleres/Assets.xcassets
IOSRES = \
  ../scribbleres/fonts/SanFranciscoDisplay-Regular.otf \
  ../scribbleres/fonts/DroidSansFallback.ttf \
  ../scribbleres/Intro.svg
INFOPLIST = ios/Info.plist
PLISTENV = CURRENT_PROJECT_VERSION=1.5 MARKETING_VERSION=1.5

# Xcode generated provisioning profile for free account is per-app and expires every 7 days; w/ paid account, 1-year profile w/ all apps (com.styluslabs.*) can be generated
# For signing, we expect a valid .xcent file - the value for com.apple.developer.team-identifier can be found under the "Organizational Unit" field in "Get Info" for the iPhone Developer certificate in the Keychain Access app
# run `security find-identity -v -p codesigning` to get argument for codesign --sign

DIST ?= 0
ifeq ($(DIST), 0)
  APPDIR = Write.app
  PROVISIONING_PROFILE = /Users/mwhite/Documents/mwhite_iOS_Dev_2024.mobileprovision
  XCENT = ios/Dev.app.xcent
  CODESIGN = /usr/bin/codesign --force --sign 9E6635D070FC516CD66467812DDFA3CFDD010E3D --timestamp=none
else
  ifneq ($(DEBUG), 0)
    $(error DIST build requires DEBUG=0)
  endif
  APPDIR = Payload/Write.app
  ifeq ("$(DIST)", "appstore")
    PROVISIONING_PROFILE = /Users/mwhite/Documents/Stylus_Labs_App_Store_2024.mobileprovision
    XCENT = ios/AppStore.app.xcent
  else
    PROVISIONING_PROFILE = /Users/mwhite/Documents/Stylus_Labs_AdHoc_2020.mobileprovision
    XCENT = ios/AdHoc.app.xcent
  endif
  CODESIGN = /usr/bin/codesign --force --sign 55648202533A2234B7ED12254A2C271BC52ACED187E0 --timestamp=none
endif

INCSYS += /Users/mwhite/styluslabs/SDL/include
FRAMEWORKS = AVFoundation GameController CoreMotion Foundation UIKit CoreGraphics OpenGLES QuartzCore CoreAudio AudioToolbox Metal StoreKit

SIM ?= 0
ifneq ($(SIM), 0)
  LIBS = ../SDL/SimDebug/libSDL2.a
else
  LIBS = ../SDL/Release/libSDL2.a
endif

IAP ?= 0
ifneq ($(IAP), 0)
  DEFS += SCRIBBLE_IAP
  PLISTENV += PRODUCT_BUNDLE_IDENTIFIER=com.styluslabs.write3a
	XCENT = ios/IAPDev.app.xcent
else
  PLISTENV += PRODUCT_BUNDLE_IDENTIFIER=com.styluslabs.write3
endif

include Makefile.ios

else ifneq ($(BUILD_SHARED_LIBRARY),)
# Android
# Uses SDL 2.0.9 (branch write-android)
# ./start_gradle assembleRelease && cp app/build/outputs/apk/release/app-release.apk . && ./resignapk.sh app-release.apk ~/styluslabs.keystore && mv signed_app-release.apk write300.apk

# working dir is android/app apparently
ifneq ($(shell git -C ../../../SDL rev-parse --abbrev-ref HEAD),write-android)
  $(error Not on write-android branch!)
endif

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

SOURCES += android/androidhelper.cpp

# SDLActivity is hardcoded to load "libmain.so"
LOCAL_MODULE := main

ALL_INC := $(INC) $(INCSYS) ../SDL/include
LOCAL_C_INCLUDES := $(addprefix $(LOCAL_PATH)/, $(ALL_INC))

LOCAL_CFLAGS := $(addprefix -D, $(DEFS))
LOCAL_CPPFLAGS := -std=c++14 -Wno-unused -Wno-error=format-security

LOCAL_SRC_FILES := $(addprefix $(LOCAL_PATH)/, $(SOURCES))
LOCAL_SHARED_LIBRARIES := SDL2
# libandroid needed for ANativeWindow_* fns
LOCAL_LDLIBS := -lGLESv3 -llog -ljnigraphics -landroid

include $(BUILD_SHARED_LIBRARY)

else ifneq ($(EMSDK),)
# Emscripten

SOURCES += wasm/wasmhelper.c
INCSYS += ../SDL/include
#INCFILES = linux/force_glibc.h
DEFS += SCRIBBLE_TEST_PATH='"$(CURDIR)/../scribbletest"'
DEBUG ?= 0
LIBS = ../SDL/build/build/.libs/libSDL2.a  ../SDL/build/build/.libs/libSDL2main.a

include Makefile.wasm

else

SOURCES += linux/linuxtablet.c ../nanovgXC/glad/glad.c
DEFS += SCRIBBLE_TEST_PATH='"$(CURDIR)/../scribbletest"'
DEBUG ?= 0
USE_SYSTEM_SDL ?= 0
ifneq ($(USE_SYSTEM_SDL),0)
  # Linux - pass `USE_SYSTEM_SDL=1` to use the system SDL2 (via pkg-config Makefile.unix)
  PKGS += sdl2
else
  # Linux - uses SDL 2.0.10 (branch write-linux); see scribbleres/SDL-Makefile.unix
  INCSYS += ../SDL/Release/include
  #INCFILES = ../scribbleres/force_link_glibc_2.14.h  -- doesn't work anymore, build in chroot instead
  ifneq ($(DEBUG), 0)
    LIBS = ../SDL/Debug/libSDL2d.a
  else
    # use $(BUILDDIR) to support Release and LinuxRel
    LIBS = ../SDL/$(BUILDDIR)/libSDL2.a
  endif
endif

ifneq ($(DEBUG), 0)
  # use asan by default for debug build
  SANITIZE = 1
endif

# must use this instead of just -lpthread so the defines needed by force_glibc.h are created
CFLAGS = -pthread
# X11 and Xi needed for linuxtablet.c
LIBS += -pthread -ldl -lX11 -lXi

# distribution package
TGZ = write$(GITCOUNT).tar.gz
DISTRES = \
  ../scribbleres/fonts/Roboto-Regular.ttf \
  ../scribbleres/fonts/DroidSansFallback.ttf \
  ../scribbleres/Intro.svg \
  ../scribbleres/linux/Write.desktop \
  ../scribbleres/linux/Write144x144.png \
  ../scribbleres/linux/setup.sh \
  ../scribbleres/linux/INSTALL

include Makefile.unix

# chroot build enviroment for Linux releases (.symver trick is no longer viable):

# - jessie g++ doesn't support c++14, use next release (stretch) - see https://wiki.debian.org/DebianReleases
# chroot setup:
# sudo debootstrap --variant=buildd bullseye build-chroot http://deb.debian.org/debian/
# sudo chroot build-chroot/ /bin/bash
# apt-get install --no-install-recommends git mercurial libx11-dev libxcb1-dev libudev-dev libdbus-1-dev libibus-1.0-dev libxcursor-dev libxinerama-dev libxi-dev libxrandr-dev libxss-dev libxxf86vm-dev libgl1-mesa-dev libegl1-mesa-dev
# mkdir -p /home/mwhite/styluslabs
# apt-get clean
# rm -r /usr/share/locale/*
# rm -r /usr/share/doc/*

# building (TODO: create make rule for this!):
# cd ~
# sudo mount --bind styluslabs build-chroot/home/mwhite/styluslabs/
# sudo chroot --userspec=mwhite:mwhite /home/mwhite/build-chroot/ /bin/bash -c "cd /home/mwhite/styluslabs/syncscribble && make DEBUG=0 BUILDDIR=LinuxRel tgz"
# and: cd /home/mwhite/styluslabs/SDL && make -f ../scribbleres/SDL-Makefile.unix DEBUG=0 BUILDDIR=LinuxRel
# sudo umount build-chroot/home/mwhite/styluslabs

endif
