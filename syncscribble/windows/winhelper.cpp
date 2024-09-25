#include "winhelper.h"
//#include "wintab/wmpointer.h" ... this was to support *compilation* on Win 7 and earlier
#include "basics.h"


#define USE_WINTAB 1

// always link WM_POINTER fns at runtime so we support *running* on Win 7 and earlier
#ifndef WMPOINTER_NEEDED
// use different names for fns if already defined
#define GetPointerInfo FnGetPointerInfo
#define GetPointerFrameInfo FnGetPointerFrameInfo
#define GetPointerPenInfo FnGetPointerPenInfo
#define GetPointerPenInfoHistory FnGetPointerPenInfoHistory
#define GetPointerDeviceRects FnGetPointerDeviceRects
#define InjectTouchInput FnInjectTouchInput
#define InitializeTouchInjection FnInitializeTouchInjection
#define WMPOINTER_NEEDED 1
#endif

#ifdef WMPOINTER_NEEDED
// WM_POINTER functions
typedef BOOL (WINAPI *PtrGetPointerInfo)(UINT32, POINTER_INFO*);
typedef BOOL (WINAPI *PtrGetPointerFrameInfo)(UINT32, UINT32*, POINTER_INFO*);
typedef BOOL (WINAPI *PtrGetPointerPenInfo)(UINT32, POINTER_PEN_INFO*);
typedef BOOL (WINAPI *PtrGetPointerPenInfoHistory)(UINT32, UINT32*, POINTER_PEN_INFO*);
typedef BOOL (WINAPI *PtrGetPointerDeviceRects)(HANDLE, RECT*, RECT*);
typedef BOOL (WINAPI *PtrInjectTouchInput)(UINT32, POINTER_TOUCH_INFO*);
typedef BOOL (WINAPI *PtrInitializeTouchInjection)(UINT32, DWORD);

static PtrGetPointerInfo GetPointerInfo = NULL;
static PtrGetPointerFrameInfo GetPointerFrameInfo;
static PtrGetPointerPenInfo GetPointerPenInfo;
static PtrGetPointerPenInfoHistory GetPointerPenInfoHistory;
static PtrGetPointerDeviceRects GetPointerDeviceRects;
static PtrInjectTouchInput InjectTouchInput;
static PtrInitializeTouchInjection InitializeTouchInjection;
#endif

#define MAX_N_POINTERS 10
static POINTER_INFO pointerInfo[MAX_N_POINTERS];
static POINTER_PEN_INFO penPointerInfo[MAX_N_POINTERS];
static bool winTabProximity = false;

enum TouchPointState { TouchPointPressed = SDL_FINGERDOWN, TouchPointMoved = SDL_FINGERMOTION, TouchPointReleased = SDL_FINGERUP };
enum PenEventType { TabletPress = SDL_FINGERDOWN, TabletMove = SDL_FINGERMOTION, TabletRelease = SDL_FINGERUP };

struct TouchPoint
{
  Point screenPos;
  Dim pressure;
  TouchPointState state;
  uint32_t id;
};

static Rect clientRect;

static void updateClientRect(HWND hwnd)
{
  // same code that SDL uses for WM_TOUCH, so hopefully it will work
  RECT rect;
  if(!GetClientRect(hwnd, &rect) || (rect.right == rect.left && rect.bottom == rect.top))
    return;
  ClientToScreen(hwnd, (LPPOINT)&rect);
  ClientToScreen(hwnd, (LPPOINT)&rect + 1);
  clientRect = Rect::ltrb(rect.left, rect.top, rect.right, rect.bottom);
}

static void notifyTabletEvent(PenEventType eventtype, const Point& globalpos,
    Dim pressure, Dim tiltX, Dim tiltY, bool eraser, int button, int buttons, int deviceid, uint32_t t)
{
  // combine all extra buttons into RightButton
  //if (button & ~0x1) button = (button & 0x1) | 0x2;  if (buttons & ~0x1) buttons = (buttons & 0x1) | 0x2;

  SDL_Event event;
  SDL_zero(event);
  event.type = eventtype;
  event.tfinger.timestamp = t;  // SDL_GetTicks();  // normally done by SDL_PushEvent()
  event.tfinger.touchId = eraser ? PenPointerEraser : PenPointerPen;
  //event.tfinger.fingerId = 0;  // if SDL sees more than one finger id for a touch id, that's multitouch
  // POINTER_FLAGS >> 4 == SDL_BUTTON_LMASK for pen down w/ no barrel buttons pressed, as desired
  event.tfinger.fingerId = eventtype == SDL_FINGERMOTION ? buttons : button;
  event.tfinger.x = (globalpos.x - clientRect.left);  // / clientRect.width();
  event.tfinger.y = (globalpos.y - clientRect.top);  // / clientRect.height();
  // stick buttons in dx, dy for now
  event.tfinger.dx = tiltX;
  event.tfinger.dy = tiltY;
  event.tfinger.pressure = pressure;
  // PeepEvents bypasses gesture recognizer and event filters
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
}

