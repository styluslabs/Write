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
#include "SDL_syswm.h"
#elif PLATFORM_LINUX
#include "linux/linuxtablet.h"
#elif PLATFORM_OSX
#include "macos/macoshelper.h"
#elif PLATFORM_EMSCRIPTEN
#include "wasm/wasmhelper.h"
#endif

// note that bug w/ iOS split view was fixed after SDL 2.0.10 (https://hg.libsdl.org/SDL/rev/806598d72494)
//  so we need to use a later rev until 2.0.12 comes out
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
#else
// GLAD generated files are in nanovg-2 repo; link to generate is:
// https://glad.dav1d.de/#profile=core&language=c&specification=gl&loader=on&api=gl%3D3.3&extensions=GL_ARB_shader_image_load_store&extensions=GL_EXT_shader_framebuffer_fetch
#include "glad.h"
//#define GLEW_STATIC
//#include <GL/glew.h>
#endif

// SW renderer used on Mac since GL extensions not avail; GL blitter used because SDL surface doesn't support
//  high-DPI scaling on OSX (it just uses the window size)
// SW renderer used on Android if GLES 3.1 not available; GL blitter due to problems on old Samsung tablets
//  with both SDL SW renderer (uses GLES too) and ANativeWindow_unlockAndPost
// SW renderer only used on iOS when forced in config file; SDL surface doesn't support high-DPI on iOS
#define USE_GL_BLITTER (PLATFORM_OSX || PLATFORM_ANDROID || PLATFORM_IOS)  // || PLATFORM_EMSCRIPTEN)

#define USE_NANOVG_VTEX 1
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

//#define MINIZ_GZ_TEST
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
static void* swFB = NULL;
#if USE_GL_BLITTER
static NVGSWUblitter* swBlitter = NULL;
#endif

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

static int sdlEventFilter(void* app, SDL_Event* event)
{
#if PLATFORM_LINUX
  if(event->type == SDL_SYSWMEVENT) {
    linuxProcessXEvent(event);
    return 0;  // no further processing
  }
  return 1;
#elif PLATFORM_MOBILE || PLATFORM_WIN  // we translate WM_QUERYENDSESSION to SDL_APP_WILLENTERBACKGROUND
  return static_cast<ScribbleApp*>(app)->sdlEventFilter(event);
#else
  return 1;
#endif
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
  return ""; //"zh_CN";
#endif
}

// for better text editing (see mapsapp)
void PLATFORM_setImeText(const char* text, int selStart, int selEnd) {}

