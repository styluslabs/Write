#pragma once

#include <string>
#include <functional>
#include "resources.h"

class Painter;
class SvgGui;
class Window;
class Dialog;
struct SDL_Window;
union SDL_Event;

class Application
{
public:
  static SvgGui* gui;

  static void setupUIScale(float horzdpi = 0);
  static bool processEvents();
  static bool layoutAndDrawSW(int w, int h);
  static bool layoutAndDrawGL(int w, int h);
  static bool layoutAndDraw(int w, int h);
  static void layoutAndDraw();
  static void execWindow(Window* w);
  static int execDialog(Dialog* dialog);
  static void asyncDialog(Dialog* dialog, const std::function<void(int)>& callback = NULL);
  static void finish() { runApplication = false; }

  static void sdlEvent(SDL_Event* event);
  static void drawFrame();
  static void setSWFramebuffer(void* dest, int w, int h, int rshift, int gshift, int bshift, int ashift);

//private:
  static bool runApplication;
  static bool glRender;
  static bool isSuspended;
  static SDL_Window* sdlWindow;
  static Painter* painter;
  static std::string appDir;
};
