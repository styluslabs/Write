#include <string.h>
#include <ctype.h>  // for tolower
#include <X11/extensions/XInput2.h>

#ifdef XINPUT2_TEST
#define PLATFORM_LOG printf
void clipboardFromBuffer(const unsigned char* buff, size_t len, int is_image) {}
#define PenPointerPen 1
#define PenPointerEraser 2
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#else
#include "ugui/svggui_platform.h"
#include <SDL_syswm.h>
#endif

#include <limits.h>

// in ScribbleApp
extern void clipboardFromBuffer(const unsigned char* buff, size_t len, int is_image);

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

static struct {
  Atom absX, absY, absP, tiltX, tiltY, clipboard, imagePng, sdlSel, Incr, utf8String;
} XAtoms;

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
  unsigned int button = device->buttons ^ device->prevButtons;
  unsigned int eventtype = SDL_FINGERMOTION;
  if(device->buttons != device->prevButtons)
    eventtype = (device->buttons & button) ? SDL_FINGERDOWN : SDL_FINGERUP;
  device->prevButtons = device->buttons;

  SDL_Event event = {0};
  event.type = eventtype;
  event.tfinger.timestamp = SDL_GetTicks(); // normally done by SDL_PushEvent()
  event.tfinger.touchId = SDL_TOUCH_MOUSEID;
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
      XConvertSelection(display, XAtoms.clipboard, XAtoms.utf8String, XAtoms.sdlSel, window, CurrentTime);
    else if(xevent->xselection.property == XAtoms.sdlSel) {
      // delete property = True needed for INCR to send next chunk
      if(XGetWindowProperty(display, window, XAtoms.sdlSel, 0, INT_MAX/4, True,
          AnyPropertyType, &seln_type, &seln_format, &nbytes, &overflow, &src) == Success) {
        if(seln_type == XAtoms.Incr) {
          if(buff)
            free(buff);
          nbuff = 0;
          cbuff = 1<<20;
          buff = malloc(cbuff);
        }
        else if((seln_type == XAtoms.imagePng || seln_type == XAtoms.utf8String) && nbytes && src)
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
          buff = realloc(buff, cbuff);
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

void linuxProcessXEvent(SDL_Event* event)
{
  XEvent* xevent = &event->syswm.msg->msg.x11.event;
  if(xevent->type == GenericEvent) {
    XGenericEventCookie* cookie = &xevent->xcookie;
    // SDL doesn't actually call XGetEventData until after syswm event, but it seems the data is already
    //  there and calling XGetEventData after XFreeEventData can crash (and second XGetEventData call returns
    //  false, so SDL won't call XFreeEventData)
    //XGetEventData(cookie->display, cookie);
    if(cookie->data) {
      if(cookie->extension == xinput2_opcode)
        processXinput2Event(cookie);
      //XFreeEventData(cookie->display, cookie);
    }
  }
  else
    processClipboardXEvent(xevent);
}

int linuxInitTablet(SDL_Window* sdlwin)
{
  SDL_SysWMinfo wmInfo;
  SDL_VERSION(&wmInfo.version)
  if(!SDL_GetWindowWMInfo(sdlwin, &wmInfo))
    return 0;
  Display* xDisplay = wmInfo.info.x11.display;
  Window xWindow = wmInfo.info.x11.window;

  XAtoms.clipboard = XInternAtom(xDisplay, "CLIPBOARD", 0);
  XAtoms.imagePng = XInternAtom(xDisplay, "image/png", 0);
  XAtoms.sdlSel = XInternAtom(xDisplay, "IMAGE_SELECTION", 0);
  XAtoms.Incr = XInternAtom(xDisplay, "INCR", 0);
  XAtoms.utf8String = XInternAtom(xDisplay, "UTF8_STRING", 0);

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

  // we handle all finger/pen/mouse input events
  SDL_EventState(SDL_FINGERDOWN, SDL_DISABLE);
  SDL_EventState(SDL_FINGERMOTION, SDL_DISABLE);
  SDL_EventState(SDL_FINGERUP, SDL_DISABLE);
  // SDL generates mouse events from MotionNotify, ButtonPress, ButtonRelease, not Xinput2 events, so this
  //  is the only way to block ... actually, I think enabling the XI_Button* blocks them!
  SDL_EventState(SDL_MOUSEMOTION, SDL_DISABLE);
  SDL_EventState(SDL_MOUSEBUTTONUP, SDL_DISABLE);
  SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_DISABLE);
  SDL_EventState(SDL_MOUSEWHEEL, SDL_DISABLE);  // X11 sends mouse button events for mouse wheel
  return nTablets;
}

#ifdef XINPUT2_TEST
// gcc -DXINPUT2_TEST -o xitest linuxtablet.c -lSDL2 -lX11 -lXi
// refs: /usr/include/X11/extensions/XInput2.h, XI2.h
// Testing events:
//  sudo evemu-record and xinput test-xi2 --root <id> - https://sourceforge.net/p/linuxwacom/bugs/334/

static int sdlEventFilter(void* app, SDL_Event* event)
{
  if(event->type == SDL_SYSWMEVENT) {
    linuxProcessXEvent(event);
    return 0;  // no further processing
  }
  return 1;
}

int main(int argc, char* argv[])
{
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window* sdlWindow = SDL_CreateWindow("xitest", 100, 100, 800, 800,
      SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);

  linuxInitTablet(sdlWindow);
  SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);  // linuxtablet.c handles touch events even if no pen
  SDL_SetEventFilter(sdlEventFilter, NULL);

  SDL_Event event;
  do {
    SDL_WaitEvent(&event);
    if(event.type == SDL_FINGERDOWN)
      PLATFORM_LOG("SDL Finger down; fingerId/button: %d\n", (int)event.tfinger.fingerId);
    else if(event.type == SDL_MOUSEBUTTONDOWN)
      PLATFORM_LOG("SDL Mouse down; which: %d!\n", event.button.which);
  } while(event.type != SDL_QUIT);

  SDL_Quit();
  return 0;
}
#endif

// evdev tablet code removed 10 July 2020

// read image from X11 clipboard
// Refs:
// * https://stackoverflow.com/questions/27378318/c-get-string-from-clipboard-on-linux/44992938#44992938
// * https://github.com/glfw/glfw/blob/master/src/x11_window.c

int requestClipboard(SDL_Window* sdlwin)
{
  SDL_SysWMinfo wmInfo;
  SDL_VERSION(&wmInfo.version)
  if(!SDL_GetWindowWMInfo(sdlwin, &wmInfo))
    return 0;
  Display* xDisplay = wmInfo.info.x11.display;
  Window xWindow = wmInfo.info.x11.window;

  Window owner = XGetSelectionOwner(xDisplay, XAtoms.clipboard);
  if(owner == None || owner == xWindow)
    return 0;

  reqClipboardText = 1;
  // XConvertSelection is asynchronous - we have to wait for SelectionNotify message
  XConvertSelection(xDisplay, XAtoms.clipboard, XAtoms.imagePng, XAtoms.sdlSel, xWindow, CurrentTime);
  return 1;
}