// Windows DPI notes: GetDpiForMonitor(MDT_RAW_DPI) returns error (not supported) in Windows 8.1 VM
// - GetDpiForMonitor(MDT_EFFECTIVE_DPI) is what SDL uses (returns 120; actual is 210 and 125% scaling)
// - GetDeviceCaps(hdc, HORZSIZE or VERTSIZE) very unreliable; suggested approach is to read EDID with SetupAPI
void Application::setupUIScale(float horzdpi)
{
  float pxRatio = 1.0f;
  int winWidth, winHeight;
  SDL_GetWindowSize(sdlWindow, &winWidth, &winHeight);
  int fbWidth = winWidth, fbHeight = winHeight;
  if(glRender || USE_GL_BLITTER)
    SDL_GL_GetDrawableSize(sdlWindow, &fbWidth, &fbHeight);

#if PLATFORM_IOS
  // SDL sets UIWindow.contentScaleFactor to UIScreen.nativeScale to use actual screen res on iPhone Plus, so
  //  pxRatio (== nativeScale) can be non-integer (e.g. 2.6 for iPhone Plus)
  pxRatio = float(fbWidth)/float(winWidth);  //int(float(fbWidth)/float(winWidth) + 0.5f);
  horzdpi = horzdpi > 0 && horzdpi < 700 ? horzdpi : pxRatio * 150;
#elif PLATFORM_EMSCRIPTEN
  pxRatio = emscripten_get_device_pixel_ratio();
  horzdpi = horzdpi > 0 && horzdpi < 700 ? horzdpi : pxRatio * 150;
#else
  if(fbWidth != winWidth) {
    //  PLATFORM_LOG("Warning: unexpected system UI scaling!\n");
    pxRatio = float(fbWidth)/float(winWidth);
  }
  if(horzdpi <= 0) {
#if PLATFORM_DESKTOP
    // DPI reported by Windows is 96*<scaling factor> ... using 168 is about right for our UI
    //SDL_GetDisplayDPI(0, NULL, &horzdpi, NULL);
    //horzdpi = 168*horzdpi/96;
    //if(horzdpi <= 125) { // 72, 96, and 120 are common garbage values returned by Windows
      SDL_Rect r;
      int disp = SDL_GetWindowDisplayIndex(sdlWindow);
      SDL_GetDisplayBounds(disp < 0 ? 0 : disp, &r);
      horzdpi = pxRatio*std::max(r.h, r.w)/11.2f;  // 12.3in diag (Surface Pro) => 10.2in width; 14 in diag (X1 yoga) => 12.2in width
    //}
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


int SDL_main(int argc, char* argv[])
{
  Application::runApplication = true;
#if PLATFORM_WIN
  setDPIAware();
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
#endif
  unet_init();
  //setlocale(LC_NUMERIC, "C");  -- we may need this if using sprintf for writing SVGs
  //SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Attach Debugger", "Attach debugger than click OK.", NULL);

  // these apply to software renderer only (and must preceed SDL_Init) - avoid OpenGL entirely
  SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  SDL_Init(SDL_INIT_VIDEO);

#if PLATFORM_IOS || PLATFORM_EMSCRIPTEN
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif PLATFORM_ANDROID
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  // 2.1 was working w/ GLES3 shader on desktop (VM and host), but probably shouldn't count on it
  // nanovg gl3 impl. uses 3.3 core - consider trying 3.0 or 3.1 instead
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
  //SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  // if we're rendering everything to our own FB, we could try this:
  //SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);  -- replace swap buffers with glFlush()
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);  // SDL docs say this gives speed up on iOS
  SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);  // SDL docs say this gives speed up on iOS
  // SDL defaults to RGB565 on iOS - I think we want more quality (e.g. for smooth gradients)
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

  const char* sdlBasePath = SDL_GetBasePath();
  Application::appDir = sdlBasePath ? sdlBasePath : "";
  SDL_free((void*)sdlBasePath);
  // event filter clears SDL event queue, so set it before showing window
  ScribbleApp* scribbleApp = new ScribbleApp(argc, argv);
  ScribbleApp::app = scribbleApp;
  SDL_SetEventFilter(sdlEventFilter, scribbleApp);  // for app lifecycle events
#if PLATFORM_LINUX
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");  // play nice with other apps
#elif PLATFORM_ANDROID
  if(AndroidHelper::doAction(A_GPU_BLACKLIST))  // try to catch some bad GPUs before even trying GL
    ScribbleApp::cfg->set("glRender", 0);
#endif

  bool sRGB = ScribbleApp::cfg->Bool("sRGB");
  int nvgFlags = NVG_AUTOW_DEFAULT | (sRGB ? NVG_SRGB : 0) | NVG_NO_FONTSTASH;
  nvglFBFlags = sRGB ? NVG_IMAGE_SRGB : 0;
#if PLATFORM_MOBILE
  if(sRGB)
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);  // needed for sRGB on iOS and Android
#endif

  SDL_Rect dispBounds;
  auto winGeom = parseNumbersList(ScribbleApp::cfg->String("windowState", ""), 6);
  int dispIdx = winGeom.size() < 6 || winGeom[5] >= SDL_GetNumVideoDisplays() ? 0 : winGeom[5];
  SDL_GetDisplayBounds(dispIdx, &dispBounds);
  if(winGeom.size() < 5 || winGeom[0] > dispBounds.w
      || winGeom[1] > dispBounds.h || winGeom[2] > dispBounds.w || winGeom[3] > dispBounds.h)
    winGeom = { 100, 100, 800, 800, 1, 0 };
  Uint32 winMaxFlag = (winGeom[4] || PLATFORM_EMSCRIPTEN) ? SDL_WINDOW_MAXIMIZED : 0;
  winGeom[0] += dispBounds.x;
  winGeom[1] += dispBounds.y;

  SDL_Window* sdlWindow = NULL;
  SDL_GLContext sdlContext = NULL;
  NVGcontext* nvgContext = NULL;
  // mac and emscripten always use SW renderer
