#include <unistd.h>  // for symlink()
#include "ugui/svggui.h"
#include "usvg/svgwriter.h"

#include "application.h"
#include "ulib/fileutil.h"
//#include "linuxPlatform.h"
//#include "util/yamlPath.h"
//#include "util/elevationManager.h"
//#include "util.h"
//#include "nfd.h"
//#include "stb_image.h"
//#include "stb_image_write.h"  // for screenshots

//#include "mapsources.h"
//#include "plugins.h"
//#include "offlinemaps.h"

// conflict between Xlib Window and SvgGui Window (should have used namespace)
#define Window XXWindow
//#include <glad/glad.h>
#include <GL/glx.h>
#include <GL/glext.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <malloc.h>

// XShm
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define LOGE PLATFORM_LOG

// MapsApp

struct SDL_Window {
  Display* dpy = NULL;
  Window win;
  XIC ic;  // input context
  GC gc;
  Visual* vis = NULL;
  XImage* img = NULL;
  int width = 0, height = 0;
};

static SDL_Window xContext;

static struct {
  Atom absX, absY, absP, tiltX, tiltY, clipboard, imagePng, sdlSel, Incr,
      UTF8_STRING, STRING, COMPOUND_TEXT, C_STRING, TARGETS;
} XAtoms;

void PLATFORM_WakeEventLoop(void)
{
  XEvent event = { ClientMessage };
  event.xclient.window = xContext.win;
  event.xclient.format = 32; // Data is 32-bit longs
  event.xclient.message_type = 0;  //XInternAtom(xDpy, "NULL", False);
  XSendEvent(xContext.dpy, xContext.win, False, 0, &event);
  XFlush(xContext.dpy);
}

/*
void TANGRAM_WakeEventLoop() { PLATFORM_WakeEventLoop(); }

void PLATFORM_setImeText(const char* text, int selStart, int selEnd) {}

bool MapsApp::openURL(const char* url)
{
  system(fstring("xdg-open '%s' || x-www-browser '%s' &", url, url).c_str());
  return true;
}

void MapsApp::notifyStatusBarBG(bool) {}
void MapsApp::setSensorsEnabled(bool enabled) {}
void MapsApp::setServiceState(int state, float intervalSec, float minDist) {}
void MapsApp::getSafeAreaInsets(float *top, float *bottom) { *top = 0; *bottom = 0; }
void MapsApp::extractAssets(const char*) {}
*/

// SDL
static std::string currClipboard;

Uint32 SDL_GetTicks()
{
  static Timestamp t0 = mSecSinceEpoch();
  return mSecSinceEpoch() - t0;
}

char* SDL_GetClipboardText()
{
  return strndup(currClipboard.data(), currClipboard.size());
}

SDL_bool SDL_HasClipboardText()
{
  return !currClipboard.empty() ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetClipboardText(const char* text)
{
  currClipboard = text;
  XSetSelectionOwner(xContext.dpy, XAtoms.clipboard, xContext.win, CurrentTime);
  return 0;
}

void SDL_free(void* mem) { free(mem); }

static SDL_Keymod currKeymod = KMOD_NONE;
SDL_Keymod SDL_GetModState() { return currKeymod; }

void SDL_SetTextInputRect(SDL_Rect* rect) {}
SDL_bool SDL_IsTextInputActive() { return SDL_FALSE; }
void SDL_StartTextInput() {}
void SDL_StopTextInput() {}

void SDL_RaiseWindow(SDL_Window* window) {}
void SDL_SetWindowTitle(SDL_Window* win, const char* title) {}
void SDL_GetWindowSize(SDL_Window* win, int* w, int* h)
{
  if(w) { *w = xContext.width; }
  if(h) { *h = xContext.height; }
}
void SDL_GetWindowPosition(SDL_Window* win, int* x, int* y) { *x = 0; *y = 0; }
void SDL_DestroyWindow(SDL_Window* win) {}
SDL_Window* SDL_GetWindowFromID(Uint32 id) { return NULL; }


int SDL_GetWindowDisplayIndex(SDL_Window* win) { return 0; }

int SDL_GetDisplayBounds(int display, SDL_Rect* rect)
{
  int snum = DefaultScreen(xContext.dpy);
  rect->w = DisplayWidth(xContext.dpy, snum);
  rect->h = DisplayHeight(xContext.dpy, snum);
  rect->x = 0;
  rect->y = 0;
  return 0;
}

void SDL_GL_GetDrawableSize(SDL_Window* window, int *w, int *h) { SDL_GetWindowSize(window, w, h); }

void SDL_GL_SwapWindow(SDL_Window* win)
{
  glXSwapBuffers(xContext.dpy, xContext.win);
}

int SDL_PushEvent(SDL_Event* event)
{
  Application::sdlEvent(event);
  return 1;
}

int SDL_PeepEvents(SDL_Event* events, int numevents, SDL_eventaction action, Uint32 minType, Uint32 maxType)
{
  if(action != SDL_ADDEVENT) return 0;
  for(int ii = 0; ii < numevents; ++ii)
    Application::sdlEvent(&events[ii]);
  return numevents;
}


// https://github.com/H-M-H/Weylus can be used to send pen input from browser supporting pointer events to
//  Linux (note that the xinput device won't appear until a client connects to web server)

enum {XI2_MOUSE, XI2_DIR_TOUCH, XI2_DEP_TOUCH, XI2_PEN, XI2_ERASER};

typedef struct { float min; float max; int idx; } ValInfo;
typedef struct {
  unsigned int buttons, prevButtons;
  ValInfo X, Y, P, tiltX, tiltY;
  //float minX, maxX, minY, maxY, minP, maxP, minTiltX, maxTiltX, minTiltY, maxTiltY;
  //int idxX, idxY, idxP, idxTiltX, idxTiltY;
  int deviceId;
  int type;
} TabletData;

// we now keep track of every slave pointer, not just tablets
#define MAX_TABLETS 32
static TabletData tabletData[MAX_TABLETS];
static size_t nTablets = 0;

static int xinput2_opcode;


static TabletData* findDevice(int sourceid)
{
  for(size_t ii = 0; ii < nTablets; ++ii) {
    if(tabletData[ii].deviceId == sourceid)
      return &tabletData[ii];
  }
  return NULL;
}

static int xi2ValuatorOffset(const unsigned char *maskPtr, int maskLen, int number)
{
  int offset = 0;
  for(int i = 0; i < maskLen; i++) {
    if(number < 8) {
      if((maskPtr[i] & (1 << number)) == 0)
        return -1;
    }
    for(int j = 0; j < 8; j++) {
      if(j == number)
        return offset;
      if(maskPtr[i] & (1 << j))
        offset++;
    }
    number -= 8;
  }
  return -1;
}

static int xi2GetValuatorValueIfSet(XIDeviceEvent* event, int valuatorNum, double *value)
{
  int offset = xi2ValuatorOffset(event->valuators.mask, event->valuators.mask_len, valuatorNum);
  if(offset >= 0)
    *value = event->valuators.values[offset];
  return offset >= 0;
}

// used for tablet input and dependent touch input
static int xi2ReadValuatorsXYP(XIDeviceEvent* xevent, TabletData* device, SDL_Event* event)
{
  // When screen size changes while app is running, size of DefaultScreenOfDisplay doesn't seem to change,
  //  only root window size changes (xrandr docs say it changes root window size)
  static XWindowAttributes rootAttr = {0};

  double currX = 0, currY = 0, currP = 0;
  // abort if we can't read x or y, to avoid invalid points
  if(!xi2GetValuatorValueIfSet(xevent, device->X.idx, &currX))
    return 0;
  float nx = (currX - device->X.min)/(device->X.max - device->X.min);
  if(!xi2GetValuatorValueIfSet(xevent, device->Y.idx, &currY))
    return 0;
  float ny = (currY - device->Y.min)/(device->Y.max - device->Y.min);
  xi2GetValuatorValueIfSet(xevent, device->P.idx, &currP);
  float np = (currP - device->P.min)/(device->P.max - device->P.min);

  float dx = xevent->event_x - xevent->root_x;
  float dy = xevent->event_y - xevent->root_y;
  // I don't know how expensive XGetWindowAttributes is, so don't call for every point
  if(event->type == SDL_FINGERDOWN || !rootAttr.width)
    XGetWindowAttributes(xevent->display, DefaultRootWindow(xevent->display), &rootAttr);

  event->tfinger.x = nx*rootAttr.width + dx;
  event->tfinger.y = ny*rootAttr.height + dy;
  event->tfinger.pressure = np;
  return 1;
}

// event_x,y (and root_x,y) in XIDeviceEvent are corrupted for tablet input, so we instead use AbsX and AbsY
//  "valuators"; Qt uses xXIDeviceEvent (the "wire" protocol in XI2Proto.h, somehow obtained directly from
//  xcb struct) where event_x,y are 16.16 fixed point values - these might not be corrupt
// - see bugreports.qt.io/browse/QTBUG-48151 and links there, esp. bugs.freedesktop.org/show_bug.cgi?id=92186
// We could also try using XI_RawMotion events, for which order of values seem to be fixed - see, e.g.,
//  https://github.com/glfw/glfw/pull/1445/files
static void xi2ReportTabletEvent(XIDeviceEvent* xevent, TabletData* device)
{
  unsigned int button = device->buttons ^ device->prevButtons;
  unsigned int eventtype = SDL_FINGERMOTION;
  if((device->buttons & SDL_BUTTON_LMASK) != (device->prevButtons & SDL_BUTTON_LMASK))
    eventtype = (device->buttons & SDL_BUTTON_LMASK) ? SDL_FINGERDOWN : SDL_FINGERUP;

  SDL_Event event = {0};
  event.type = eventtype;
  event.tfinger.timestamp = xevent->time;  //SDL_GetTicks()
  event.tfinger.touchId = device->type == XI2_ERASER ? PenPointerEraser : PenPointerPen;
  //event.tfinger.fingerId = 0; // if SDL sees more than one finger id for a touch id, that's multitouch
  // POINTER_FLAGS >> 4 == SDL_BUTTON_LMASK for pen down w/ no barrel buttons pressed, as desired
  event.tfinger.fingerId = eventtype == SDL_FINGERMOTION ? device->buttons : button;
  // PeepEvents bypasses gesture recognizer and event filters
  if(xi2ReadValuatorsXYP(xevent, device, &event)) {
    // get tilt
    double tiltX = 0, tiltY = 0;
    if(xi2GetValuatorValueIfSet(xevent, device->tiltX.idx, &tiltX))
      event.tfinger.dx = 2*tiltX/(device->tiltX.max - device->tiltY.min);  // scale to -1 .. +1
    if(xi2GetValuatorValueIfSet(xevent, device->tiltY.idx, &tiltY))
      event.tfinger.dy = 2*tiltY/(device->tiltY.max - device->tiltY.min);

    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0); //SDL_PushEvent(&event);
    device->prevButtons = device->buttons;
  }

  //fprintf(stderr, "Tablet event: raw (%f, %f); xevent delta x,y: (%f, %f), rel: (%f, %f, %f) w/ buttons %d"
  //    " from device %d; root win: %d x %d\n", event.tfinger.x, event.tfinger.y,  //currX, currY,
  //    xevent->event_x - event.tfinger.x, xevent->event_y - event.tfinger.y,
  //    event.tfinger.x, event.tfinger.y, event.tfinger.pressure, device->buttons, device->deviceId,
  //    0, 0);  //rootAttr.width, rootAttr.height);
}

