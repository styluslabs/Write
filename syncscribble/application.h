#pragma once

#include <string>
#include <functional>
#include "resources.h"

class Painter;
class SvgGui;
class Window;
class Dialog;
struct SDL_Window;

class Application
{
public:
  static SvgGui* gui;

  static void setupUIScale(float horzdpi = 0);
  static bool processEvents();
  static void layoutAndDrawSW();
  static void layoutAndDrawGL();
  static void layoutAndDraw();
  static void execWindow(Window* w);
  static int execDialog(Dialog* dialog);
  static void asyncDialog(Dialog* dialog, const std::function<void(int)>& callback = NULL);
  static void finish() { runApplication = false; }

//private:
  static bool runApplication;
  static bool glRender;
  static bool isSuspended;
  static SDL_Window* sdlWindow;
  static Painter* painter;
  static std::string appDir;
};