#if !PLATFORM_OSX && !PLATFORM_EMSCRIPTEN
  if(ScribbleApp::cfg->Int("glRender")) {
    // create window so we can create GL context
    sdlWindow = SDL_CreateWindow("Write", winGeom[0], winGeom[1], winGeom[2], winGeom[3],
        winMaxFlag|SDL_WINDOW_RESIZABLE|SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!sdlWindow)
      PLATFORM_LOG("SDL_CreateWindow (OpenGL) failed: %s\n", SDL_GetError());
    else {
      sdlContext = SDL_GL_CreateContext(sdlWindow);
      if(!sdlContext)
        PLATFORM_LOG("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
      else {
        bool glLoadOK = true;
#if PLATFORM_WIN || PLATFORM_LINUX
        glLoadOK = gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
        if(!glLoadOK)
          PLATFORM_LOG("gladLoadGLLoader failed\n");
        // don't need any extensions for vtex renderer
        //if(!GLAD_GL_EXT_shader_framebuffer_fetch && !GLAD_GL_ARB_shader_image_load_store
        //     && ScribbleApp::cfg->Int("glRender") < 2) {  // set glRender > 1 to force
        //  PLATFORM_LOG("GL extensions not found\n");
        //  glLoadOK = false;
        //}
#elif PLATFORM_ANDROID
        // blacklist PowerVR GPUs (libGLESv2_mtk.so crashes); glGetString doesn't work until context is created
        const char* glRenderer = (const char*)glGetString(GL_RENDERER);
        glLoadOK = ScribbleApp::cfg->Int("glRender") > 1 || !glRenderer || strncmp(glRenderer, "PowerVR", 7) != 0;  //"Mali",4 too?
#endif
        // FB fetch is even slower than FB swap on Intel HD 620 GPU
        // also, FB fetch messes up system cursor on Windows ... maybe just because we disable GL_BLEND?
        int cfgFlags = ScribbleApp::cfg->Int("nvgGlFlags", 0);
#if PLATFORM_DESKTOP && !USE_NANOVG_VTEX
        if(!cfgFlags) cfgFlags |= NVGL_NO_FB_FETCH;
#endif
        nvgContext = glLoadOK ? nvglCreate(nvgFlags | cfgFlags | (SCRIBBLE_DEBUG ? NVGL_DEBUG : 0)) : NULL;
        // This will cause SDL_GL_SwapWindow to stall for vsync, so we'd want to call it on a separate thread
        //if(SDL_GL_SetSwapInterval(-1) == 0) PLATFORM_LOG("Adaptive vsync enabled\n");
        //else if(SDL_GL_SetSwapInterval(1) == 0) PLATFORM_LOG("Non-adpative vsync enabled\n");
        //else PLATFORM_LOG("Unable to enable vsync\n");
        if(!nvgContext) {
          SDL_GL_DeleteContext(sdlContext);
          sdlContext = NULL;
        }
      }  // sdlContext
      if(!nvgContext) {
        SDL_DestroyWindow(sdlWindow);
        sdlWindow = NULL;
      }
    }  // /sdlWindow
  }  // /useGL
#endif
  Application::glRender = nvgContext != NULL;
  // create SW window and nanovg-2 SW renderer if GL didn't work
  if(!nvgContext) {
#if PLATFORM_ANDROID
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
#if PLATFORM_MOBILE
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);  // otherwise Android will convert SW renderer output!
#endif
    sdlWindow = SDL_CreateWindow("Write", winGeom[0], winGeom[1], winGeom[2], winGeom[3],
        winMaxFlag|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(USE_GL_BLITTER ? SDL_WINDOW_OPENGL : 0));
#if USE_GL_BLITTER
    // note that OpenGL loader is not needed for Mac
    sdlContext = SDL_GL_CreateContext(sdlWindow);
    swBlitter = nvgswuCreateBlitter();
#endif
    nvgContext = nvgswCreate(nvgFlags);
    ScribbleApp::cfg->set("glRender", 0);
    if(!sdlWindow || !nvgContext || (USE_GL_BLITTER && !sdlContext))
      PLATFORM_LOG("SW renderer setup failed: %s\n", SDL_GetError());

    int ncores = std::thread::hardware_concurrency();
    int nthreads = (ncores > 0 ? ncores : 4);  //(PLATFORM_MOBILE ? 1 : 2) -- hyperthreading incl on Windows
#if !IS_DEBUG
    if(nthreads > 1) {
      swThreadPool = new ThreadPool(nthreads);
      nvgswSetThreading(nvgContext, nthreads/2, 2, poolSubmit, poolWait);
    }
#endif
  }
  Application::sdlWindow = sdlWindow;


  if(ScribbleApp::cfg->Bool("perfTrace")) { TRACE_INIT(); }