static void notifyTouchEvent(TouchPointState touchstate, const std::vector<TouchPoint>& _points, uint32_t t)
{
  for (const TouchPoint& p : _points) {
    SDL_Event event;
    SDL_zero(event);
    event.type = p.state;
    event.tfinger.timestamp = t;  // SDL_GetTicks();  // normally done by SDL_PushEvent()
    event.tfinger.touchId = 0;
    event.tfinger.fingerId = p.id;
    event.tfinger.x = (p.screenPos.x - clientRect.left);  // /clientRect.width();
    event.tfinger.y = (p.screenPos.y - clientRect.top);  // /clientRect.height();
    event.tfinger.pressure = p.pressure;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
  }
}

#ifdef USE_WINTAB
#include "wintab/utils.h"
#include "wintab/msgpack.h"

// these defines are required for pktdef.h
#define PACKETDATA (PK_X | PK_Y | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_CURSOR)
//#define PACKETDATA (PK_X | PK_Y | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_TANGENT_PRESSURE | PK_ORIENTATION | PK_CURSOR | PK_Z | PK_TIME)
#define PACKETMODE 0
#include "wintab/pktdef.h"

static constexpr int PACKET_BUFF_SIZE = 128;
static PACKET localPacketBuf[PACKET_BUFF_SIZE];
static HCTX hTab = NULL;

static Rect tabletArea;
static Rect desktopArea;
static int minPressure = 0, maxPressure = 0;
static DWORD btnPrev = 0;
//static SDL_Window* sdlWindow = NULL;

static void initWinTab(HWND hWnd)
{
  // attempt to load wintab
  if(!LoadWintab() || !gpWTInfoA || !gpWTInfoA(0, 0, NULL) || !gpWTPacketsGet)
    return;

  LOGCONTEXTA lc;
  // WTI_DEFSYSCTX has CXO_SYSTEM set to enable as "system cursor context" so pen moves cursor (WTI_DEFCONTEXT doesn't)
  gpWTInfoA(WTI_DEFSYSCTX, 0, &lc);

  lc.lcOptions |= CXO_MESSAGES;
  lc.lcPktData = PACKETDATA;
  lc.lcMoveMask = PACKETDATA;
  lc.lcPktMode = PACKETMODE;
  lc.lcBtnUpMask = lc.lcBtnDnMask;

  // set output range to input range to get full available precision (default output range is screen resolution)
  lc.lcOutOrgX = 0;
  lc.lcOutExtX = lc.lcInExtX;
  lc.lcOutOrgY = 0;
  lc.lcOutExtY = -lc.lcInExtY;  // invert Y axis to match screen coords
  tabletArea = Rect::ltwh(lc.lcOutOrgX, lc.lcOutOrgY, std::abs(lc.lcOutExtX), std::abs(lc.lcOutExtY));
  desktopArea = Rect::ltwh(lc.lcSysOrgX, lc.lcSysOrgY, lc.lcSysExtX, lc.lcSysExtY);

  hTab = gpWTOpenA(hWnd, &lc, TRUE);
  // set queue size
  gpWTQueueSizeSet(hTab, PACKET_BUFF_SIZE);
  PLATFORM_LOG("Wintab initialized.\n");
}

// Wintab references:
//  https://developer-docs.wacom.com/display/DevDocs/Windows+Wintab+Documentation
//  Qt: http://qt.gitorious.org/qt/qt/blobs/raw/4.8/src/gui/kernel/qapplication_win.cpp  qwidget_win.cpp
// WM_POINTER references:
//  http://msdn.microsoft.com/en-us/library/windows/desktop/hh454916(v=vs.85).aspx
//  http://software.intel.com/en-us/articles/comparing-touch-coding-techniques-windows-8-desktop-touch-sample
// Windows' "interaction context" stuff can be used with WM_POINTER to recognize gestures (see the
//  InteractionContextSample from Intel) but is a bit too high level, e.g., no way to select single finger
//  pan vs. two finger pan.  Also, we already have "gesture recognition" code

