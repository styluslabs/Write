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
MINORVER := 1
#HGREV := $(shell hg id -n)
GITREV := $(shell git rev-parse --short HEAD || wsl git rev-parse --short HEAD)
GITCOUNT := $(shell git rev-list --count HEAD || wsl git rev-list --count HEAD)
APPVERSION := $(MAJORVER).$(MINORVER).$(GITCOUNT)
# defines for pugixml config instead of forking whole repo; use external STB libs in nanovg
DEFS = PUGIXML_NO_XPATH PUGIXML_NO_EXCEPTIONS
DEFS += SCRIBBLE_REV_NUMBER="$(GITREV)"


ifeq ($(OS),Windows_NT)
# Windows

# platform layer: ugui/platforms/winmain.cpp; Wintab (Wacom helper code) in windows/
SOURCES += \
  windows/winhelper.cpp \
  windows/wintab/Utils.c \
  ../ugui/platforms/winmain.cpp \
  ../nanovgXC/glad/glad.c

RESOURCES = windows/resources.rc
DEFS += SVGGUI_NO_SDL SCRIBBLE_TEST_PATH="\"$(CURDIR)/../scribbletest\""
DEFS += _USE_MATH_DEFINES UNICODE NOMINMAX FONS_WPATH

# only dependencies under this path will be tracked in .d files; note [\\] must be used for "\"
# ensure that no paths containing spaces are included
FWDSLASH := /
BACKSLASH_ESC := [\\]
DEPENDBASE ?= $(subst $(FWDSLASH),$(BACKSLASH_ESC),$(abspath ..))

# ws2_32 for sockets; shell32 for ShellExecute, CommandLineToArgvW, drag and drop; imm32 for IME
LIBS = \
  ws2_32.lib \
  shell32.lib \
  user32.lib \
  gdi32.lib \
  opengl32.lib \
  imm32.lib

FORCECPP = ../layout/layout.obj

# distribution package
MSIFILE = write-$(GITREV).msi
ZIPFILE = write-$(GITREV).zip
ZIPDIR = Write
DISTRES = \
  ..\scribbleres\fonts\Roboto-Regular.ttf \
  ..\scribbleres\fonts\DroidSansFallback.ttf \
  ..\scribbleres\Intro.svg
# installer
WXS = windows\InstallWrite.wxs

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

# platform layer: ugui/platforms/iosApp.mm (app's main() and UIApplication delegate)
SOURCES += ios/ioshelper.m ../ugui/platforms/iosApp.mm
DEFS += SVGGUI_NO_SDL GLES_SILENCE_DEPRECATION
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

FRAMEWORKS = Foundation UIKit CoreGraphics OpenGLES QuartzCore StoreKit

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
# ./start_gradle assembleRelease && cp app/build/outputs/apk/release/app-release.apk . && ./resignapk.sh app-release.apk ~/styluslabs.keystore && mv signed_app-release.apk write300.apk

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# platform layer: ugui/platforms/androidApp.cpp + ugui/platforms/android/ (Java)
SOURCES += android/androidhelper.cpp ../ugui/platforms/androidApp.cpp
DEFS += SVGGUI_NO_SDL

# loaded by com.styluslabs.writeqt.MainActivity
LOCAL_MODULE := main

ALL_INC := $(INC) $(INCSYS)
LOCAL_C_INCLUDES := $(addprefix $(LOCAL_PATH)/, $(ALL_INC))

LOCAL_CFLAGS := $(addprefix -D, $(DEFS))
LOCAL_CPPFLAGS := -std=c++14 -Wno-unused -Wno-error=format-security

LOCAL_SRC_FILES := $(addprefix $(LOCAL_PATH)/, $(SOURCES))
# libandroid needed for ANativeWindow_* fns
LOCAL_LDLIBS := -lGLESv3 -lEGL -llog -ljnigraphics -landroid

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

SOURCES += ../nanovgXC/glad/glad.c
# platform layer (provides main()) - only linked into Write, not WriteTest
MAIN_SOURCES = ../ugui/platforms/linuxmain.cpp
# `make test` builds and runs WriteTest, which runs tests using a headless platform layer
TEST_TARGET = WriteTest
TEST_SOURCES = \
  ../scribbletest/scribbletest.cpp \
  ../scribbletest/imagecompare.cpp \
  ../scribbletest/headless.cpp \
  ../scribbletest/testmain.cpp
DEFS += SVGGUI_NO_SDL SCRIBBLE_TEST_PATH='"$(CURDIR)/../scribbletest"'
DEBUG ?= 0
#~ USE_SYSTEM_SDL ?= 0
#~ ifneq ($(USE_SYSTEM_SDL),0)
#~   # Linux - pass `USE_SYSTEM_SDL=1` to use the system SDL2 (via pkg-config Makefile.unix)
#~   PKGS += sdl2
#~ else
#~   # Linux - uses SDL 2.0.10 (branch write-linux); see scribbleres/SDL-Makefile.unix
#~   INCSYS += ../SDL/Release/include
#~   #INCFILES = ../scribbleres/force_link_glibc_2.14.h  -- doesn't work anymore, build in chroot instead
#~   ifneq ($(DEBUG), 0)
#~     LIBS = ../SDL/Debug/libSDL2d.a
#~   else
#~     # use $(BUILDDIR) to support Release and LinuxRel
#~     LIBS = ../SDL/$(BUILDDIR)/libSDL2.a
#~   endif
#~ endif

# must use this instead of just -lpthread so the defines needed by force_glibc.h are created
CFLAGS = -pthread
# X11 and Xi needed for linuxtablet.c
LIBS += -pthread -ldl -lX11 -lXi -lXext -lXcursor -lXrandr -lOpenGL -lGLX

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

endif