static void xi2ReportMouseEvent(XIDeviceEvent* xevent, TabletData* device)
{
  constexpr bool simulateTouch = false;
  if(simulateTouch && !device->buttons && !device->prevButtons) { return; }  // discard hover events

  unsigned int button = device->buttons ^ device->prevButtons;
  unsigned int eventtype = SDL_FINGERMOTION;
  if(device->buttons != device->prevButtons)
    eventtype = (device->buttons & button) ? SDL_FINGERDOWN : SDL_FINGERUP;
  device->prevButtons = device->buttons;

  SDL_Event event = {0};
  event.type = eventtype;
  event.tfinger.timestamp = SDL_GetTicks(); // normally done by SDL_PushEvent()
  event.tfinger.touchId = simulateTouch ? 0 : SDL_TOUCH_MOUSEID;
  event.tfinger.fingerId = eventtype == SDL_FINGERMOTION ? device->buttons : button;
  event.tfinger.x = xevent->event_x;
  event.tfinger.y = xevent->event_y;
  // stick buttons in dx, dy for now
  //event.tfinger.dx = button;
  //event.tfinger.dy = device->buttons;
  event.tfinger.pressure = 1;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0); //SDL_PushEvent(&event);
}

static void xi2ReportWheelEvent(XIDeviceEvent* xevent, TabletData* device)
{
  int b = xevent->detail;
  SDL_Event event = {0};
  event.type = SDL_MOUSEWHEEL;
  event.wheel.timestamp = SDL_GetTicks();
  //event.wheel.windowID = 0;
  //event.wheel.which = 0;  //SDL_TOUCH_MOUSEID;
  event.wheel.x = b == 6 ? 1 : (b == 7 ? -1 : 0);
  event.wheel.y = b == 4 ? 1 : (b == 5 ? -1 : 0);
  // SDL mod state is updates when sending events, not processing, so checking when processing wheel event
  //  fails for Wacom tablet pinch-zoom which sends Ctrl press + wheel event + Ctrl release
  event.wheel.direction = SDL_MOUSEWHEEL_NORMAL | (SDL_GetModState() << 16);
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
}

void xi2ReportTouchEvent(XIDeviceEvent* xevent, uint32_t evtype)
{
  SDL_Event event = {0};
  event.type = evtype;
  event.tfinger.timestamp = SDL_GetTicks();  // normally done by SDL_PushEvent()
  event.tfinger.touchId = 0;  //xev->sourceid
  event.tfinger.fingerId = xevent->detail;
  event.tfinger.x = xevent->event_x;
  event.tfinger.y = xevent->event_y;
  TabletData* devinfo = findDevice(xevent->sourceid);
  if(devinfo && devinfo->type == XI2_DEP_TOUCH)
    xi2ReadValuatorsXYP(xevent, devinfo, &event);
  event.tfinger.pressure = 1;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
}

// I guess we could just use XIValuatorClassInfo instead of making ValInfo...
static void initValInfo(ValInfo* val, XIValuatorClassInfo* vci)
{
  val->min = vci->min;
  val->max = vci->max;
  val->idx = vci->number;
}