// WM_POINTER observations (from Win 8.0 on Microsoft Surface Pro):
// * Multiple touch points: we receive a move event with the two pointers before the press event for the second
// * Pen entering proximity when touch points are down: WM_POINTERUP is sent for each
//  touch point, but each GetPointerFrameInfo only returns a frame with one point, even for the first point
//  to go up.  WM_POINTERENTER is sent for the pen pointer AFTER touch points go up.
// The POINTER_INFO associated with the pointer up event is identical to a normal pointer up event,
//  except the POINTER_FLAG_CONFIDENCE flag is set, but there is no reason to think this is a reliable
//  indicator across devices.
// If we want to use relative timing, compare touch pointer up time to WM_POINTERENTER time

// Debugging w/ Visual Studio VM:
// 1. share C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\IDE\Remote Debugger in VM
// 1. open share on host and run x64/msvsmon.exe
// 1. build Write and run on host
// 1. In VS: Debug -> Attach to Process..., enter host's network name or IP, choose Write process
// Things that don't work:
// - touchscreen.vusb.usePen = "TRUE" ... Win 8.1 crashes, Win 10 mouse breaks
// - connect Wacom device to VM ... fails, breaks pen and touch on host, requiring restart

static bool processWTPacket(MSG* msg)
{
  // primary barrel button is 0x2 for single button pen, but 0x4 with default config of two button pen!
  static DWORD tipBtn = 0x00000001;
  int numPackets = gpWTPacketsGet((HCTX)(msg->lParam), PACKET_BUFF_SIZE, &localPacketBuf);
  for(int ii = 0; ii < numPackets; ii++) {
    DWORD btnNew = localPacketBuf[ii].pkButtons;
    UINT pressureNew = localPacketBuf[ii].pkNormalPressure;
    // on my X1 Yoga, if barrel button is pressed before pen touches screen, tipBtn never gets set
    // remaining issue is that button release becomes pen up too ... remove btnNew != 0 condition?
    if(pressureNew && btnNew)
      btnNew |= tipBtn;
    PenEventType eventtype = TabletMove;
    if((btnNew & tipBtn) && !(btnPrev & tipBtn))
      eventtype = TabletPress;
    else if(!(btnNew & tipBtn) && (btnPrev & tipBtn))
      eventtype = TabletRelease;

    Dim globalx = ((localPacketBuf[ii].pkX - tabletArea.left) * desktopArea.width() / tabletArea.width()) + desktopArea.left;
    Dim globaly = ((localPacketBuf[ii].pkY - tabletArea.top) * desktopArea.height() / tabletArea.height()) + desktopArea.top;
    Dim pressure = btnNew ? pressureNew / Dim(maxPressure - minPressure) : 0;
    // docs say to check this upon WT_PROXIMITY ... but it can change afterwards too!
    bool eraser = (localPacketBuf[ii].pkCursor % 3 == 2);

    int uniqueId = 1;
    // TODO: enable and use pkTime field
    notifyTabletEvent(eventtype, Point(globalx, globaly), pressure, 0, 0,
        eraser, btnNew ^ btnPrev, btnNew, uniqueId, SDL_GetTicks());
    btnPrev = btnNew;
  }
  return true;
}

static bool winTabEvent(MSG* msg) //, long* result)
{
  switch(msg->message) {
  case WM_ACTIVATE:
    gpWTEnable(hTab, GET_WM_ACTIVATE_STATE(msg->wParam, msg->lParam));
    if(GET_WM_ACTIVATE_STATE(msg->wParam, msg->lParam))
      gpWTOverlap(hTab, TRUE);
    else {
      SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
      SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
      SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
    }
    break;
  case WM_DESTROY:
    gpWTClose(hTab);
    break;
  case WT_PROXIMITY:
    // only handle proximity enter
    if(LOWORD(msg->lParam) != 0) {  // entering proximity
      LOGCONTEXTA lc;
      AXIS np;
      // get the current context
      gpWTGetA((HCTX)(msg->wParam), &lc);
      // this shouldn't be necessary - output range shouldn't change
      tabletArea = Rect::ltwh(lc.lcOutOrgX, lc.lcOutOrgY, std::abs(lc.lcOutExtX), std::abs(lc.lcOutExtY));
      // this doesn't seem to change (w/o closing and reopening Wintab) so we could do only in init
      desktopArea = Rect::ltwh(lc.lcSysOrgX, lc.lcSysOrgY, lc.lcSysExtX, lc.lcSysExtY);
      // get the size of the pressure axis
      gpWTInfoA(WTI_DEVICES + lc.lcDevice, DVC_NPRESSURE, &np);
      minPressure = int(np.axMin);
      maxPressure = int(np.axMax);
      // TODO: get uniqueID and other cursor info
      btnPrev = 0;
      // this seems to be the only way to get virtual screen size from Windows, but it doesn't work right w/o
      //  PROCESS_PER_MONITOR_DPI_AWARE set; I don't know what happens on earlier versions of Windows
      //int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

      updateClientRect(msg->hwnd);
      // WinTab generates WM_MOUSEMOVE (and thus SDL mouse events) for cursor context (CXO_SYSTEM set)
      // ... disable them to prevent ScribbleArea from changing cursor to mouse cursor
      SDL_EventState(SDL_MOUSEMOTION, SDL_DISABLE);
      SDL_EventState(SDL_MOUSEBUTTONUP, SDL_DISABLE);
      SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_DISABLE);
      winTabProximity = true;
    }
    else {  // leaving proximity
      SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
      SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
      SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
      winTabProximity = false;
    }
    return true;
  case WT_PACKET:
    return processWTPacket(msg);
  case WM_DISPLAYCHANGE:
    // when resolution changes, Wintab seems to use updated resolution internally but LOGCONTEXT returned
    //  by WTGet isn't updated
    gpWTClose(hTab);
    initWinTab(msg->hwnd);
    break;
  default:
    break;
  } // switch
  return false; // propagate to next handler
}

