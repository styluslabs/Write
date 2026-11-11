#include "application.h"
#include "usvg/svgpainter.h"
#include "usvg/svgwriter.h"
#include "usvg/svgparser.h"
#include "ugui/svggui.h"
#include "scribbleapp.h"
#include "scribbleview.h"
#include "scribbleconfig.h"

#if PLATFORM_WIN
#include "windows/winhelper.h"
#elif PLATFORM_OSX
#include "macos/macoshelper.h"
#elif PLATFORM_EMSCRIPTEN
#include "wasm/wasmhelper.h"
#endif

#if PLATFORM_IOS
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#elif PLATFORM_ANDROID
// to test Thinkpad Android tablet (API level 15), change to GLES2 and comment out nanovg_gl stuff
#include <GLES3/gl31.h>
#include <GLES3/gl3ext.h>
#elif PLATFORM_OSX
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#elif PLATFORM_EMSCRIPTEN
#include <GLES3/gl3.h>
#include <GLES3/gl2ext.h>
#define GL_FRAMEBUFFER_SRGB GL_FRAMEBUFFER_SRGB_EXT

#elif PLATFORM_LINUX

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#else
// GLAD generated files are in nanovg-2 repo; link to generate is:
// https://glad.dav1d.de/#profile=core&language=c&specification=gl&loader=on&api=gl%3D3.3&extensions=GL_ARB_shader_image_load_store&extensions=GL_EXT_shader_framebuffer_fetch
#include "glad.h"
#endif

// SW renderer used on Mac since GL extensions not avail; GL blitter used because SDL surface doesn't support
//  high-DPI scaling on OSX (it just uses the window size)
// SW renderer used on Android if GLES 3 not available; previously used GL blitter due to problems on old
//  Samsung tablets w/ ANativeWindow_unlockAndPost, but now platform layer provides SW framebuffer
// SW renderer only used on iOS when forced in config file; platform layer provides SW framebuffer
#define USE_GL_BLITTER (PLATFORM_OSX)

// possible regression on iOS 18.4, so safer to stick w/ framebuffer fetch for now
#define USE_NANOVG_VTEX (!PLATFORM_IOS)
// note these are not marked as build dependencies (in application.d) since nanovg-2 is `isystem`
// Only basic GLES3, at best, so use SW renderer
#if PLATFORM_EMSCRIPTEN
#define NANOVG_GLU_IMPLEMENTATION  // no nanovg_gl - utils only
#elif PLATFORM_MOBILE
#define NANOVG_GLES3_IMPLEMENTATION
#else
#define NANOVG_GL3_IMPLEMENTATION
#endif
#define NVG_LOG PLATFORM_LOG
#if USE_NANOVG_VTEX
#include "nanovg_vtex.h"
#else
#include "nanovg_gl.h"
#endif
#include "nanovg_gl_utils.h"

#define NANOVG_SW_IMPLEMENTATION
#define NVGSW_QUIET_FRAME
#include "nanovg_sw.h"

#if PLATFORM_ANDROID || PLATFORM_EMSCRIPTEN
#define NVGSWU_GLES2
#elif PLATFORM_IOS
#define NVGSWU_GLES3
#else
#define NVGSWU_GL3
#endif
#include "nanovg_sw_utils.h"

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MINIZ_GZ_IMPLEMENTATION
#include "ulib/miniz_gzip.h"

#define PLATFORMUTIL_IMPLEMENTATION
#include "ulib/platformutil.h"

#define STRINGUTIL_IMPLEMENTATION
#include "ulib/stringutil.h"

#define FILEUTIL_IMPLEMENTATION
#include "ulib/fileutil.h"

#define UNET_IMPLEMENTATION
#include "ulib/unet.h"

// tracing
#define UTRACE_ENABLE
#define UTRACE_IMPLEMENTATION
#include "ulib/utrace.h"

#define SVGGUI_UTIL_IMPLEMENTATION
#include "ugui/svggui_util.h"  // sdlEventLog for debugging


SvgGui* Application::gui = NULL;