#if PLATFORM_WIN
  initTouchInput(sdlWindow, ScribbleApp::cfg->Bool("useWintab"));
#elif PLATFORM_OSX
  macosDisableMouseCoalescing();  // get all tablet input points
#elif PLATFORM_LINUX
  linuxInitTablet(sdlWindow);
  SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);  // linuxtablet.c handles touch events even if no pen
#elif PLATFORM_EMSCRIPTEN
  wasmSetupInput();
#endif

  Painter::initFontStash(FONS_DELAY_LOAD | FONS_SUMMED);
  nvgSetFontStash(nvgContext, Painter::fontStash);
  Painter* painter = new Painter(nvgContext);
  Painter::cachingPainter = painter;
  Application::painter = painter;
  if(Application::glRender) {  //useFramebuffer
    nvglFB = nvgluCreateFramebuffer(nvgContext, 0, 0, NVGLU_NO_NVG_IMAGE | nvglFBFlags);
    if(sRGB)
      nvgluSetFramebufferSRGB(1);  // no-op for GLES - sRGB enabled iff FB is sRGB
  }

  Painter boundsPainter(Painter::PAINT_NULL);
  SvgPainter boundsCalc(&boundsPainter);
  SvgDocument::sharedBoundsCalc = &boundsCalc;

  setupResources();  // load fonts, icons, etc.
  const char* cfgLocale = ScribbleApp::cfg->String("locale", "");
  scribbleApp->hasI18n = setupI18n(cfgLocale[0] ? cfgLocale : getLocale());
  SvgGui* svgGui = new SvgGui();
  Application::gui = svgGui;
  svgGui->setWindowStylesheet(std::unique_ptr<SvgCssStylesheet>(createStylesheet()));

  // maybe increase dpi by 1.5x if screen larger than 15 inches?
  Dim dpi = ScribbleApp::cfg->Int("screenDPI");
  Application::setupUIScale(dpi >= 10 && dpi <= 1200 ? dpi : 0);

  scribbleApp->init();  // this previously could enter event loop, but not anymore
#if PLATFORM_EMSCRIPTEN
  //SvgGui::debugDirty = true;
  emscripten_set_main_loop(wasmMainLoop, 0, 1);  // fps = 0 (use default), infinite loop = 1 (never returns)
#else
  while(Application::processEvents()) {
    Application::layoutAndDraw();
  }
#endif

  // save window state
  int winMax = 0, winX = 0, winY = 0, winW = 0, winH = 0;
  Uint32 winstate = SDL_GetWindowFlags(sdlWindow);
  winMax = winstate & SDL_WINDOW_MAXIMIZED ? 1 : 0;
  if(winMax)
    SDL_RestoreWindow(sdlWindow);
  // we could get size from SvgGui winBounds, but we have to get position from SDL anyway
  dispIdx = SDL_GetWindowDisplayIndex(sdlWindow);
  SDL_GetDisplayBounds(dispIdx, &dispBounds);
  SDL_GetWindowPosition(sdlWindow, &winX, &winY);
  SDL_GetWindowSize(sdlWindow, &winW, &winH);
  ScribbleApp::cfg->set("windowState", fstring("%d %d %d %d %d %d",
      winX - dispBounds.x, winY - dispBounds.y, winW, winH, winMax, dispIdx).c_str());

#if IS_DEBUG && !PLATFORM_MOBILE
  SDL_HideWindow(sdlWindow);
  SCRIBBLE_LOG("\n**** RUNNING TESTS ****\n%s\n", scribbleApp->runTest("test").c_str());