#endif // Wintab

/* Use this code to debug problem with multiple screens:
      RECT pRect, dRect;
      GetPointerDeviceRects(pointerInfo[ii].sourceDevice, &pRect, &dRect);
      PLATFORM_LOG("Pointer device rect ltrb: %d %d %d %d; Display device rect: %d %d %d %d\n",
        pRect.left, pRect.top, pRect.right, pRect.bottom,
        dRect.left, dRect.top, dRect.right, dRect.bottom);
      PLATFORM_LOG("WMPointer touch at %d, %d; himetric %d, %d; clientRect origin: %f, %f\n",
        pointerInfo[ii].ptPixelLocation.x, pointerInfo[ii].ptPixelLocation.y,
        pointerInfo[ii].ptHimetricLocation.x, pointerInfo[ii].ptHimetricLocation.y,
        clientRect.left, clientRect.top);
*/

static void processPenInfo(const POINTER_PEN_INFO& ppi, PenEventType eventtype)
{
  static UINT32 prevButtons = 0;

  if(winTabProximity) return;
  const POINT pix = ppi.pointerInfo.ptPixelLocation;
  const POINT him = ppi.pointerInfo.ptHimetricLocation;
  Dim x = pix.x;
  Dim y = pix.y;
  RECT pRect, dRect;  // pointer (Himetric) rect, display rect
  if(GetPointerDeviceRects(ppi.pointerInfo.sourceDevice, &pRect, &dRect)) {
    x = dRect.left + (dRect.right - dRect.left) * Dim(him.x - pRect.left)/(pRect.right - pRect.left);
    y = dRect.top + (dRect.bottom - dRect.top) * Dim(him.y - pRect.top)/(pRect.bottom - pRect.top);
  }
  // Confirmed that HIMETRIC is higher resolution than pixel location on Surface Pro: saw different HIMETRIC
  //  locations for the same pixel loc, including updates to HIMETRIC loc with no change in pixel loc
  //PLATFORM_LOG("Pix: %d %d; HIMETRIC: %d %d", pix.x, pix.y, him.x, him.y);
  //PLATFORM_LOG("Pointer flags: 0x%x; pen flags: 0x%x\n", ppi.pointerInfo.pointerFlags, ppi.penFlags);

  // if barrel button pressed when pen down, penFlags changes but pointerFlags doesn't (the first time)
  UINT32 buttons = ((ppi.pointerInfo.pointerFlags >> 4) & 0x1F) | (ppi.penFlags & PEN_FLAG_BARREL ? SDL_BUTTON_RMASK : 0);
  bool isEraser = ppi.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED);  // PEN_FLAG_INVERTED means eraser in proximity
  notifyTabletEvent(eventtype, Point(x, y), ppi.pressure/1024.0, ppi.tiltX/90.0, ppi.tiltY/90.0, isEraser,
      buttons ^ prevButtons, buttons, int(ppi.pointerInfo.sourceDevice), ppi.pointerInfo.dwTime);
  prevButtons = buttons;
}

