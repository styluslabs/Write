#!/bin/bash
# Sample usage is as follows;
# ./signapk myapp.apk styluslabs.keystore styluslabs
#
# param1, APK file: Calculator_debug.apk
# param2, keystore location: ~/.android/debug.keystore
set -x

BUILD_TOOLS="$HOME/android-sdk/build-tools/34.0.0"

# use my debug key default
APK=$1
KEYSTORE=$2

# get the filename
APK_BASENAME=$(basename $APK)
SIGNED_APK="signed_"$APK_BASENAME

# delete META-INF folder
zip -d $APK META-INF/\*

$BUILD_TOOLS/zipalign -v -p 4 $APK $SIGNED_APK

$BUILD_TOOLS/apksigner sign --ks $KEYSTORE $SIGNED_APK

$BUILD_TOOLS/apksigner verify $SIGNED_APK
