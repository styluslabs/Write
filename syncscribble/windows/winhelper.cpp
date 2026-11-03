// Windows-specific helpers for Write: clipboard image, Wintab; platform layer is ugui/platforms/winmain.cpp
#include "winhelper.h"
#include "basics.h"

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

// Wintab
// We subclass the platform layer's window so we see messages before ugui/platforms/winmain.cpp; Wintab
//  generates mouse input (system cursor context) and Windows Ink may also report the pen, so mouse and pen
//  input is dropped while Wintab pen is in proximity.  Pen events are queued w/ SDL_PeepEvents, same as
//  winmain's own input events, so order is preserved.
// refs:
//  https://developer-docs.wacom.com/display/DevDocs/Windows+Wintab+Documentation
//  Qt: http://qt.gitorious.org/qt/qt/blobs/raw/4.8/src/gui/kernel/qapplication_win.cpp  qwidget_win.cpp

#include "ugui/svggui_platform.h"
#include "wintab/Utils.h"
#include "wintab/MSGPACK.H"
// these defines are required for pktdef.h
#define PACKETDATA (PK_X | PK_Y | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_CURSOR)
#define PACKETMODE 0
#include "wintab/PKTDEF.H"

static constexpr int PACKET_BUFF_SIZE = 128;
static PACKET localPacketBuf[PACKET_BUFF_SIZE];
static HCTX hTab = NULL;
static WNDPROC prevWndProc = NULL;
static bool winTabProximity = false;

static RECT tabletArea;  // left, top, width, height
static RECT desktopArea;  // left, top, width, height
static int minPressure = 0, maxPressure = 0;
static DWORD btnPrev = 0;
static POINT clientOrigin = {0, 0};  // client area origin in screen coords

// GetPointerType is Win 8+; WM_POINTER messages are only sent on Win 8+, so only needed then
typedef BOOL (WINAPI *PtrGetPointerType)(UINT32, POINTER_INPUT_TYPE*);
static PtrGetPointerType pGetPointerType = NULL;

static bool openWintab(HWND hWnd)
{
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
  tabletArea = RECT{lc.lcOutOrgX, lc.lcOutOrgY, std::abs(lc.lcOutExtX), std::abs(lc.lcOutExtY)};
  desktopArea = RECT{lc.lcSysOrgX, lc.lcSysOrgY, lc.lcSysExtX, lc.lcSysExtY};

  hTab = gpWTOpenA(hWnd, &lc, TRUE);
  if(!hTab) { PLATFORM_LOG("WTOpen failed\n"); return false; }
  gpWTQueueSizeSet(hTab, PACKET_BUFF_SIZE);
  return true;
}

static void processWTPacket(HCTX ctx)
{
  // primary barrel button is 0x2 for single button pen, but 0x4 with default config of two button pen!
  static DWORD tipBtn = 0x00000001;
  int numPackets = gpWTPacketsGet(ctx, PACKET_BUFF_SIZE, &localPacketBuf);
  for(int ii = 0; ii < numPackets; ii++) {
    DWORD btnNew = localPacketBuf[ii].pkButtons;
    UINT pressureNew = localPacketBuf[ii].pkNormalPressure;
    // on my X1 Yoga, if barrel button is pressed before pen touches screen, tipBtn never gets set
    if(pressureNew && btnNew)
      btnNew |= tipBtn;
    uint32_t eventtype = SDL_FINGERMOTION;
    if((btnNew & tipBtn) && !(btnPrev & tipBtn))
      eventtype = SDL_FINGERDOWN;
    else if(!(btnNew & tipBtn) && (btnPrev & tipBtn))
      eventtype = SDL_FINGERUP;

    double globalx = (double(localPacketBuf[ii].pkX - tabletArea.left) * desktopArea.right / tabletArea.right) + desktopArea.left;
    double globaly = (double(localPacketBuf[ii].pkY - tabletArea.top) * desktopArea.bottom / tabletArea.bottom) + desktopArea.top;
    double pressure = btnNew && maxPressure > minPressure ? pressureNew / double(maxPressure - minPressure) : 0;
    // docs say to check this upon WT_PROXIMITY ... but it can change afterwards too!
    bool eraser = (localPacketBuf[ii].pkCursor % 3 == 2);

    // same event format as winmain.cpp's pen input
    SDL_Event event = {0};
    event.type = eventtype;
    event.tfinger.timestamp = SDL_GetTicks();  // TODO: enable and use pkTime field
    event.tfinger.touchId = eraser ? PenPointerEraser : PenPointerPen;
    event.tfinger.fingerId = eventtype == SDL_FINGERMOTION ? btnNew : (btnNew ^ btnPrev);
    event.tfinger.x = globalx - clientOrigin.x;
    event.tfinger.y = globaly - clientOrigin.y;
    event.tfinger.pressure = pressure;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    btnPrev = btnNew;
  }
}