static void enumerateDevices(Display* xDisplay)
{
  memset(tabletData, 0, sizeof(tabletData));
  nTablets = 0;
  int deviceCount = 0;
  XIDeviceInfo* devices = XIQueryDevice(xDisplay, XIAllDevices, &deviceCount);
  for (int ii = 0; ii < deviceCount && nTablets < MAX_TABLETS; ++ii) {
    // Only non-master pointing devices are relevant here.
    if(devices[ii].use != XISlavePointer)
      continue;
    int isTouch = 0;
    TabletData* devinfo = &tabletData[nTablets];
    devinfo->X.idx = -1;  devinfo->Y.idx = -1;  devinfo->P.idx = -1;
    for (int c = 0; c < devices[ii].num_classes; ++c) {
      if(devices[ii].classes[c]->type == XITouchClass) {
        XITouchClassInfo* tci = (XITouchClassInfo*)(devices[ii].classes[c]);
        isTouch = tci->mode == XIDependentTouch ? XI2_DEP_TOUCH : XI2_DIR_TOUCH;
      }
      else if(devices[ii].classes[c]->type == XIValuatorClass) {
        XIValuatorClassInfo* vci = (XIValuatorClassInfo*)(devices[ii].classes[c]);
        if(vci->label == XAtoms.absX)
          initValInfo(&devinfo->X, vci);
        else if(vci->label == XAtoms.absY)
          initValInfo(&devinfo->Y, vci);
        else if(vci->label == XAtoms.absP)
          initValInfo(&devinfo->P, vci);
        else if(vci->label == XAtoms.tiltX)
          initValInfo(&devinfo->tiltX, vci);
        else if(vci->label == XAtoms.tiltY)
          initValInfo(&devinfo->tiltY, vci);
      }
    }

    if(devinfo->X.idx >= 0 && devinfo->Y.idx >= 0 && devinfo->P.idx >= 0 && !isTouch) {
      char devname[256];
      size_t jj = 0;
      for(; jj < 255 && devices[ii].name[jj]; ++jj)
        devname[jj] = tolower(devices[ii].name[jj]);
      devname[jj] = '\0';
      devinfo->type = strstr(devname, "eraser") ? XI2_ERASER : XI2_PEN;
      //PLATFORM_LOG("Tablet bounds: %f %f %f %f\n", device->minX, device->minY, device->maxX, device->maxY);
    }
    else
      devinfo->type = isTouch ? isTouch : XI2_MOUSE;

    //PLATFORM_LOG("Device %s is type %d\n", devices[ii].name, device->type);
    devinfo->deviceId = devices[ii].deviceid;
    ++nTablets;
  }
  XIFreeDeviceInfo(devices);
}

static int xiToSDLButton(uint32_t b)
{
  switch (b) {
    case 1: return SDL_BUTTON_LMASK;
    case 2: return SDL_BUTTON_MMASK;
    case 3: return SDL_BUTTON_RMASK;
    default: return 0;
  }
}

static void processXinput2Event(XGenericEventCookie* cookie)
{
  XIDeviceEvent* xiDeviceEvent = (XIDeviceEvent*)cookie->data;
  if(cookie->evtype == XI_TouchUpdate) xi2ReportTouchEvent(xiDeviceEvent, SDL_FINGERMOTION);
  else if(cookie->evtype == XI_TouchBegin) xi2ReportTouchEvent(xiDeviceEvent, SDL_FINGERDOWN);
  else if(cookie->evtype == XI_TouchEnd) xi2ReportTouchEvent(xiDeviceEvent, SDL_FINGERUP);
  else if(cookie->evtype == XI_Motion || cookie->evtype == XI_ButtonPress || cookie->evtype == XI_ButtonRelease) {
    TabletData* devinfo = findDevice(xiDeviceEvent->sourceid);
    if(!devinfo) {
      enumerateDevices(cookie->display);  // "hotplug" support
      return;  // we'll just wait for next event
    }
    // Dependent touch devices may send wheel events for scroll and zoom gestures
    if(devinfo->type == XI2_DIR_TOUCH)
      return;
    // buttons 4,5,6,7 are for +y,-y,+x,-x scrolling
    if(xiDeviceEvent->detail >= 4 && xiDeviceEvent->detail <= 7) {  //&& devinfo->type == XI2_MOUSE) {
      if(cookie->evtype == XI_ButtonPress)
        xi2ReportWheelEvent(xiDeviceEvent, devinfo);
      return;
    }
    if(devinfo->type == XI2_DEP_TOUCH)
      return;
    // we could maybe use xiDeviceEvent->buttons.mask >> 1 instead of tracking button state ourselves
    if(cookie->evtype == XI_ButtonPress)
      devinfo->buttons |= xiToSDLButton(xiDeviceEvent->detail);
    else if(cookie->evtype == XI_ButtonRelease)
      devinfo->buttons ^= xiToSDLButton(xiDeviceEvent->detail);
    // AbsX, AbsY don't seem to be set for pen press/release events, so wait for next motion event if pen
    if(devinfo->type == XI2_MOUSE)
      xi2ReportMouseEvent(xiDeviceEvent, devinfo);
    else if(cookie->evtype == XI_Motion)
      xi2ReportTabletEvent(xiDeviceEvent, devinfo);
  }
}

static int reqClipboardText = 0;

void clipboardFromBuffer(const unsigned char* buff, size_t len, int is_image)
{
  if(!is_image)
    currClipboard.assign((const char*)buff, len);
  //ScribbleApp::app->insertImage(Image::decodeBuffer(buff, len), true);
}

// This should be moved to a separate file and std::vector used instead of our manual attempt
static void processClipboardXEvent(XEvent* xevent)
{
  static unsigned char* buff = NULL;
  static size_t cbuff = 0;
  static size_t nbuff = 0;

  Atom seln_type;
  int seln_format;
  unsigned long nbytes;
  unsigned long overflow;
  unsigned char* src;

  if(xevent->type == SelectionNotify) {
    Display* display = xevent->xselection.display;
    Window window = xevent->xselection.requestor;
    // if request for PNG failed, try text ... some applications (e.g. SDL!) don't copy requested target type
    //  to reply, so we use our own flag
    if(xevent->xselection.property == None && reqClipboardText)   //xevent->xselection.target == XAtoms.imagePng)
      XConvertSelection(display, XAtoms.clipboard, XAtoms.UTF8_STRING, XAtoms.sdlSel, window, CurrentTime);
    else if(xevent->xselection.property == XAtoms.sdlSel) {
      // delete property = True needed for INCR to send next chunk
      if(XGetWindowProperty(display, window, XAtoms.sdlSel, 0, INT_MAX/4, True,
          AnyPropertyType, &seln_type, &seln_format, &nbytes, &overflow, &src) == Success) {
        if(seln_type == XAtoms.Incr) {
          if(buff)
            free(buff);
          nbuff = 0;
          cbuff = 1<<20;
          buff = (unsigned char*)malloc(cbuff);
        }
        else if((seln_type == XAtoms.imagePng || seln_type == XAtoms.UTF8_STRING) && nbytes && src)
          clipboardFromBuffer(src, nbytes, seln_type == XAtoms.imagePng);  // does not require null term.
        XFree(src);
      }
    }
    reqClipboardText = 0;
  }
  else if(buff && xevent->type == PropertyNotify && xevent->xproperty.atom == XAtoms.sdlSel
      && xevent->xproperty.state == PropertyNewValue) {
    // this event preceeds SelectionNotify, so we need to ignore unless we get INCR so we don't delete
    //  property in non-INCR case
    Display* display = xevent->xproperty.display;
    Window window = xevent->xproperty.window;
    if(XGetWindowProperty(display, window, XAtoms.sdlSel, 0, INT_MAX/4, True,
        AnyPropertyType, &seln_type, &seln_format, &nbytes, &overflow, &src) == Success) {
      if(nbytes > 0) {
        if(nbytes + nbuff > cbuff) {
          cbuff = nbytes > cbuff ? cbuff + nbytes : 2*cbuff;
          buff = (unsigned char*)realloc(buff, cbuff);
        }
        memcpy(buff + nbuff, src, nbytes);
        nbuff += nbytes;
      }
      else if(nbuff) {
        clipboardFromBuffer(buff, nbuff, seln_type == XAtoms.imagePng);
        free(buff);
        buff = NULL;
        nbuff = 0;
        cbuff = 0;
      }
      XFree(src);
    }
  }
}