bool Application::runApplication = true;
bool Application::glRender = false;
bool Application::isSuspended = false;
SDL_Window* Application::sdlWindow = NULL;
Painter* Application::painter = NULL;
std::string Application::appDir;

static int nvglFBFlags = 0;
static NVGLUframebuffer* nvglFB = NULL;

// thread pool for nanovgXC SW renderer
static ThreadPool* swThreadPool = NULL;
static std::vector< std::future<void> > swFutures;

static void poolSubmit(void (*fn)(void*), void* arg)
{
  swFutures.push_back(swThreadPool->enqueue(fn, arg));
}

static void poolWait()
{
  for(auto& future : swFutures)
    future.wait();
  swFutures.clear();
}

#if PLATFORM_EMSCRIPTEN
#include "emscripten.h"

static void wasmMainLoop()
{
  if(Application::processEvents())
    Application::layoutAndDraw();
  if(!Application::runApplication)
    emscripten_cancel_main_loop();
}
#endif

static const char* getLocale()
{
#if PLATFORM_IOS
  return iosGetLocale();
#else
  // Windows: GetUserDefaultLocaleName
  // Android: Locale.getDefault().getLanguage() + .getCountry() (or .toString())
  return "";
#endif
}


// Windows DPI notes: GetDpiForMonitor(MDT_RAW_DPI) returns error (not supported) in Windows 8.1 VM
// - GetDpiForMonitor(MDT_EFFECTIVE_DPI) is what SDL uses (returns 120; actual is 210 and 125% scaling)
// - GetDeviceCaps(hdc, HORZSIZE or VERTSIZE) very unreliable; suggested approach is to read EDID with SetupAPI
void Application::setupUIScale(float horzdpi)
{
  float pxRatio = 1.0f;
  int winWidth, winHeight;
  SDL_GetWindowSize(sdlWindow, &winWidth, &winHeight);
  int fbWidth = winWidth, fbHeight = winHeight;

#if PLATFORM_IOS
  // platform layer reports window size in pixels; DPI is 163*(pixels per point), where pixels per point is
  //  UIScreen.nativeScale, so can be non-integer (e.g. 2.6 for iPhone Plus); default UI scale is same as w/ SDL
  //  (which reported window size in points): 150 dpi per point
  if(horzdpi <= 0 || horzdpi >= 700) {
    float sysdpi = 0;
    SDL_GetDisplayDPI(0, NULL, &sysdpi, NULL);
    horzdpi = sysdpi > 0 ? sysdpi*150/163 : 300;
  }
#elif PLATFORM_EMSCRIPTEN
  pxRatio = emscripten_get_device_pixel_ratio();
  horzdpi = horzdpi > 0 && horzdpi < 700 ? horzdpi : pxRatio * 150;
#else
  if(fbWidth != winWidth) {
    pxRatio = float(fbWidth)/float(winWidth);
  }
  if(horzdpi <= 0) {
#if PLATFORM_DESKTOP
    // DPI reported by Windows is 96*<scaling factor> and 72, 96, and 120 are common garbage values, so estimate
    //  from display size instead
    SDL_Rect r = {0, 0, 1920, 1080};
    SDL_GetDisplayBounds(SDL_GetWindowDisplayIndex(sdlWindow), &r);
    horzdpi = pxRatio*std::max(r.h, r.w)/11.2f;  // 12.3in diag (Surface Pro) => 10.2in width; 14 in diag (X1 yoga) => 12.2in width
#else
    SDL_GetDisplayDPI(0, NULL, &horzdpi, NULL);  // just Android for now
#endif
  }
#endif
  // 150 - 160 dpi is pretty standard ref
  gui->paintScale = std::max(90, int(horzdpi + 0.5f))/150.0;
  gui->inputScale = pxRatio/gui->paintScale;
  ScribbleView::unitsPerPx = 1/gui->paintScale;
  SvgLength::defaultDpi = 150;

  painter->setAtlasTextThreshold(24 * gui->paintScale);  // 24px font is default for dialog titles
}