// ideally, we wouldn't process history unless mode is STROKE
static void processPenHistory(UINT32 ptrid)
{
  if(winTabProximity) return;
  UINT32 historycount = MAX_N_POINTERS;
  POINTER_PEN_INFO* ppi = &penPointerInfo[0];
  if(GetPointerPenInfoHistory(ptrid, &historycount, ppi)) {
    if(historycount > MAX_N_POINTERS) {
      // need more room ... we want to get all history at once since it's returned newest first!
      ppi = new POINTER_PEN_INFO[historycount];
      GetPointerPenInfoHistory(ptrid, &historycount, ppi);
    }
    // process items oldest to newest
    for(int ii = historycount - 1; ii >= 0; ii--)
      processPenInfo(ppi[ii], TabletMove);
    if(ppi != &penPointerInfo[0])
      delete[] ppi;
  }
}

static bool processPointerFrame(UINT32 ptrid, TouchPointState eventtype)
{
  UINT32 pointercount = MAX_N_POINTERS;
  std::vector<TouchPoint> pts;
  if(GetPointerFrameInfo(ptrid, &pointercount, &pointerInfo[0])) {
    for(unsigned int ii = 0; ii < pointercount; ii++) {
      if(pointerInfo[ii].pointerType == PT_PEN) {
        // for hovering pen
        if(GetPointerPenInfo(pointerInfo[ii].pointerId, &penPointerInfo[0]))
          processPenInfo(penPointerInfo[0], TabletMove);
        // propagate pen hover events to DefWindowProc so mouse cursor follows pen - see explanation below
        return false;
      }
      if(pointerInfo[ii].pointerType != PT_TOUCH)
        continue;
      TouchPoint pt;
      pt.id = pointerInfo[ii].pointerId;
      pt.state = pointerInfo[ii].pointerId == ptrid ? eventtype : TouchPointMoved;
      pt.screenPos = Point(pointerInfo[ii].ptPixelLocation.x, pointerInfo[ii].ptPixelLocation.y);
      pt.pressure = 1;
      pts.push_back(pt);
    }
    if(pts.empty())
      return false;
    //event.t = pointerInfo[0].performanceCount;
    notifyTouchEvent(eventtype, pts, pointerInfo[0].dwTime);
    return true;
  }
  else if(eventtype == TouchPointReleased) {
    // seems GetPointerFrameInfo/GetPointerInfo sometimes returns error for WM_POINTERUP ... didn't notice this before
    TouchPoint pt;
    pt.id = ptrid;
    pt.state = eventtype;
    pt.screenPos = Point(0, 0);
    pt.pressure = 0;
    pts.push_back(pt);
    notifyTouchEvent(eventtype, pts, pointerInfo[0].dwTime);  // reuse time from last event
    return true;
  }
  return false;
}

static void initWMPointer()
{
#ifdef WMPOINTER_NEEDED
  HINSTANCE user32 = LoadLibraryA("user32.dll");
  if(user32) {
    GetPointerInfo = (PtrGetPointerInfo)(GetProcAddress(user32, "GetPointerInfo"));
    GetPointerFrameInfo = (PtrGetPointerFrameInfo)(GetProcAddress(user32, "GetPointerFrameInfo"));
    GetPointerPenInfo = (PtrGetPointerPenInfo)(GetProcAddress(user32, "GetPointerPenInfo"));
    GetPointerPenInfoHistory = (PtrGetPointerPenInfoHistory)(GetProcAddress(user32, "GetPointerPenInfoHistory"));
    GetPointerDeviceRects = (PtrGetPointerDeviceRects)(GetProcAddress(user32, "GetPointerDeviceRects"));
    InjectTouchInput = (PtrInjectTouchInput)(GetProcAddress(user32, "InjectTouchInput"));
    InitializeTouchInjection = (PtrInitializeTouchInjection)(GetProcAddress(user32, "InitializeTouchInjection"));
  }
#endif
  // Attempt to get HIMETRIC to pixel conversion factor; on Surface Pro, result is close, but not quite
  // 1 HIMETRIC = 0.01 mm
  //QWidget* screen = QApplication::desktop()->screen(0);
  // this is equiv to GetDeviceCaps(HORZRES)/GetDeviceCaps(HORZSIZE)
  //HimetricToPix = (96.0 / 2540);  //screen->width()/qreal(100*screen->widthMM());
}