static int linuxInitTablet(Display* xDisplay, Window xWindow)
{
  XAtoms.clipboard = XInternAtom(xDisplay, "CLIPBOARD", 0);
  XAtoms.imagePng = XInternAtom(xDisplay, "image/png", 0);
  XAtoms.sdlSel = XInternAtom(xDisplay, "IMAGE_SELECTION", 0);
  XAtoms.Incr = XInternAtom(xDisplay, "INCR", 0);
  XAtoms.UTF8_STRING = XInternAtom(xDisplay, "UTF8_STRING", 0);
  XAtoms.STRING = XInternAtom(xDisplay, "STRING", 0);
  XAtoms.COMPOUND_TEXT = XInternAtom(xDisplay, "COMPOUND_TEXT", 0);
  XAtoms.C_STRING = XInternAtom(xDisplay, "C_STRING", 0);
  XAtoms.TARGETS = XInternAtom(xDisplay, "TARGETS", 0);

  int event, err;
  if(!XQueryExtension(xDisplay, "XInputExtension", &xinput2_opcode, &event, &err))
    return 0;

  XAtoms.absX = XInternAtom(xDisplay, "Abs X", 1);
  XAtoms.absY = XInternAtom(xDisplay, "Abs Y", 1);
  XAtoms.absP = XInternAtom(xDisplay, "Abs Pressure", 1);
  XAtoms.tiltX = XInternAtom(xDisplay, "Abs Tilt X", 1);
  XAtoms.tiltY = XInternAtom(xDisplay, "Abs Tilt Y", 1);

  enumerateDevices(xDisplay);

  // disable the events enabled by SDL
  XIEventMask eventmask;
  unsigned char mask[3] = { 0,0,0 };
  eventmask.deviceid = XIAllMasterDevices;
  eventmask.mask_len = sizeof(mask);
  eventmask.mask = mask;
  XISelectEvents(xDisplay, DefaultRootWindow(xDisplay), &eventmask, 1);  // != Success ...
  // ... and enable the events we want
  XISetMask(mask, XI_TouchBegin);
  XISetMask(mask, XI_TouchEnd);
  XISetMask(mask, XI_TouchUpdate);
  XISetMask(mask, XI_ButtonPress);
  XISetMask(mask, XI_ButtonRelease);
  XISetMask(mask, XI_Motion);
  XISelectEvents(xDisplay, xWindow, &eventmask, 1);

  return nTablets;
}

// read image from X11 clipboard
// Refs:
// * https://stackoverflow.com/questions/27378318/c-get-string-from-clipboard-on-linux/44992938#44992938
// * https://github.com/glfw/glfw/blob/master/src/x11_window.c

int requestClipboard(SDL_Window* sdlwin)
{
  Window owner = XGetSelectionOwner(xContext.dpy, XAtoms.clipboard);
  if(owner == None || owner == xContext.win)
    return 0;
  reqClipboardText = 1;
  // XConvertSelection is asynchronous - we have to wait for SelectionNotify message
  XConvertSelection(xContext.dpy, XAtoms.clipboard, XAtoms.imagePng, XAtoms.sdlSel, xContext.win, CurrentTime);
  return 1;
}

void processSelectionRequest(XSelectionRequestEvent* xse)
{
  Atom selAtoms[] = {XAtoms.TARGETS, XAtoms.UTF8_STRING, XAtoms.STRING, XAtoms.COMPOUND_TEXT, XAtoms.C_STRING};

  if(xse->property == None) { xse->property = xse->target; }

  XEvent nxe;
  nxe.xselection.type = SelectionNotify;
  nxe.xselection.requestor = xse->requestor;
  nxe.xselection.property = xse->property;
  nxe.xselection.display = xse->display;
  nxe.xselection.selection = xse->selection;
  nxe.xselection.target = xse->target;
  nxe.xselection.time = xse->time;

  if(xse->target == XAtoms.TARGETS) {
    XChangeProperty(xContext.dpy, xse->requestor, xse->property, XA_ATOM,
        32, PropModeReplace, (const uint8_t *)selAtoms, sizeof(selAtoms)/sizeof(selAtoms[0]));
  }
  else if(std::find(std::begin(selAtoms), std::end(selAtoms), xse->target) != std::end(selAtoms)) {
    if(xse->selection == XAtoms.clipboard) {  // XAtoms.PRIMARY
      XChangeProperty(xContext.dpy, xse->requestor, xse->property, xse->target,
          8, PropModeReplace, (const uint8_t*)currClipboard.c_str(), int(currClipboard.size()));
    }
  }
  else {
    nxe.xselection.property = None;  // not a supported target
  }
  XSendEvent(xContext.dpy, xse->requestor, 0, 0, &nxe);
  XFlush(xContext.dpy);
}