// Optionally wait for GPU to finish previous frame's nanovg draws before uploading data (vertices, uniforms,
//  edge texture) for next frame.  This should not be necessary - GL guarantees implicit sync for glBufferData,
//  glTexImage3D, etc. - but VMware SVGA3D driver (Mesa 26.1) corrupts rendering of rapidly updated small frames
//  (in-progress stroke, hover cursor) without it.  Measured cost w/ SVGA3D was ~zero: wait just moves from
//  swap buffers to the fence.  Enabled by glFrameFence pref: 0 = auto (SVGA3D only), 1 = always, -1 = never.
//  (Alternative fix of cycling buffers in nanovg_vtex.h kept in nanovgXC git stash)
static bool useFrameFence = false;
static GLsync prevFrameFence = 0;

static void waitPrevFrameFence()
{
  if(!prevFrameFence) return;
  glClientWaitSync(prevFrameFence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);  // 1 sec timeout
  glDeleteSync(prevFrameFence);
  prevFrameFence = 0;
}

static void setPrevFrameFence()
{
  if(!useFrameFence) return;
  if(prevFrameFence) glDeleteSync(prevFrameFence);
  prevFrameFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

// bounds calculator for SvgDocument; must outlive all documents, so created in appInit() and deleted in appShutdown()
static Painter* boundsPainter = NULL;
static SvgPainter* boundsCalc = NULL;

// appMain() is split into appInit() and appShutdown() so that WriteTest can run tests in place of the event loop
bool appInit(int argc, char* argv[])
{
  Application::runApplication = true;
#if PLATFORM_WIN
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
#endif
  unet_init();
  if(argc > 0)
    Application::appDir = canonicalPath(FSPath(argv[0]).parentPath());
  else
    Application::appDir = canonicalPath("./");

  ScribbleApp* scribbleApp = new ScribbleApp(argc, argv);
  ScribbleApp::app = scribbleApp;
#if PLATFORM_ANDROID
  if(ScribbleApp::cfg->Int("glRender") < 2 && AndroidHelper::doAction(A_GPU_BLACKLIST))  // try to catch some bad GPUs before even trying GL
    ScribbleApp::cfg->set("glRender", 0);
#endif

  bool sRGB = ScribbleApp::cfg->Bool("sRGB");
  nvglFBFlags = sRGB ? NVG_IMAGE_SRGB : 0;

  auto winGeom = parseNumbersList(ScribbleApp::cfg->String("windowState", ""), 6);
  if(winGeom.size() < 6) { winGeom = { 0, 0, 0, 0, 0, 0 }; }
  WindowPos winrect =
      {int(winGeom[0]), int(winGeom[1]), int(winGeom[2]), int(winGeom[3]), int(winGeom[4]), int(winGeom[5])};
  bool wantGL = ScribbleApp::cfg->Int("glRender") && !PLATFORM_OSX && !PLATFORM_EMSCRIPTEN;
  PlatformConfig platformCfg = {"Write", "Write", winrect,
      (wantGL ? PlatformConfig::USE_GL : 0) | (sRGB ? PlatformConfig::SRGB : 0), 0};
  Application::sdlWindow = platformInit(platformCfg);
  if(!Application::sdlWindow) {
    platformMessageBox("Error", "Unable to create window.");
    return false;
  }
#if PLATFORM_WIN
  // platform layer creates GL context, but we must load GL fns
  if((platformCfg.flags & PlatformConfig::USE_GL) && !gladLoadGL()) {
    PLATFORM_LOG("gladLoadGL() failed - falling back to software rendering\n");
    platformCfg.flags &= ~PlatformConfig::USE_GL;  // platform blits SW framebuffer once requested
  }
  if(ScribbleApp::cfg->Bool("useWintab"))
    initWintab((HWND)platformGetNativeWindow());
#endif
  if(!(platformCfg.flags & PlatformConfig::USE_GL))
    ScribbleApp::cfg->set("glRender", 0);
  // platform clears SRGB flag if sRGB framebuffer not available (only relevant for GLES)
  if(!(platformCfg.flags & PlatformConfig::SRGB)) {
    sRGB = false;
    nvglFBFlags = 0;
  }

  if(ScribbleApp::cfg->Bool("perfTrace")) { TRACE_INIT(); }
  Painter::initFontStash(FONS_DELAY_LOAD | FONS_SUMMED);
  fonsInternalParams(Painter::fontStash)->notDefCodePt = 0xFE56;

  int painterFlags = (ScribbleApp::cfg->Int("glRender") ? Painter::PAINT_GL : Painter::PAINT_SW) |
      Painter::SW_FALLBACK | Painter::CACHE_IMAGES | (sRGB ? Painter::SRGB_AWARE : 0) |
      (SCRIBBLE_DEBUG ? Painter::PAINT_DEBUG_GL : 0) | (USE_GL_BLITTER ? Painter::SW_BLIT_GL : 0);
  Painter* painter = new Painter(painterFlags ^ ScribbleApp::cfg->Int("nvgGlFlags"));  // for testing/debugging
  Application::painter = painter;
  Application::glRender = painter->usesGPU();
  if(Application::glRender) {
    nvglFB = nvgluCreateFramebuffer(painter->vg, 0, 0, NVGLU_NO_NVG_IMAGE | nvglFBFlags);
    if(sRGB)
      nvgluSetFramebufferSRGB(1);  // no-op for GLES - sRGB enabled iff FB is sRGB
    // see waitPrevFrameFence()
    int fenceCfg = ScribbleApp::cfg->Int("glFrameFence");
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    useFrameFence = fenceCfg > 0 || (fenceCfg == 0 && glRenderer && strstr(glRenderer, "SVGA3D"));
    if(useFrameFence)
      PLATFORM_LOG("Waiting for previous frame before rendering (glFrameFence = %d, GL renderer: %s)\n",
          fenceCfg, glRenderer ? glRenderer : "");
  }
  else {
    int ncores = std::thread::hardware_concurrency();
    int nthreads = (ncores > 0 ? ncores : 4);  // hyperthreading incl on Windows
    if(nthreads > 1) {
      swThreadPool = new ThreadPool(nthreads);
      nvgswSetThreading(painter->vg, nthreads/2, 2, poolSubmit, poolWait);
    }
  }

  boundsPainter = new Painter(Painter::PAINT_NULL);
  boundsCalc = new SvgPainter(boundsPainter);
  SvgDocument::sharedBoundsCalc = boundsCalc;

  setupResources();  // load fonts, icons, etc.
  const char* cfgLocale = ScribbleApp::cfg->String("locale", "");
  scribbleApp->hasI18n = setupI18n(cfgLocale[0] ? cfgLocale : getLocale());
  SvgGui* svgGui = new SvgGui();
  Application::gui = svgGui;
  svgGui->setWindowStylesheet(std::unique_ptr<SvgCssStylesheet>(createStylesheet()));

  platformSetEventHandler(Application::sdlEvent);

  // maybe increase dpi by 1.5x if screen larger than 15 inches?
  Dim dpi = ScribbleApp::cfg->Int("screenDPI");
  Application::setupUIScale(dpi >= 10 && dpi <= 1200 ? dpi : 0);

  scribbleApp->init();  // this previously could enter event loop, but not anymore
  return true;
}

void appShutdown()
{
  ScribbleApp* scribbleApp = ScribbleApp::app;
  SvgGui* svgGui = Application::gui;
  Painter* painter = Application::painter;

  // save window state
  auto winpos = platformGetWindowPos();
  ScribbleApp::cfg->set("windowState", fstring("%d %d %d %d %d %d",
      winpos.x, winpos.y, winpos.w, winpos.h, winpos.maximized, winpos.display).c_str());

  delete scribbleApp;
  ScribbleApp::app = NULL;
  delete svgGui;
  Application::gui = NULL;
  delete painter;
  Application::painter = NULL;
  SvgDocument::sharedBoundsCalc = NULL;
  delete boundsCalc;
  boundsCalc = NULL;
  delete boundsPainter;
  boundsPainter = NULL;
  if(nvglFB)
    nvgluDeleteFramebuffer(nvglFB);
  if(swThreadPool)
    delete swThreadPool;
  platformSetEventHandler(NULL);
#if PLATFORM_WIN
  shutdownWintab();
#endif
  platformShutdown();
  unet_terminate();
}

int appMain(int argc, char* argv[])
{
  if(!appInit(argc, argv))
    return -1;
#if PLATFORM_EMSCRIPTEN
  emscripten_set_main_loop(wasmMainLoop, 0, 1);  // fps = 0 (use default), infinite loop = 1 (never returns)
#else
  while(Application::processEvents()) {
    Application::layoutAndDraw();
  }
#endif
  appShutdown();
  return 0;
}

// debug hook called after each frame is drawn (before present) - see scribbletest/redrawcheck.cpp
void (*Application::frameHook)(const Rect& dirty) = NULL;
static Rect lastDirty;

int Application::glFramebufferFlags() { return nvglFBFlags; }

bool Application::processEvents()
{
  return platformProcessEvents(true) && runApplication;
}

void Application::layoutAndDraw()
{
  if(isSuspended) return;
  int w = 0, h = 0;
  if(glRender) {
    SDL_GetWindowSize(sdlWindow, &w, &h);
    if(w <= 0 || h <= 0) return;  // no surface (e.g. Android app in background)
  }
  else {
    static PlatformFramebuffer prevFB = {};
    PlatformFramebuffer fb = platformGetSWFramebuffer();
    if(!fb.pixels) return;
    if(fb.pixels != prevFB.pixels || fb.width != prevFB.width || fb.height != prevFB.height) {
      nvgswSetFramebuffer(painter->vg, fb.pixels, fb.width, fb.height, fb.rshift, fb.gshift, fb.bshift, fb.ashift);
      prevFB = fb;
    }
    w = fb.width;  h = fb.height;
  }
  if(layoutAndDraw(w, h)) {
    if(frameHook)
      frameHook(lastDirty);
    platformPresent();
  }
}

bool Application::layoutAndDraw(int w, int h)
{
  return glRender ? layoutAndDrawGL(w, h) : layoutAndDrawSW(w, h);
}

static Rect tracedGuiLayoutAndDraw(int w, int h)
{
  if(Application::isSuspended) return Rect();  // trying to use GL when suspended crashes app on iOS
  TRACE_BEGIN(t00);
  Application::painter->deviceRect = Rect::wh(w, h);
  Rect dirty = Application::gui->layoutAndDraw(Application::painter);
  int dirtyw = dirty.isValid() ? int(dirty.width()) : 0;
  int dirtyh = dirty.isValid() ? int(dirty.height()) : 0;
  TRACE_END(t00, fstring("SvgGui::layoutAndDraw; %d*%d = %d pixels dirty", dirtyw, dirtyh, dirtyw*dirtyh).c_str());
  return dirty;
}

bool Application::layoutAndDrawSW(int w, int h)
{
  Rect dirty = tracedGuiLayoutAndDraw(w, h);
  lastDirty = dirty;
  if(!dirty.isValid())
    return false;
  TRACE(painter->endFrame());
  TRACE_FLUSH();
  return true;
}

bool Application::layoutAndDrawGL(int fbWidth, int fbHeight)
{
  Rect oldDeviceRect = painter->deviceRect;
  Rect dirty = tracedGuiLayoutAndDraw(fbWidth, fbHeight);
  lastDirty = dirty;
  if(!dirty.isValid())
    return false;
  // for now, we scissor here to limit fill rate; we may add support directly to nanovg-2 later (but we would
  //  still need to set glScissor here to use glClear); we assume dirty has already been rounded to ints

  if(dirty != painter->deviceRect)
    nvgluSetScissor(int(dirty.left), fbHeight - int(dirty.bottom), int(dirty.width()), int(dirty.height()));

  waitPrevFrameFence();  // endFrame() uploads vertex, uniform, edge data
  if(nvglFB) {
    // drawing to screen on iOS is accomplished with EAGLContext::presentRenderbuffer - there is no default
    //  framebuffer (FB 0 is "incomplete"); platform layer creates a framebuffer, so we need to keep track of it
    int prev = nvgluBindFramebuffer(nvglFB);
    nvgluSetFramebufferSize(nvglFB, fbWidth, fbHeight, nvglFBFlags);
    TRACE(painter->endFrame());
    setPrevFrameFence();  // only nanovg draws need to complete before next frame's uploads
    nvgluSetScissor(0, 0, 0, 0);  // disable scissor for blit
    TRACE(nvgluBlitFramebuffer(nvglFB, prev));  // blit to prev FBO and rebind it
  }
  else {
    TRACE(painter->endFrame());
    setPrevFrameFence();
  }

  // caller presents (platformPresent())
  TRACE_FLUSH();

  // workaround for bug on Android ... due to SDL threading? only on some devices? WTF!?!
  //  note that SDL surfaceChanged() reports RGB_565 (default for non-GL), but GL surface is always RGBA_8888
#if PLATFORM_ANDROID
  if(oldDeviceRect != painter->deviceRect) {
    // present now, then blit again so caller presents same frame a second time (contents of back buffer
    //  are undefined after swap)
    PLATFORM_LOG("Performing extra GL_SwapWindow due to FB size change\n");
    TRACE(platformPresent());
    int prev = nvgluBindFramebuffer(nvglFB);
    TRACE(nvgluBlitFramebuffer(nvglFB, prev));  // blit to prev FBO and rebind it
  }
#endif
  return true;
}

void Application::sdlEvent(SDL_Event* event)
{
  TRACE_SCOPE("sdlEvent: type = %s", sdlEventName(event).c_str());
#if IS_DEBUG
  if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_PRINTSCREEN) {
    if(event->key.keysym.mod & KMOD_CTRL)
      SvgGui::debugDirty = !SvgGui::debugDirty;
    else {
      Window* debugWin = gui->windowfromSDLID(event->key.windowID);
      if(debugWin && debugWin->modalChild())
        debugWin = debugWin->modalChild();
      debugWin = debugWin ? debugWin : gui->windows.front();
      if(!(event->key.keysym.mod & KMOD_SHIFT)) {
        // need to rerun layout w/ debugLayout set to get layout:ltwh data (prevent w/ Shift+PrintScreen)
        SvgGui::debugLayout = true;
        layoutAndDraw();
        SvgGui::debugLayout = false;
      }
      XmlStreamWriter xmlwriter;
      SvgWriter::DEBUG_CSS_STYLE = true;
      SvgWriter(xmlwriter).serialize(debugWin->documentNode());
      SvgWriter::DEBUG_CSS_STYLE = false;
#if PLATFORM_WIN
      const char* debug_layout = "C:/Temp/debug_layout.svg";
#else
      const char* debug_layout = "/home/mwhite/styluslabs/usvg/test/debug_layout.svg";
#endif
      xmlwriter.saveFile(debug_layout);
      PLATFORM_LOG("Post-layout SVG written to %s\n", debug_layout);
    }
  }
  else
#endif
    gui->sdlEvent(event);
}

// show window and reenter event loop to block until window finishes
void Application::execWindow(Window* w)
{
  // bit of a hack to display modal on behalf of another modal
  gui->showModal(w, gui->windows.front()->modalOrSelf());
  // event queue will have been drained before modal shown, so we need to make sure it's actually drawn
#ifndef SVGGUI_MULTIWINDOW  // assuming there will be events from showing new window for non-mobile case
  layoutAndDraw();
#endif
  // if window is closed, do not redraw so we don't clear dirty rect on parent window before caller updates
  //  in response to window closure
  while(processEvents() && w->isVisible())
    layoutAndDraw();
  if(w->isVisible())
    gui->closeWindow(w);  // in case runApplication was cleared instead of window being closed
}

int Application::execDialog(Dialog* dialog)
{
  execWindow(dialog);
  return dialog->result;
}

void Application::asyncDialog(Dialog* dialog, const std::function<void(int)>& callback)
{
  dialog->onFinished = [dialog, callback](int res){
    if(callback)
      callback(res);
    // Window deletes node
    SvgGui::delayDeleteWin(dialog);  // immediate deletion causes crash when closing link dialog by dbl click
  };
  gui->showModal(dialog, gui->windows.front()->modalOrSelf());
}