static bool winInputEvent(MSG* m) //, long* result)
{
  // we're assuming pointerIds are never 0, which seems to be the case, but probably isn't a good idea
  static UINT32 penPointerId = 0;

#ifdef WMPOINTER_NEEDED
  if(!GetPointerInfo)
    return false;
#endif
  switch(m->message) {
  // WM_POINTER:
  // WM_POINTERDOWN with type PT_PEN: ignore all other pointers, use GetPointerPenInfoHistory
  // otherwise, use GetPointerFrameInfo (discard history)
  case WM_POINTERDOWN:
    updateClientRect(m->hwnd);
    if(GetPointerInfo(GET_POINTERID_WPARAM(m->wParam), &pointerInfo[0])) {
      if(pointerInfo[0].pointerType == PT_PEN) {
        penPointerId = pointerInfo[0].pointerId;
        if(GetPointerPenInfo(penPointerId, &penPointerInfo[0]))
          processPenInfo(penPointerInfo[0], TabletPress);
        return true;
      }
      else
        return processPointerFrame(GET_POINTERID_WPARAM(m->wParam), TouchPointPressed);
    }
    break;
  case WM_POINTERUPDATE:
    updateClientRect(m->hwnd);
    if(penPointerId && penPointerId == GET_POINTERID_WPARAM(m->wParam)) {
      processPenHistory(penPointerId);
      return true;
    }
    else
      return processPointerFrame(GET_POINTERID_WPARAM(m->wParam), TouchPointMoved);
    break;
  case WM_POINTERUP:
    updateClientRect(m->hwnd);
    if(penPointerId && penPointerId == GET_POINTERID_WPARAM(m->wParam)) {
      if(GetPointerPenInfo(penPointerId, &penPointerInfo[0]))
        processPenInfo(penPointerInfo[0], TabletRelease);
      penPointerId = 0;
      return true;
    }
    else
      return processPointerFrame(GET_POINTERID_WPARAM(m->wParam), TouchPointReleased);

  case WM_MOUSEWHEEL:
  case WM_MOUSEHWHEEL:
  {
    // SDL doesn't update its internal mod state if window doesn't have focus, so get mods from Windows
    uint32_t mods = GetKeyState(VK_CONTROL) < 0 ? KMOD_CTRL : 0;  // high order bit set if key is down
    mods |= GetKeyState(VK_SHIFT) < 0 ? KMOD_SHIFT : 0;  // high order bit set if key is down
    SDL_Event event = { 0 };  // we'll leave windowID and which == 0
    event.type = SDL_MOUSEWHEEL;
    //event.wheel.timestamp = SDL_GetTicks();
    event.wheel.x = m->message == WM_MOUSEWHEEL ? 0 : GET_WHEEL_DELTA_WPARAM(m->wParam);
    event.wheel.y = m->message == WM_MOUSEWHEEL ? GET_WHEEL_DELTA_WPARAM(m->wParam) : 0;
    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL | (mods << 16);
    SDL_PushEvent(&event);  //SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    break;
  }
  case WM_CLIPBOARDUPDATE:
  {
    SDL_Event event = { 0 };
    event.type = SDL_CLIPBOARDUPDATE;
    SDL_PushEvent(&event);
    break;
  }
  // Windows shutdown message; followed by WM_ENDSESSION
  case WM_QUERYENDSESSION:
  {
    SDL_Event event = { 0 };
    event.type = SDL_APP_WILLENTERBACKGROUND;
    SDL_PushEvent(&event);  // currently handled by event filter, so use PushEvent instead of PeepEvents
    break;
  }
  default:
    break;
  }
  return false;
}

static WNDPROC prevWndProc = NULL;

// We wrap SDL's window proc to prevent WM_POINTER from being forwared to DefWindowProc, which causes touch
//  feedback to be shown (e.g. on pen tap). However, in order for mouse cursor to track pen (or touch)
//  position, WM_POINTER messages must reach DefWindowProc (or SetCursorPos must be called manually), so we
//  do propagate pen hover events to DefWindowProc. The resulting mouse events are flagged as synthesized
//  from touch (unlike if we called SetCursorPos manually), so they'll be ignored in SvgGui::sdlEvent.
// Besides seeming like the right UX, it is necessary for mouse cursor to track pen because setting cursor
//  fails (at least via SDL) if the mouse cursor is outside the window (even if the pen cursor is inside).
// Win 8+ provides SetWindowFeedbackSetting that might also accomplish this, but swallowing WM_POINTER (except
//  pen hover) was how it worked in Qt version of Write ... by dumb luck!
// Note that neither SDL_SYSWMEVENT nor SDL_WindowsMessageHook provide a way to swallow events
// If we need this for multiple windows, look into setting for window class instead of per window - see
//  SDL_RegisterApp
LRESULT CALLBACK wrapSDLWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  MSG msg = { hwnd, uMsg, wParam, lParam, 0, {0, 0} };
  // window proc return value depends on message, but typically 0 indicates application has processed message
#ifdef USE_WINTAB
  if(hTab && winTabEvent(&msg))
    return 0;
#endif
  if(winInputEvent(&msg))
    return 0;  //DefWindowProc(hwnd, uMsg, wParam, lParam);

  return CallWindowProc(prevWndProc, hwnd, uMsg, wParam, lParam);
}

