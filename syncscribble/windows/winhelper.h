#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ugui/svggui.h"

void initTouchInput(SDL_Window* sdlwin, bool useWintab);
//bool touchInputFilter(SDL_Event* event);
void setDPIAware();
Image getClipboardImage();