// Map X11 KeySym to SDL_Keycode
static SDL_Keycode keySymToSDLK(KeySym keysym)
{
  switch (keysym) {
  case XK_0: return SDLK_0;
  case XK_1: return SDLK_1;
  case XK_2: return SDLK_2;
  case XK_3: return SDLK_3;
  case XK_4: return SDLK_4;
  case XK_5: return SDLK_5;
  case XK_6: return SDLK_6;
  case XK_7: return SDLK_7;
  case XK_8: return SDLK_8;
  case XK_9: return SDLK_9;
  case XK_a: case XK_A: return SDLK_a;
  case XK_b: case XK_B: return SDLK_b;
  case XK_c: case XK_C: return SDLK_c;
  case XK_d: case XK_D: return SDLK_d;
  case XK_e: case XK_E: return SDLK_e;
  case XK_f: case XK_F: return SDLK_f;
  case XK_g: case XK_G: return SDLK_g;
  case XK_h: case XK_H: return SDLK_h;
  case XK_i: case XK_I: return SDLK_i;
  case XK_j: case XK_J: return SDLK_j;
  case XK_k: case XK_K: return SDLK_k;
  case XK_l: case XK_L: return SDLK_l;
  case XK_m: case XK_M: return SDLK_m;
  case XK_n: case XK_N: return SDLK_n;
  case XK_o: case XK_O: return SDLK_o;
  case XK_p: case XK_P: return SDLK_p;
  case XK_q: case XK_Q: return SDLK_q;
  case XK_r: case XK_R: return SDLK_r;
  case XK_s: case XK_S: return SDLK_s;
  case XK_t: case XK_T: return SDLK_t;
  case XK_u: case XK_U: return SDLK_u;
  case XK_v: case XK_V: return SDLK_v;
  case XK_w: case XK_W: return SDLK_w;
  case XK_x: case XK_X: return SDLK_x;
  case XK_y: case XK_Y: return SDLK_y;
  case XK_z: case XK_Z: return SDLK_z;
  case XK_Return: return SDLK_RETURN;
  case XK_space: return SDLK_SPACE;
  case XK_Escape: return SDLK_ESCAPE;
  case XK_Tab: return SDLK_TAB;
  case XK_BackSpace: return SDLK_BACKSPACE;
  case XK_Shift_L: return SDLK_LSHIFT;
  case XK_Shift_R: return SDLK_RSHIFT;
  case XK_Control_L: return SDLK_LCTRL;
  case XK_Control_R: return SDLK_RCTRL;
  case XK_Alt_L: return SDLK_LALT;
  case XK_Alt_R: return SDLK_RALT;
  case XK_Super_L: return SDLK_LGUI;
  case XK_Super_R: return SDLK_RGUI;
  case XK_Left: return SDLK_LEFT;
  case XK_Right: return SDLK_RIGHT;
  case XK_Up: return SDLK_UP;
  case XK_Down: return SDLK_DOWN;
  case XK_F1: return SDLK_F1;
  case XK_F2: return SDLK_F2;
  case XK_F3: return SDLK_F3;
  case XK_F4: return SDLK_F4;
  case XK_F5: return SDLK_F5;
  case XK_F6: return SDLK_F6;
  case XK_F7: return SDLK_F7;
  case XK_F8: return SDLK_F8;
  case XK_F9: return SDLK_F9;
  case XK_F10: return SDLK_F10;
  case XK_F11: return SDLK_F11;
  case XK_F12: return SDLK_F12;
  case XK_Delete: return SDLK_DELETE;
  case XK_Caps_Lock: return SDLK_CAPSLOCK;
  case XK_Home: return SDLK_HOME;
  case XK_End: return SDLK_END;
  case XK_Page_Up: return SDLK_PAGEUP;
  case XK_Page_Down: return SDLK_PAGEDOWN;
  case XK_Insert: return SDLK_INSERT;
  case XK_Print: return SDLK_PRINTSCREEN;
  default:
    // Try to handle printable ASCII range
    if (keysym >= XK_space && keysym <= XK_asciitilde)
        return (SDL_Keycode)keysym;
    return SDLK_UNKNOWN;
  }
}

static SDL_Keycode keycodeToSDLK[256] = {0};  // not necessary - static var defaults to zero init

static void initKeycodeToSDLK()
{
  int dummy;
  for(int keycode = 8; keycode < 256; ++keycode) {
    KeySym* keySyms = XGetKeyboardMapping(xContext.dpy, keycode, 1, &dummy);
    keycodeToSDLK[keycode] = keySymToSDLK(keySyms[0]);
    XFree(keySyms);
  }
}

static void updateKeymods(SDL_Keycode key, bool down)
{
  int mask = 0;
  if(key == SDLK_RSHIFT || key == SDLK_LSHIFT) { mask = KMOD_SHIFT; }
  else if(key == SDLK_RCTRL || key == SDLK_LCTRL) { mask = KMOD_CTRL; }
  else if(key == SDLK_RALT || key == SDLK_LALT) { mask = KMOD_ALT; }
  else if(key == SDLK_RGUI || key == SDLK_LGUI) { mask = KMOD_GUI; }
  else { return; }
  currKeymod = (SDL_Keymod)(down ? (currKeymod | mask) : (currKeymod & ~mask));
}

static struct {
  Atom WM_PROTOCOLS, WM_DELETE_WINDOW, NET_WM_PING;
} WMAtoms;

static void processX11Event(XEvent* xevent)
{
  KeySym keycode = 0;
  if(xevent->type == KeyPress || xevent->type == KeyRelease)
    keycode = xevent->xkey.keycode;

  bool filtered = XFilterEvent(xevent, None);

  switch (xevent->type) {
  case ClientMessage:
    if(xevent->xclient.message_type == WMAtoms.WM_PROTOCOLS) {
      const Atom protocol = xevent->xclient.data.l[0];
      if (protocol == WMAtoms.WM_DELETE_WINDOW) {
        SDL_Event event = {0};
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
      }
      else if(protocol == WMAtoms.NET_WM_PING) {
        XEvent reply = *xevent;
        reply.xclient.window = XDefaultRootWindow(xContext.dpy);
        XSendEvent(xContext.dpy, reply.xclient.window,
            False, SubstructureNotifyMask | SubstructureRedirectMask, &reply);
      }
    }
    break;
  case ConfigureNotify:
    if(xContext.width != xevent->xconfigure.width || xContext.height != xevent->xconfigure.height) {
      xContext.width = xevent->xconfigure.width;
      xContext.height = xevent->xconfigure.height;
      SDL_Event event = {0};
      event.type = SDL_WINDOWEVENT;
      event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
      event.window.data1 = xevent->xconfigure.width;
      event.window.data2 = xevent->xconfigure.height;
      SDL_PushEvent(&event);
    }
    break;
  case Expose:
  {
    SDL_Event event = {0};
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_EXPOSED;
    SDL_PushEvent(&event);
    break;
  }
  case FocusIn:
    requestClipboard(NULL);  // X11 clipboard request is async, so load immediately
  case FocusOut:
  {
    currKeymod = KMOD_NONE;  // reset mod state because we don't get key events when not focused
    SDL_Event event = {0};
    event.type = SDL_WINDOWEVENT;
    event.window.event = xevent->type == FocusIn ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
    SDL_PushEvent(&event);
    break;
  }
  case KeyPress:
  case KeyRelease:
  {
    SDL_Keycode sdlk = keycode > 0 && keycode < 256 ? keycodeToSDLK[keycode] : 0;
    bool down = xevent->type == KeyPress;
    updateKeymods(sdlk, down);  // xevent->xkey.state reflects state before this key event
    SDL_Event event = {0};
    event.key.type = !down ? SDL_KEYUP : SDL_KEYDOWN;
    event.key.state = !down ? SDL_RELEASED : SDL_PRESSED;
    //event.key.repeat = LOWORD(lParam);
    event.key.keysym.scancode = (SDL_Scancode)keycode;
    event.key.keysym.sym = sdlk;
    event.key.keysym.mod = currKeymod;
    event.key.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;
    SDL_PushEvent(&event);
    if (xevent->type != KeyPress || filtered) { break; }
    std::string buff(32, '\0');
    Status status;
    int count = Xutf8LookupString(xContext.ic, &xevent->xkey, (char*)buff.data(), buff.size(), NULL, &status);
    if(status == XBufferOverflow) {
      buff.resize(count + 1, '\0');
      count = Xutf8LookupString(xContext.ic, &xevent->xkey, (char*)buff.data(), buff.size(), NULL, &status);
    }
    if((status != XLookupChars && status != XLookupBoth) || buff[0] < 0x20 || buff[0] == 0x7F) { break; }
    // can't blindly split UTF-8 string, so just print error for now
    if(count > SDL_TEXTINPUTEVENT_TEXT_SIZE - 1)
      LOGE("Input string too long!!!");
    event = {0};
    event.text.type = SDL_TEXTINPUT;
    event.text.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;
    char* out = (char*)event.text.text;
    int nbytes = std::min(size_t(SDL_TEXTINPUTEVENT_TEXT_SIZE - 1), buff.size());
    strncpy(out, buff.c_str(), nbytes);
    out[nbytes] = '\0';
    SDL_PushEvent(&event);
    break;
  }
  case GenericEvent:
  {
    XGenericEventCookie* cookie = &xevent->xcookie;
    XGetEventData(cookie->display, cookie);
    if(cookie->data) {
      if(cookie->extension == xinput2_opcode)
        processXinput2Event(cookie);
      XFreeEventData(cookie->display, cookie);
    }
    break;
  }
  case SelectionRequest:
    processSelectionRequest(&xevent->xselectionrequest);
    break;
  case SelectionNotify:
  case PropertyNotify:
    processClipboardXEvent(xevent);
    break;
  }
}