#include "SDL_syswm.h"

void initTouchInput(SDL_Window* sdlwin, bool useWintab)
{
  SDL_SysWMinfo wmInfo;
  SDL_VERSION(&wmInfo.version);
  if(!SDL_GetWindowWMInfo(sdlwin, &wmInfo))
    return;
  HWND hwnd = wmInfo.info.win.window;
  //sdlWindow = sdlwin;

  // setup Wintab and WM_POINTER support
  initWMPointer();
#ifdef USE_WINTAB
  hTab = NULL;
  if(useWintab)
    initWinTab(hwnd);
#endif

  //SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
  // wrap SDL's window proc to handle Wintab and WM_POINTER events
  prevWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)&wrapSDLWndProc);

#ifdef WMPOINTER_NEEDED
  if(!GetPointerInfo)
    return;  // don't disable SDL's touch handling if WM_POINTER not available
#endif

  // SDL divides mouse wheel step by WHEEL_DELTA (== 120) and sends as int - this is fine for an actual
  //  mouse wheel but throws away resolution for touchpad scroll, so we handle it ourselves
  SDL_EventState(SDL_MOUSEWHEEL, SDL_DISABLE);
  // disable SDL's touch handling since it will send finger events for pen input too
  // - technically not necessary if there are no WM_TOUCH events
  SDL_EventState(SDL_FINGERDOWN, SDL_DISABLE);
  SDL_EventState(SDL_FINGERMOTION, SDL_DISABLE);
  SDL_EventState(SDL_FINGERUP, SDL_DISABLE);
  // tell Windows not to send WM_TOUCH (SDL calls RegisterTouchWindow())
  // - doesn't seem to be necessary if we accept WM_POINTER, but can't hurt I guess
  UnregisterTouchWindow(hwnd);  // Win 7+

  // SDL_SetClipboardText is broken - it reads sequence number before calling CloseClipboard()
  AddClipboardFormatListener(hwnd);  // Win Vista+
  SDL_EventState(SDL_CLIPBOARDUPDATE, SDL_DISABLE);
}

#include <ShellScalingAPI.h>
typedef HRESULT(WINAPI *PtrSetProcessDpiAwareness)(PROCESS_DPI_AWARENESS);

void setDPIAware()
{
  HINSTANCE shcore = LoadLibraryA("shcore.dll");
  if(shcore) {
    PtrSetProcessDpiAwareness fn = (PtrSetProcessDpiAwareness)(GetProcAddress(shcore, "SetProcessDpiAwareness"));
    if(fn) {
      fn(PROCESS_PER_MONITOR_DPI_AWARE);
      return;
    }
  }
  // This is supposed to be set in application manifest; needs #include <ShellScalingAPI.h> and Shcore.lib
  //SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);  -- use LoadLibraryA and GetProcAddress here!
  SetProcessDPIAware();
}

// ref: https://github.com/FMJ-Software/Image-Eye/blob/master/Src/Clipboard.cpp
Image getClipboardImage()
{
  Image img(0, 0);
  //UINT fmt = 0; OpenClipboard(NULL); do { fmt = EnumClipboardFormats(fmt); } while(fmt); CloseClipboard();
  if(!IsClipboardFormatAvailable(CF_DIB) || !OpenClipboard(NULL))
    return img;

  HANDLE hClipboard = GetClipboardData(CF_DIB);
  //if(!hClipboard)
  //  hClipboard = GetClipboardData(CF_DIBV5);
  if(hClipboard && hClipboard != INVALID_HANDLE_VALUE) {
    void* dib = GlobalLock(hClipboard);
    if(dib) {
      BITMAPINFOHEADER* bminfo = (BITMAPINFOHEADER*)dib;
      //BITMAPFILEHEADER hdr;
      constexpr int bfHdrSize = 14;
      int iColors = 0;
      if(bminfo->biBitCount <= 8)
        iColors = bminfo->biClrUsed == 0 ? (1 << bminfo->biBitCount) : bminfo->biClrUsed;
      DWORD dwSize = bminfo->biSizeImage;
      if(dwSize == 0)
        dwSize = bminfo->biHeight * ((((bminfo->biWidth*bminfo->biBitCount + 7) / 8) + 3) & (~3));
      DWORD bfOffset = bfHdrSize + bminfo->biSize + iColors * sizeof(RGBQUAD);
      if(bminfo->biCompression == BI_BITFIELDS) bfOffset += 3 * sizeof(DWORD);
      DWORD bfSize = bfOffset + dwSize;

      unsigned char* buff = (unsigned char*)malloc(bfSize);
      *(WORD*)buff = 0x4D42;  // "BM"
      *(DWORD*)(buff + 2) = bfSize;
      *(DWORD*)(buff + 6) = 0;
      *(DWORD*)(buff + 10) = bfOffset;
      memcpy(buff + bfHdrSize, bminfo, bfSize - bfHdrSize);

      // we have to assume PNG for clipboard; since Image::encodePNG() checks for PNG header, not a problem
      //  that encoded format is Windows bitmap instead of PNG
      img = Image::decodeBuffer(buff, bfSize, Image::PNG);
      free(buff);
      GlobalUnlock(dib);
    }
  }
  CloseClipboard();
  return img;
}

