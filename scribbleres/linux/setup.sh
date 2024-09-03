#!/bin/sh
set -e
SCRIPTPATH="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

if ! $SCRIPTPATH/Write --exit >/dev/null 2>&1 ; then
  echo "Installing SDL2 library - this may prompt for password to install package"
  command -v yum >/dev/null 2>&1 && sudo yum install SDL2
  command -v pacman >/dev/null 2>&1 && sudo pacman -S sdl2
  command -v apt-get >/dev/null 2>&1 && (dpkg -s libsdl2-2.0-0 >/dev/null 2>&1 || sudo apt-get install libsdl2-2.0-0)

  if ! $SCRIPTPATH/Write --exit; then
    echo "\nERROR: Unable to setup Write - please make sure SDL2 (libsdl2) is installed and that your graphics driver supports OpenGL 3.3 or later"
    exit 1
  fi
fi

if ! command -v desktop-file-edit >/dev/null 2>&1 ; then
  echo "\nIf you would like to create a desktop entry for Write, please install the desktop-file-utils package and run this script again"
else
  DESKTOPFILE=$SCRIPTPATH/Write.desktop
  desktop-file-edit --set-key=Exec --set-value=$SCRIPTPATH/Write $DESKTOPFILE
  desktop-file-edit --set-key=Icon --set-value=$SCRIPTPATH/Write144x144.png $DESKTOPFILE
  #sudo desktop-file-install $DESKTOPFILE  # to install for all users
  desktop-file-install --dir=$HOME/.local/share/applications $DESKTOPFILE
  echo "\nCreated desktop entry for Write"
fi

echo "*** Setup succeeded ***"