/*
// spent more time trying to get xwd, maim, scrot, etc. working than it took to write this
static void screenshotPng(int width, int height)
{
  Image img(width, height);
  glReadBuffer(GL_FRONT);  // to get MSAA resolve ... in general reading front buffer not guaranteed to work
  // GL_RGB, not GL_RGBA (alpha channel not allowed in screenshots)
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, img.pixels());
  glReadBuffer(GL_BACK);
  auto pngname = std::to_string(mSecSinceEpoch()/1000) + ".png";
  stbi_flip_vertically_on_write(1);
  // initial value of GL_PACK_ALIGNMENT is 4
  stbi_write_png(pngname.c_str(), width, height, 3, img.pixels(), (3*width + 0x3) & ~0x3);
  stbi_flip_vertically_on_write(0);
  //FileStream fstrm(pngname.c_str(), "wb");
  //auto encoded = img.encodePNG();
  //fstrm.write((char*)encoded.data(), encoded.size());
  PLATFORM_LOG("Screenshot written to %s\n", pngname.c_str());
}
*/

static void initBaseDir(const char* exepath)
{
  if(exepath)
    Application::appDir = canonicalPath(FSPath(exepath).parentPath());
  else
    Application::appDir = canonicalPath("./");
  /*
  if(!FSPath(Application::appDir, "config.default.yaml").exists()) {
    if(exepath)
      MapsApp::baseDir = canonicalPath(FSPath(exepath).parentPath());
    if(!FSPath(MapsApp::baseDir, "config.default.yaml").exists())
      MapsApp::baseDir = canonicalPath(FSPath(MapsApp::baseDir, "../../assets/"));
  }

  // if config.yaml already exists in base dir, use that
  if(FSPath(MapsApp::baseDir, "config.yaml").exists()) { return; }
  //if(FSPath(MapsApp::baseDir, "config.default.yaml").exists("rb+")) { return; }

  // use $HOME/.config/ascend
  const char* env_home = getenv("HOME");
  if(!env_home || !env_home[0]) { return; }
  FSPath xdgcfg(env_home, ".config/Ascend/");
  if(!createPath(xdgcfg)) { return; }
  if(!xdgcfg.child("config.default.yaml").exists()) {
    FSPath base(MapsApp::baseDir);
    // hardcoding resources to symlink is obviously not ideal...
    const char* tolink[] = {"config.default.yaml", "mapsources.default.yaml", "plugins", "res", "scenes", "shared"};
    for(const char* l : tolink) {
      if(symlink(base.child(l).c_str(), xdgcfg.child(l).c_str()) != 0) { return; }  // abort on error
    }
  }
  MapsApp::baseDir = xdgcfg.path;
  */
}

// If WM_CLASS is set correctly, launcher icon specified in .desktop file will be used (as desired)
/*
static void setWinIcon(Display* xDpy, Window xWin, const char* iconpath)
{
  int width = 0, height = 0, nchannels = 0;
  uint32_t* pixels = (uint32_t*)stbi_load(iconpath, &width, &height, &nchannels, 4);
  if(!pixels) { PLATFORM_LOG("Error loading icon file %s\n", iconpath); return; }
  std::vector<unsigned long> netWmIcon(width * height + 2);
  netWmIcon[0] = width;
  netWmIcon[1] = height;
  for(int ii = 0; ii < width * height; ++ii) { netWmIcon[ii+2] = Color::swapRB(pixels[ii]); }
  stbi_image_free(pixels);
  XChangeProperty(xDpy, xWin, XInternAtom(xDpy, "_NET_WM_ICON", False), XInternAtom(xDpy, "CARDINAL", False),
      32, PropModeReplace, (unsigned char *)netWmIcon.data(), netWmIcon.size());
}
*/

static XImage* createShmImage(Display *dpy, Visual *vis, int depth, int width, int height, XShmSegmentInfo *shminfo)
{
  XImage *img = XShmCreateImage(dpy, vis, depth, ZPixmap, NULL, shminfo, width, height);
  shminfo->shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0777);
  shminfo->shmaddr = img->data = (char*)shmat(shminfo->shmid, 0, 0);
  shminfo->readOnly = False;
  XShmAttach(dpy, shminfo);
  return img;
}

static void deleteShmImage(Display *dpy, XImage *img, XShmSegmentInfo *shminfo)
{
  if(!img) { return; }
  XShmDetach(dpy, shminfo);
  shmdt(shminfo->shmaddr);
  shmctl(shminfo->shmid, IPC_RMID, 0);
  XDestroyImage(img);
}

// Refs:
// - https://hereket.com/posts/x11_window_with_shaders/
// - https://www.khronos.org/opengl/wiki/Tutorial:_OpenGL_3.0_Context_Creation_(GLX)
// - https://gist.github.com/baines/5a49f1334281b2685af5dcae81a6fa8a - XIM, XIC
// - https://github.com/blender/blender/blob/main/intern/ghost/intern/GHOST_SystemX11.cc

static int glxFbAttribs[] = {
  GLX_X_RENDERABLE    , True,
  GLX_DOUBLEBUFFER    , True,
  GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
  GLX_RENDER_TYPE     , GLX_RGBA_BIT,
  GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
  GLX_RED_SIZE        , 8,
  GLX_GREEN_SIZE      , 8,
  GLX_BLUE_SIZE       , 8,
#ifndef MAPS_TANGRAM_FBO
  GLX_ALPHA_SIZE      , 8,
  GLX_DEPTH_SIZE      , 24,
  GLX_STENCIL_SIZE    , 8,
  GLX_SAMPLE_BUFFERS  , True,
  GLX_SAMPLES         , 4,
#endif
  None
};

static int glxCtxAttribs[] = {
  GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
  GLX_CONTEXT_MINOR_VERSION_ARB, 3,
  //GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_ES2_PROFILE_BIT_EXT, // GLX_CONTEXT_CORE_PROFILE_BIT_ARB
  None
};

bool Application::processEvents()
{
  do {
    XEvent xevent = {};
    XNextEvent(xContext.dpy, &xevent);
    processX11Event(&xevent);
  } while(runApplication && XPending(xContext.dpy));

  return runApplication;
}

static XShmSegmentInfo shminfo;

