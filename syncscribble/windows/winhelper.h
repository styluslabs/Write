#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ulib/image.h"

Image getClipboardImage();
// Wintab (Wacom) pen input, for tablets w/o Windows Ink support; subclasses platform layer's window
void initWintab(HWND hwnd);
void shutdownWintab();