#endif

  delete scribbleApp;
  delete svgGui;
  delete painter;
  if(nvglFB)
    nvgluDeleteFramebuffer(nvglFB);
  if(swThreadPool)
    delete swThreadPool;
  if(sdlContext)
    SDL_GL_DeleteContext(sdlContext);
#if USE_GL_BLITTER
  nvgswuDeleteBlitter(swBlitter);
  free(swFB);
  swFB = NULL;  // SDL_main can be called again on Android(!)
#endif
  SDL_Quit();
  unet_terminate();
  return 0;
}

void Application::layoutAndDraw()
{
  glRender ? layoutAndDrawGL() : layoutAndDrawSW();
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

#if USE_GL_BLITTER
void Application::layoutAndDrawSW()
{
  int fbWidth = 0, fbHeight = 0;
  SDL_GL_GetDrawableSize(sdlWindow, &fbWidth, &fbHeight);
  if(fbWidth <= 0 || fbHeight <= 0)
    return;
  bool sizeChanged = swFB && (fbWidth != swBlitter->width || fbHeight != swBlitter->height);
  if(!swFB || sizeChanged)
    swFB = realloc(swFB, fbWidth*fbHeight*4);
  nvgswSetFramebuffer(painter->vg, swFB, fbWidth, fbHeight, 0, 8, 16, 24);

  Rect dirty = tracedGuiLayoutAndDraw(fbWidth, fbHeight);
  if(!dirty.isValid())
    return;

  TRACE(painter->endFrame());
  // note we don't flip y of dirty rect since that is done when rendering
  TRACE(nvgswuBlit(swBlitter, swFB, fbWidth, fbHeight,
      int(dirty.left), int(dirty.top), int(dirty.width()), int(dirty.height())));
  TRACE(SDL_GL_SwapWindow(sdlWindow));

#if PLATFORM_ANDROID
  if(sizeChanged) {
    TRACE(nvgswuBlit(swBlitter, swFB, fbWidth, fbHeight,
        int(dirty.left), int(dirty.top), int(dirty.width()), int(dirty.height())));
    TRACE(SDL_GL_SwapWindow(sdlWindow));
  }
#endif
  TRACE_FLUSH();
}
#elif 0 //PLATFORM_ANDROID  -- seems to have some issues on the same devices as the SDL surface version!
// since Android can enlarge the dirty area from what was requested, I think it makes sense to draw to a
//  separate buffer, then copy dirty area instead of drawing directly to Android surface
void Application::layoutAndDrawSW()
{
  static int prevWidth = 0, prevHeight = 0;
  int fbWidth = 0, fbHeight = 0;
  SDL_GetWindowSize(sdlWindow, &fbWidth, &fbHeight);
  if(!swFB || fbWidth != prevWidth || fbHeight != prevHeight)
    swFB = realloc(swFB, fbWidth*fbHeight*4);
  prevWidth = fbWidth;  prevHeight = fbHeight;
  nvgswSetFramebuffer(Painter::vg, swFB, fbWidth, fbHeight, 0, 8, 16, 24);

  Rect dirty = tracedGuiLayoutAndDraw(fbWidth, fbHeight);
  if(!dirty.isValid())
    return;
  TRACE(painter->endFrame());

  TRACE(AndroidHelper::blitSurface(swFB, fbWidth, fbHeight,
      int(dirty.left), int(dirty.top), int(dirty.width()), int(dirty.height())));
  TRACE_FLUSH();
}
#else
void Application::layoutAndDrawSW()
{
  SDL_Surface* sdlSurface = SDL_GetWindowSurface(sdlWindow);  // has to be done every frame in case window resizes
  if(!sdlSurface) {
    // hopefully this only happens when app is getting hidden (was getting crashes on Android)
    PLATFORM_LOG("SDL_GetWindowSurface failed: %s\n", SDL_GetError());
    return;
  }
  int fbWidth = sdlSurface->w;
  int fbHeight = sdlSurface->h;
  SDL_PixelFormat* fmt = sdlSurface->format;
  nvgswSetFramebuffer(painter->vg, sdlSurface->pixels, fbWidth, fbHeight, fmt->Rshift, fmt->Gshift, fmt->Bshift, 24);
  //SDL_FillRect(sdlSurface, NULL, SDL_MapRGB(sdlSurface->format, 255, 255, 255));

  Rect dirty = tracedGuiLayoutAndDraw(fbWidth, fbHeight);
  if(!dirty.isValid())
    return;

  SDL_LockSurface(sdlSurface);
  TRACE(painter->endFrame());
  SDL_UnlockSurface(sdlSurface);

  if(dirty != painter->deviceRect) {
    SDL_Rect r;
    r.x = int(dirty.left); r.y = int(dirty.top); r.w = int(dirty.width()); r.h = int(dirty.height());
    TRACE(SDL_UpdateWindowSurfaceRects(sdlWindow, &r, 1));
  }
  else
    TRACE(SDL_UpdateWindowSurface(sdlWindow));
  TRACE_FLUSH();
}
#endif

void Application::layoutAndDrawGL()
{
  int fbWidth = 0, fbHeight = 0;
  SDL_GL_GetDrawableSize(sdlWindow, &fbWidth, &fbHeight);

  Rect oldDeviceRect = painter->deviceRect;
  Rect dirty = tracedGuiLayoutAndDraw(fbWidth, fbHeight);
  if(!dirty.isValid())
    return;
  // for now, we scissor here to limit fill rate; we may add support directly to nanovg-2 later (but we would
  //  still need to set glScissor here to use glClear); we assume dirty has already been rounded to ints

  if(dirty != painter->deviceRect)
    nvgluSetScissor(int(dirty.left), fbHeight - int(dirty.bottom), int(dirty.width()), int(dirty.height()));
  //nvgluClear(nvgRGBA(0, 0, 0, 0));

  if(nvglFB) {
    // drawing to screen on iOS is accomplished with EAGLContext::presentRenderbuffer - there is no default
    //  framebuffer (FB 0 is "incomplete"); SDL creates a framebuffer for us, so we need to keep track of it
    // FB handle can also be obtained from SDL_GetWindowWMInfo
    int prev = nvgluBindFramebuffer(nvglFB);
    nvgluSetFramebufferSize(nvglFB, fbWidth, fbHeight, nvglFBFlags);
    TRACE(painter->endFrame());
    nvgluSetScissor(0, 0, 0, 0);  // disable scissor for blit
    TRACE(nvgluBlitFramebuffer(nvglFB, prev));  // blit to prev FBO and rebind it
  }
  else
    TRACE(painter->endFrame());

  TRACE(SDL_GL_SwapWindow(sdlWindow));
  TRACE_FLUSH();

  // workaround for bug on Android ... due to SDL threading? only on some devices? WTF!?!
  //  note that SDL surfaceChanged() reports RGB_565 (default for non-GL), but GL surface is always RGBA_8888
#if PLATFORM_ANDROID
  if(oldDeviceRect != painter->deviceRect) {
    PLATFORM_LOG("Performing extra GL_SwapWindow due to FB size change\n");
    int prev = nvgluBindFramebuffer(nvglFB);
    TRACE(nvgluBlitFramebuffer(nvglFB, prev));  // blit to prev FBO and rebind it
    TRACE(SDL_GL_SwapWindow(sdlWindow));
  }
#endif
}

// called from timerThreadFn() in svggui.cpp
void PLATFORM_WakeEventLoop()
{
#if PLATFORM_WIN
  // Wake up main thread, assumed to wait using WaitMessage()
  SDL_SysWMinfo wmInfo;
  SDL_VERSION(&wmInfo.version);
  if(!Application::gui->windows.empty() && SDL_GetWindowWMInfo(Application::gui->windows.front()->sdlWindow, &wmInfo))
    PostMessage(wmInfo.info.win.window, WM_USER + 1337, 0, 0);
#elif PLATFORM_IOS
  iosWakeEventLoop();
#elif PLATFORM_OSX
  macosWakeEventLoop();
#endif
}

// If this causes *any* difficulty in the future, move processEvents() and execWindow() back into ScribbleApp
bool Application::processEvents()
{
  SDL_Event event;
  // SDL_WaitEvent() does while(!SDL_PeekEvent()) { SDL_Delay(10) } ... fixed as of 2.24
#if PLATFORM_WIN
  while(!SDL_PollEvent(&event))
    TRACE(WaitMessage());  // Windows API fn
  // ... this still isn't quite right, because SDL's WIN_PumpEvents returns after some elapsed time even if
  //  messages are still available, so if it happens that none of the messages generates an SDL event, we will
  //  enter WaitMessage() w/ unprocessed messages still available!
#elif PLATFORM_IOS
  // seems to work; keep an eye out for issues
  while(!SDL_PollEvent(&event))
    TRACE(iosPumpEventsBlocking());
#elif PLATFORM_OSX
  while(!SDL_PollEvent(&event))
    TRACE(macosWaitEvent());
#elif PLATFORM_EMSCRIPTEN
  if(!SDL_PollEvent(&event))
    return false;
#else
  TRACE(SDL_WaitEvent(&event));
#endif

  // use loop to empty event queue before rendering frame, so that user's most recent input is accounted for
  do {
    //PLATFORM_LOG("%s\n", sdlEventLog(&event).c_str());
    TRACE_SCOPE("sdlEvent: type = %s", sdlEventName(&event).c_str());
#if IS_DEBUG
    if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PRINTSCREEN) {
      if(event.key.keysym.mod & KMOD_CTRL)
        SvgGui::debugDirty = !SvgGui::debugDirty;
      else {
        Window* debugWin = gui->windowfromSDLID(event.key.windowID);
        if(debugWin && debugWin->modalChild())
          debugWin = debugWin->modalChild();
        debugWin = debugWin ? debugWin : gui->windows.front();
        if(!(event.key.keysym.mod & KMOD_SHIFT)) {
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
      gui->sdlEvent(&event);
  } while(runApplication && SDL_PollEvent(&event));

  return runApplication;
}

// show window and reenter event loop to block until window finishes
void Application::execWindow(Window* w)
{
  // bit of a hack to display modal on behalf of another modal
  gui->showModal(w, gui->windows.front()->modalOrSelf());  //win->modalOrSelf());
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
    //delete dialog;  //gui->deleteWidget(dialog);  -- Window deletes node
    SvgGui::delayDeleteWin(dialog);  // immediate deletion causes crash when closing link dialog by dbl click
  };
  gui->showModal(dialog, gui->windows.front()->modalOrSelf());
}

/*
#include "document.h"
#include <chrono>

int SDL_main(int argc, char* argv[])
{
  Document* newdoc = new Document();
  std::string ioshome = getenv("HOME");
  auto t0_load = std::chrono::high_resolution_clock::now();
  newdoc->load((ioshome + "/Documents/Optimization.svgz").c_str());

  auto t0_save = std::chrono::high_resolution_clock::now();
  //newdoc->save((ioshome + "/Documents/Optimization2.svgz").c_str(), NULL);
  newdoc->save((ioshome + "/Documents/Optimization3.svg").c_str(), NULL);  // NOTE .svg
  auto t1_save = std::chrono::high_resolution_clock::now();
  PLATFORM_LOG("Load time: %.3f s\n", std::chrono::duration<float>(t0_save - t0_load).count());
  PLATFORM_LOG("Save time: %.3f s\n", std::chrono::duration<float>(t1_save - t0_save).count());
  PLATFORM_LOG("$HOME: %s\n", getenv("HOME"));

  // Release build: time to save Optimization.svg(z) (does not include load time)
  // 6 = 2.73s, 6.7MB; 1 = 1.13s, 8.9 MB, 2 = 1.15s, 7.7 MB, 4 = 1.4s, 6.8 MB, 3 = 1.34s, 7.0 MB
  // save .svg (no compression): 0.87s, 22.4 MB; slow realToStr: 0.34s; fast realToStr: 0.18s; stb_sprintf for Element attribs: 0.155s
  // ... optimized save + level = 2 compression: 0.41s; 16 MB buffer for miniz_go doesn't make much difference

  // iOS (Release build): 0.8s load, 1.4s save svgz (level=2), 0.5s save svg

  // Let's stick w/ compression level = 2 for now - 3 or 4 aren't much slower and gain a little bit of ratio, but let's wait until
  //  we're sure overall performance is OK

  return 0;
}
*/