void Application::drawFrame()
{
  int w = xContext.width, h = xContext.height;
  if(Application::glRender) {
    if(Application::layoutAndDraw(w, h)) //app->drawFrame(xContext.width, xContext.height))
      glXSwapBuffers(xContext.dpy, xContext.win);
  }
  else {
    if(!xContext.img || xContext.img->width != w || xContext.img->height != h) {
      if(xContext.img) { deleteShmImage(xContext.dpy, xContext.img, &shminfo); }
      int depth = DefaultDepth(xContext.dpy, DefaultScreen(xContext.dpy));
      xContext.img = createShmImage(xContext.dpy, xContext.vis, depth, w, h, &shminfo);
      setSWFramebuffer(xContext.img->data, w, h, 16, 8, 0, 24);  //nvgswSetFramebuffer(Application::painter->vg,
    }
    if(Application::layoutAndDraw(w, h)) {
      XShmPutImage(xContext.dpy, xContext.win, xContext.gc, xContext.img, 0, 0, 0, 0, w, h, False);
      XSync(xContext.dpy, False);
    }
  }

}

extern int SDL_main(int argc, char* argv[]);

int main(int argc, char* argv[])
{
  initBaseDir(argc > 0 ? argv[0] : NULL);
  /*
  MapsApp::loadConfig("");

  // command line args
  std::string sceneFile, importFile;  // -f scenes/scene-omt.yaml
  for(int argi = 1; argi < argc-1; argi += 2) {
    YAML::Node* node = NULL;
    if(strncmp(argv[argi], "--", 2) == 0 &&
        (node = Tangram::YamlPath(std::string("+") + (argv[argi] + 2)).get(MapsApp::config))) {
      *node = argv[argi+1];
    }
    else
      LOGE("Unknown command line argument: %s", argv[argi]);
  }
  */

  XInitThreads();  // seems this might be required due to glXMakeCurrent for offscreen worker
  // window setup
  Display* xDpy = XOpenDisplay(0);
  xContext.dpy = xDpy;
  int mainScr = XDefaultScreen(xDpy);
  Window rootWin = XDefaultRootWindow(xDpy);

  Screen* scrInfo = DefaultScreenOfDisplay(xDpy);
  //float inches = scrInfo->mwidth/25.4;
  //float dpi = inches > 2 && inches < 240 ? scrInfo->width/inches : scrInfo->width/11.2f;
  float dpi = scrInfo->width/12.0f;  // ignore physical size of screen - larger screen is typically further away

  XSizeHints winSize = {};
  winSize.flags = PPosition | PSize;
  winSize.x = 0; winSize.y = 0;
  winSize.width = scrInfo->width/2;
  winSize.height = int(0.9f*scrInfo->height);
  /*
  const YAML::Node& posYaml = MapsApp::cfg()["ui"]["position"];
  if(posYaml.size() == 4) {
    winSize.x = posYaml[0].as<int>(0);
    winSize.y = posYaml[1].as<int>(0);
    winSize.width = posYaml[2].as<int>(winSize.width);
    winSize.height = posYaml[3].as<int>(winSize.height);
  }
  */

  if(!glXQueryExtension(xDpy, NULL, NULL)) { LOGE("glXQueryExtension() failed."); return -1; }

  int fbcount = 0;
  GLXFBConfig* fbConfigs = glXChooseFBConfig(xDpy, mainScr, glxFbAttribs, &fbcount);  //glXChooseVisual
  if(!fbConfigs) { LOGE("glXChooseFBConfig() failed"); return -1; }
  XVisualInfo* xVisual = glXGetVisualFromFBConfig(xDpy, fbConfigs[0]);
  if(!xVisual) { LOGE("glXGetVisualFromFBConfig() failed"); return -1; }

  XSetWindowAttributes winSetAttrs = {};
  winSetAttrs.colormap = XCreateColormap(xDpy, rootWin, xVisual->visual, AllocNone);
  winSetAttrs.background_pixel = scrInfo->black_pixel;
  winSetAttrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                           EnterWindowMask | LeaveWindowMask | ButtonPressMask |
                           ButtonReleaseMask | PointerMotionMask | FocusChangeMask |
                           PropertyChangeMask | KeymapStateMask;
  Window xWin = XCreateWindow(xDpy, rootWin, 0, 0, winSize.width, winSize.height, 0,
      xVisual->depth, InputOutput, xVisual->visual, CWBackPixel | CWEventMask | CWColormap, &winSetAttrs);
  xContext.win = xWin;

  // set WM_CLASS so window is associated with correct launcher icon
  const char* res_name = "Ascend";
  const char* res_class = "Ascend";
  XClassHint classHint;
  classHint.res_name = (char*)res_name;  // don't cast string directly to avoid warning
  classHint.res_class = (char*)res_class;
  XSetClassHint(xDpy, xWin, &classHint);

  // set _NET_WM_PID and _NET_WM_WINDOW_TYPE
  pid_t pid = getpid();
  XChangeProperty(xDpy, xWin, XInternAtom(xDpy, "_NET_WM_PID", False),
      XInternAtom(xDpy, "CARDINAL", False), 32, PropModeReplace, (unsigned char *)&pid, 1);
  Atom NET_WM_WINDOW_TYPE_NORMAL = XInternAtom(xDpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
  XChangeProperty(xDpy, xWin, XInternAtom(xDpy, "_NET_WM_WINDOW_TYPE", False),
      XA_ATOM, 32, PropModeReplace, (unsigned char *)&NET_WM_WINDOW_TYPE_NORMAL, 1);

  // set WM_NAME (window title)
  //XStoreName(xDpy, xWin, "Ascend Maps");
  XSetStandardProperties(xDpy, xWin, "Ascend Maps", "Ascend Maps", None, NULL, 0, &winSize);
  //setWinIcon(xDpy, xWin, FSPath(MapsApp::baseDir, "shared/icons/app144x144.png").c_str());
  XMapWindow(xDpy, xWin);

  WMAtoms.WM_PROTOCOLS = XInternAtom(xDpy, "WM_PROTOCOLS", False);
  WMAtoms.NET_WM_PING = XInternAtom(xDpy, "_NET_WM_PING", False);
  WMAtoms.WM_DELETE_WINDOW = XInternAtom(xDpy, "WM_DELETE_WINDOW", False);
  if(!XSetWMProtocols(xDpy, xWin, &WMAtoms.WM_DELETE_WINDOW, 1))
    LOGE("Couldn't register WM_DELETE_WINDOW\n");

  // don't bother with GLAD GLX loader for just a few fns
  auto glXCreateContextAttribsARB =
      (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddress((const GLubyte*)"glXCreateContextAttribsARB");
  auto glXSwapIntervalEXT =
      (PFNGLXSWAPINTERVALEXTPROC) glXGetProcAddress((const GLubyte*)"glXSwapIntervalEXT");
  if(!glXCreateContextAttribsARB || !glXSwapIntervalEXT) { LOGE("glXGetProcAddress() failed"); return -1; }

  GLXContext mainCtx = glXCreateContextAttribsARB(xDpy, fbConfigs[0], NULL, true, glxCtxAttribs);
  //GLXContext mainCtx = glXCreateContext(xDpy, xVisual, None, True);
  if(!mainCtx) { LOGE("glXCreateContextAttribsARB() failed"); return -1; }

  //  GLXContext auxCtx = glXCreateContextAttribsARB(xDpy, fbConfigs[0], mainCtx, true, glxCtxAttribs);
  //  if(!auxCtx) { LOGE("glXCreateContext() failed for offscreen context"); return -1; }
  //  auto offscreenWorker = std::make_unique<Tangram::AsyncWorker>("Ascend offscreen GL worker");
  //  offscreenWorker->enqueue([=](){ glXMakeCurrent(xDpy, xWin, auxCtx); });
  //  Tangram::ElevationManager::offscreenWorker = std::move(offscreenWorker);
  glXMakeCurrent(xDpy, xWin, mainCtx);
  //gladLoadGL();
  const char* glXexts = glXQueryExtensionsString(xDpy, DefaultScreen(xDpy));

  /*
  int dfltswap = strstr(glXexts, "GLX_EXT_swap_control_tear") ? -1 : 1;
  glXSwapIntervalEXT(xDpy, xWin, MapsApp::cfg()["gl_swap_interval"].as<int>(dfltswap));
  */

  // setup input context for keyboard input
  XSetLocaleModifiers("");
  XIM xIM = XOpenIM(xDpy, 0, 0, 0);
  if(!xIM) {
    XSetLocaleModifiers("@im=none");  // fallback to internal input method
    xIM = XOpenIM(xDpy, 0, 0, 0);
  }
  xContext.ic = XCreateIC(xIM, XNInputStyle,
      XIMPreeditNothing | XIMStatusNothing, XNClientWindow, xWin, XNFocusWindow, xWin, NULL);
  XSetICFocus(xContext.ic);

  initKeycodeToSDLK();

  // mouse/pen/touch input setup
  linuxInitTablet(xDpy, xWin);
/*
  // file dialog setup
  if(NFD_Init() != NFD_OKAY)
    LOGE("NFD_Init error: %s", NFD_GetError());
*/
  // XGetWindowAttributes is slow, so don't call every frame
  XWindowAttributes winAttrs;
  XGetWindowAttributes(xDpy, xWin, &winAttrs);
  xContext.width = winAttrs.width;
  xContext.height = winAttrs.height;

  Application::sdlWindow = &xContext;

  // SW rendering
  GC xGC = xContext.gc = DefaultGC(xDpy, mainScr);
  Visual* xVis = xContext.vis = DefaultVisual(xDpy, mainScr);
  //int depth = DefaultDepth(xDpy, mainScr);
  //XShmSegmentInfo shminfo;
  //XImage* xImg = NULL;

  /*
  // MapsApp setup
  MapsApp* app = new MapsApp(new Tangram::LinuxPlatform());
  app->setDpi(dpi);
  //app->map->setupGL();
  app->createGUI((SDL_Window*)xWin);

  static bool takeScreenshot = false;
  app->win->addHandler([&](SvgGui*, SDL_Event* event){
    if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_q && event->key.keysym.mod & KMOD_CTRL)
        MapsApp::runApplication = false;
      else if(event->key.keysym.sym == SDLK_F5) {
        if((event->key.keysym.mod & KMOD_CTRL) && (event->key.keysym.mod & KMOD_SHIFT)) {
          malloc_trim(0);
          PLATFORM_LOG("Ran malloc_trim(0)\n");
        }
        else {
          app->mapsSources->reload();
          app->pluginManager->reload();
          app->loadSceneFile();  // reload scene
        }
      }
      else if(event->key.keysym.sym == SDLK_PRINTSCREEN) {
        // testing/debugging stuff
        if((event->key.keysym.mod & KMOD_CTRL) && (event->key.keysym.mod & KMOD_SHIFT))
          MapsApp::gui->pushUserEvent(SvgGui::KEYBOARD_HIDDEN, 0);  // can't use a menu item for this obviously
        else if(event->key.keysym.mod & KMOD_CTRL)
          SvgGui::debugDirty = !SvgGui::debugDirty;
        //else if(event->key.keysym.mod & KMOD_ALT)
        //  debugHovered = true;
        else if(event->key.keysym.mod & KMOD_SHIFT) {
          takeScreenshot = true;
          MapsApp::platform->requestRender();
        }
        else
          SvgGui::debugLayout = true;
      }
      else
        return false;
    }
    else if(event->type == SDL_QUIT)
      MapsApp::runApplication = false;
    else
      return false;
    return true;
  });

  app->mapsOffline->resumeDownloads();

  app->mapsSources->rebuildSource(app->config["sources"]["last_source"].Scalar());

#ifndef NDEBUG
  app->updateLocation(Location{0, 37.777, -122.434, 0, 100, 0, NAN, 0, NAN, 0});
  app->updateOrientation(0, M_PI/2, 0, 0);
#endif
  app->updateGpsStatus(10, 10);  // turn location maker blue
*/

  // main loop
  /*while(Application::runApplication) {

    do {
      XEvent xevent = {};
      XNextEvent(xDpy, &xevent);
      processX11Event(&xevent);
    } while(XPending(xDpy));

    if(Application::glRender) {
      if(Application::layoutAndDraw(xContext.width, xContext.height)) //app->drawFrame(xContext.width, xContext.height))
        glXSwapBuffers(xDpy, xWin);
    }
    else {
      if(!xImg || xImg->width != xContext.width || xImg->height != xContext.height) {
        if(xImg) { deleteShmImage(xDpy, xImg, &shminfo); }
        xImg = createShmImage(xDpy, xVis, depth, xContext.width, xContext.height, &shminfo);
        nvgswSetFramebuffer(Application::painter->vg, xImg->data, xContext.width, xContext.height, 0, 8, 16, 24);
      }
      if(Application::layoutAndDraw(xContext.width, xContext.height)) {
        XShmPutImage(xDpy, xWin, xGC, xImg, 0, 0, 0, 0, xImg->width, xImg->height, False);
        XSync(xDpy, False);
      }
    }


//    if(takeScreenshot) {
//      screenshotPng(xContext.width, xContext.height);
//      takeScreenshot = false;
//    }
//    if(SvgGui::debugLayout) {
//      FileStream strm("debug_layout.svg", "wb");
//      SvgWriter::DEBUG_CSS_STYLE = true;
//      SvgWriter::save(app->win->modalOrSelf()->documentNode(), strm);
//      SvgWriter::DEBUG_CSS_STYLE = false;
//      PLATFORM_LOG("Post-layout SVG written to debug_layout.svg\n");
//      SvgGui::debugLayout = false;
//    }
  } */

/*
  // save window size
  XGetWindowAttributes(xDpy, xWin, &winAttrs);
  app->config["ui"]["position"] = YAML::Array({winAttrs.x, winAttrs.y, winAttrs.width, winAttrs.height});  // YAML::Tag::YAML_FLOW

  app->onSuspend();
  delete app;
  NFD_Quit();
*/

  int res = SDL_main(argc, argv);

  glXMakeCurrent(xDpy, None, NULL);
  //  offscreenWorker = std::move(Tangram::ElevationManager::offscreenWorker);
  //  if(offscreenWorker) {
  //    offscreenWorker->enqueue([=](){ glXMakeCurrent(xDpy, None, NULL); });
  //    offscreenWorker->waitForCompletion();
  //    offscreenWorker.reset();  // wait for thread exit
  //  }

  if(xContext.img)
    deleteShmImage(xContext.dpy, xContext.img, &shminfo);

  XCloseDisplay(xDpy);

  return res;
}