// On Win 8.1 VM, widthMM is just 96 * widthPx
/*float winGetRealDpi(SDL_Window* sdlWindow)
{
  SDL_SysWMinfo wmInfo;
  SDL_VERSION(&wmInfo.version);
  if(!SDL_GetWindowWMInfo(sdlWindow, &wmInfo))
    return 96;
  HDC dc = wmInfo.info.win.hdc;
  int widthMM = GetDeviceCaps(dc, HORZSIZE);
  int widthPx = GetDeviceCaps(dc, HORZRES);
  return (25.4*widthPx) / widthMM;
}*/


/*
// Qt created a dummy window to receive Wintab events - not sure what the advantage of this is; doesn't seem
//  necessary
static HWND createDummyWindow(WNDPROC wndProc)
{
  WNDCLASSEX wc;
  HWND hwnd;
  MSG Msg;
  HINSTANCE hInstance = static_cast<HINSTANCE>(GetModuleHandle(0));
  const wchar_t* winClassName = L"TabletDummyWindow";
  const wchar_t* windowName = L"TabletDummyWindow";

  //Step 1: Registering the Window Class
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = 0;
  wc.lpfnWndProc = wndProc;  // wndProc ? wndProc : DefWindowProc
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = hInstance;
  wc.hIcon = 0; // LoadIcon(NULL, IDI_APPLICATION);
  wc.hCursor = 0; // LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
  wc.lpszMenuName = NULL;
  wc.lpszClassName = winClassName;
  wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

  if(!RegisterClassEx(&wc))
    return 0;

  return CreateWindowEx(0, winClassName, windowName, WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, HWND_MESSAGE, NULL, hInstance, NULL);
}

LRESULT CALLBACK winTabWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  MSG msg = { hwnd, uMsg, wParam, lParam, 0, {0, 0} };
  if(ghWintab && winTabEvent(&msg))
    return 0;
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
*/
// functions for direct injection of tablet and touch events (only used on Windows at the moment)
//#include "test/scribbletest.h"

// see http://code.msdn.microsoft.com/windowsdesktop/Touch-Injection-Sample-444d9bf7/
/* #ifdef SCRIBBLE_TEST
bool ScribbleInput::injectTouch(Dim x, Dim y, Dim p, int event)
{
  static bool touchInjectionInited = false;
  static POINTER_TOUCH_INFO contact;
  if(!GetPointerInfo)
    return false;

  if(!touchInjectionInited) {
    InitializeTouchInjection(10, TOUCH_FEEDBACK_NONE);
    memset(&contact, 0, sizeof(POINTER_TOUCH_INFO));
    contact.pointerInfo.pointerType = PT_TOUCH; // we're sending touch input
    contact.pointerInfo.pointerId = 0;          // contact 0
    contact.touchFlags = TOUCH_FLAG_NONE;
    contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    contact.orientation = 90;
    // set the contact area depending on thickness
    //contact.rcContact.top = 480 - 2;
    //contact.rcContact.bottom = 480 + 2;
    //contact.rcContact.left = 640 - 2;
    //contact.rcContact.right = 640 + 2;
    touchInjectionInited = true;
  }

  contact.pointerInfo.ptPixelLocation.x = x;
  contact.pointerInfo.ptPixelLocation.y = y;
  //contact.pointerInfo.ptHimetricLocation.x = x/HimetricToPix;
  //contact.pointerInfo.ptHimetricLocation.y = y/HimetricToPix;
  contact.pressure = p * 1024;
  if(event == INPUTEVENT_PRESS)
    contact.pointerInfo.pointerFlags = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
  else if(event == INPUTEVENT_RELEASE)
    contact.pointerInfo.pointerFlags = POINTER_FLAG_UP;
  else
    contact.pointerInfo.pointerFlags = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
  if(InjectTouchInput(1, &contact) == 0)
    return false;
  // seems to be necessary for us to receive the touch event
  QApplication::processEvents();
  return true;
}
#endif */
