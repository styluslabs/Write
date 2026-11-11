#pragma once

#include <string>
#include <functional>
#include "resources.h"

class Painter;
struct Rect;
class SvgGui;
class Window;
class Dialog;
struct SDL_Window;
struct SDL_Rect;
union SDL_Event;

// appMain() = appInit() + event loop + appShutdown(); WriteTest calls appInit() and appShutdown() directly
bool appInit(int argc, char* argv[]);
void appShutdown();

class Application
{
public:
  static SvgGui* gui;

  static void setupUIScale(float horzdpi = 0);
  // process pending events (blocking until at least one); for nested event loops
  static bool processEvents();
  static bool layoutAndDraw(int w, int h);
  // layout and draw window, then present to screen if anything changed
  static void layoutAndDraw();
  static void execWindow(Window* w);
  static int execDialog(Dialog* dialog);
  static void asyncDialog(Dialog* dialog, const std::function<void(int)>& callback = NULL);
  static void finish() { runApplication = false; }
  // debug hook called after each frame is drawn, before present (see scribbletest/redrawcheck.cpp); for GL,
  //  the frame has been blitted to the window framebuffer, which is bound
  static void (*frameHook)(const Rect& dirty);
  // NVG image flags (e.g. NVG_IMAGE_SRGB) of offscreen framebuffer used for GL rendering
  static int glFramebufferFlags();

  static void sdlEvent(SDL_Event* event);

//private:
  static bool layoutAndDrawSW(int w, int h);
  static bool layoutAndDrawGL(int w, int h);

  static bool runApplication;
  static bool glRender;
  static bool isSuspended;
  static SDL_Window* sdlWindow;
  static Painter* painter;
  static std::string appDir;
};