// returns true if message should not be passed to platform layer
static bool winTabEvent(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch(uMsg) {
  case WM_ACTIVATE:
    gpWTEnable(hTab, GET_WM_ACTIVATE_STATE(wParam, lParam));
    if(GET_WM_ACTIVATE_STATE(wParam, lParam))
      gpWTOverlap(hTab, TRUE);
    else
      winTabProximity = false;
    return false;
  case WT_PROXIMITY:
    if(LOWORD(lParam) != 0) {  // entering proximity
      LOGCONTEXTA lc;
      AXIS np;
      gpWTGetA((HCTX)wParam, &lc);
      // this shouldn't be necessary - output range shouldn't change
      tabletArea = RECT{lc.lcOutOrgX, lc.lcOutOrgY, std::abs(lc.lcOutExtX), std::abs(lc.lcOutExtY)};
      // this doesn't seem to change (w/o closing and reopening Wintab) so we could do only in init
      desktopArea = RECT{lc.lcSysOrgX, lc.lcSysOrgY, lc.lcSysExtX, lc.lcSysExtY};
      // get the size of the pressure axis
      gpWTInfoA(WTI_DEVICES + lc.lcDevice, DVC_NPRESSURE, &np);
      minPressure = int(np.axMin);
      maxPressure = int(np.axMax);
      btnPrev = 0;
      clientOrigin = {0, 0};
      ClientToScreen(hwnd, &clientOrigin);
      winTabProximity = true;
    }
    else  // leaving proximity
      winTabProximity = false;
    return true;
  case WT_PACKET:
    processWTPacket((HCTX)lParam);
    return true;
  case WM_DISPLAYCHANGE:
    // when resolution changes, Wintab seems to use updated resolution internally but LOGCONTEXT returned
    //  by WTGet isn't updated
    gpWTClose(hTab);
    hTab = NULL;
    openWintab(hwnd);
    return false;
  case WM_POINTERDOWN:
  case WM_POINTERUPDATE:
  case WM_POINTERUP:
  {
    // drop mouse (generated by Wintab system cursor context) and pen (Windows Ink) input while in proximity
    POINTER_INPUT_TYPE type = PT_POINTER;
    if(!winTabProximity || !pGetPointerType || !pGetPointerType(GET_POINTERID_WPARAM(wParam), &type))
      return false;
    return type == PT_MOUSE || type == PT_PEN;
  }
  case WM_MOUSEMOVE:
  case WM_LBUTTONDOWN: case WM_LBUTTONUP:
  case WM_RBUTTONDOWN: case WM_RBUTTONUP:
  case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    return winTabProximity;
  default:
    return false;
  }
}

static LRESULT CALLBACK wintabWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if(hTab && winTabEvent(hwnd, uMsg, wParam, lParam))
    return 0;
  return CallWindowProc(prevWndProc, hwnd, uMsg, wParam, lParam);
}

void initWintab(HWND hwnd)
{
  // attempt to load wintab
  if(!hwnd || !LoadWintab() || !gpWTInfoA || !gpWTInfoA(0, 0, NULL) || !gpWTPacketsGet)
    return;
  if(!openWintab(hwnd)) { UnloadWintab(); return; }
  pGetPointerType = (PtrGetPointerType)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetPointerType");
  prevWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)&wintabWndProc);
  // initial WM_ACTIVATE was sent before we subclassed
  if(GetActiveWindow() == hwnd)
    gpWTOverlap(hTab, TRUE);
  PLATFORM_LOG("Wintab initialized.\n");
}

void shutdownWintab()
{
  if(!hTab) { return; }
  HWND hwnd = (HWND)platformGetNativeWindow();
  if(hwnd && prevWndProc)
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)prevWndProc);
  prevWndProc = NULL;
  gpWTClose(hTab);
  hTab = NULL;
  UnloadWintab();
}
