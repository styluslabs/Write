#include "linuxwayland.h"
#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_mouse.h"
#include "SDL_syswm.h"
#include "SDL_timer.h"
#include "SDL_version.h"
#include "tablet-v2.h"
#include "ugui/svggui_platform.h"
#include "unistd.h"
#include "wayland-client-protocol.h"
#include "SDL_video.h"
#include "wayland-util.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#define MAX_TOOLS 32
#define MAX_TOUCH_POINTS 10

// in ScribbleApp
extern void clipboardFromBuffer(const unsigned char* buff, size_t len, int is_image);

typedef struct ToolState {
  struct zwp_tablet_tool_v2* tool;
  uint32_t toolType;
  float x;
  float y;
  float tiltX; // normalized to -1 .. +1
  float tiltY; // normalized to -1 .. +1
  float pressure; // normalized to 0..1
  unsigned int buttons;

  // 0 means not set this frame.
  // 1 means pen up.
  // 2 means pen down.
  int framePenDown;
  bool frameMotionSet;
} ToolState;

enum TouchEventMask {
       TOUCH_EVENT_DOWN = 1 << 0,
       TOUCH_EVENT_UP = 1 << 1,
       TOUCH_EVENT_MOTION = 1 << 2,
       TOUCH_EVENT_SHAPE = 1 << 3,
       TOUCH_EVENT_ORIENTATION = 1 << 4,
};

// sorted by lower to higher priority for clipboard selection
enum MimeType {
  // TODO: foot supports TEXT, STRING, UTF8_STRING, what are those??
  MIME_TYPE_UNSET,
  MIME_TYPE_TEXT_PLAIN,
  MIME_TYPE_TEXT_UTF8,

  MIME_TYPE_APP_IMAGE_SVG_XML,
};

typedef struct TouchPoint {
  bool valid;
  int32_t id;
  uint32_t eventMask;

  wl_fixed_t surface_x, surface_y;
  wl_fixed_t major, minor;
  wl_fixed_t orientation;
} TouchPoint;

typedef struct TouchEvent {
  uint32_t time;
  uint32_t serial;
  bool cancelled;
  struct TouchPoint points[MAX_TOUCH_POINTS];
} TouchEvent;

typedef struct WlState {
  SDL_Window* window;
  struct wl_seat* seat;
  struct zwp_tablet_manager_v2* tabletManager;
  struct zwp_tablet_seat_v2* tabletSeat;
  struct wl_touch* touch;
  struct wl_data_device_manager* dataDeviceManager;
  struct wl_data_device* dataDevice;
  struct wl_data_offer* dataOffer;
  ToolState tools[MAX_TOOLS];
  TouchEvent touchEvent;
  enum MimeType clipboardMimeType;
} WlState;

static WlState wlState = {0};

static const char *const mimeTypeMap[] = {
  [MIME_TYPE_UNSET] = NULL,
  [MIME_TYPE_TEXT_PLAIN] = "text/plain",
  [MIME_TYPE_TEXT_UTF8] = "text/plain;charset=utf-8",

  [MIME_TYPE_APP_IMAGE_SVG_XML] = "application/svg+xml",
};

static float getDisplayScaleFactor(SDL_Window* window) {
  int displayIdx = SDL_GetWindowDisplayIndex(window);
  float ddpi, hdpi, vdpi;
  if (SDL_GetDisplayDPI(displayIdx, &ddpi, &hdpi, &vdpi) == 0) {
    return ddpi/96.f; // 96 DPI is the baseline scale
  }

  printf("%s", SDL_GetError());
  // TODO: is there something else I can do?
  return 1.f;
}

static void wlReportTabletEvent(ToolState* state)
{
  uint32_t eventType = SDL_FINGERMOTION;
  if(state->framePenDown != 0)
    eventType = state->framePenDown == 1 ? SDL_FINGERUP : SDL_FINGERDOWN;

  SDL_Event event = {
    .tfinger = {
      .type = eventType,
      .timestamp = SDL_GetTicks(),
      .touchId = state->toolType == ZWP_TABLET_TOOL_V2_TYPE_ERASER ? PenPointerEraser : PenPointerPen,
      .fingerId = state->buttons,
      .x = state->x,
      .y = state->y,
      .dx = state->tiltX,
      .dy = state->tiltY,
      .pressure = state->pressure,
      .windowID = 0, // unused
    }
  };

  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);

  // fprintf(stderr,
  //     "Wayland tablet event: touchId: %ld, x,y: (%f, %f), buttons: %ld,"
  //     "tilt: (%f, %f), pressure: %f",
  //     event.tfinger.touchId, event.tfinger.x, event.tfinger.y, event.tfinger.fingerId,
  //     event.tfinger.dx, event.tfinger.dy, event.tfinger.pressure);
}

static int wlToSDLButton(uint32_t b)
{
  // SDL_BUTTON_LMASK is used to represent pen down/pen up
  // SDL_BUTTON_MMASK represents side button pressed state
  // SDL_BUTTON_MMASK represents second side button pressed state
  switch (b) {
  // see %{_includedir}/linux/input-event-codes.h
  case 0x14b: // BTN_STYLUS
    return SDL_BUTTON_MMASK;
  case 0x14c: // BTN_STYLUS2
    return SDL_BUTTON_RMASK;
  // case 0x149: // BTN_STYLUS3
  //   return SDL_BUTTON_RMASK;
  default:
    return 0;
  }
}

static TouchPoint *
getTouchPoint(WlState *state, int32_t id)
{
  struct TouchEvent *touch = &state->touchEvent;
  const size_t nmemb = sizeof(touch->points) / sizeof(struct TouchPoint);
  int invalid = -1;
  for (size_t i = 0; i < nmemb; ++i) {
    if (touch->points[i].valid && touch->points[i].id == id) {
      return &touch->points[i];
    }
    if (invalid == -1 && !touch->points[i].valid) {
      invalid = i;
    }
  }
  if (invalid == -1) {
    return NULL;
  }
  touch->points[invalid].valid = true;
  touch->points[invalid].id = id;
  touch->points[invalid].eventMask = 0;
  return &touch->points[invalid];
}

static void reportTouchEvent(uint32_t eventType, uint32_t id,
                             wl_fixed_t surface_x, wl_fixed_t surface_y)
{
  float scale = getDisplayScaleFactor(wlState.window);

  SDL_Event event = {0};
  event.tfinger.type = eventType;
  // TODO: can I re-use time from wayland event?
  event.tfinger.timestamp = SDL_GetTicks();
  event.tfinger.touchId = 0;
  event.tfinger.fingerId = id;
  event.tfinger.x = wl_fixed_to_double(surface_x) * scale;
  event.tfinger.y = wl_fixed_to_double(surface_y) * scale;
  // TODO: is a value required for dx, dy (tilt) and pressure?

  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0); //SDL_PushEvent(&event);
}

static void touchHandlerDown(void *data, struct wl_touch *touch,
                             uint32_t serial, uint32_t time,
                             struct wl_surface *surface, int id,
                             wl_fixed_t x, wl_fixed_t y)
{
  struct TouchPoint *point = getTouchPoint(&wlState, id);
  if (point == NULL) {
    return;
  }
  point->eventMask |= TOUCH_EVENT_DOWN;
  point->surface_x = x;
  point->surface_y = y;
  wlState.touchEvent.time = time;
  wlState.touchEvent.serial = serial;
}

static void
touchHandlerUp(void *data, struct wl_touch *wl_touch, uint32_t serial,
               uint32_t time, int32_t id)
{
  struct TouchPoint *point = getTouchPoint(&wlState, id);
  if (point == NULL) {
    return;
  }
  point->eventMask |= TOUCH_EVENT_UP;
}

static void
touchHandlerMotion(void *data, struct wl_touch *wl_touch, uint32_t time,
               int32_t id, wl_fixed_t x, wl_fixed_t y)
{
  struct TouchPoint *point = getTouchPoint(&wlState, id);
  if (point == NULL) {
    return;
  }
  point->eventMask |= TOUCH_EVENT_MOTION;
  point->surface_x = x;
  point->surface_y = y;
  wlState.touchEvent.time = time;
}

static void
touchHandlerCancel(void *data, struct wl_touch *wl_touch)
{
  wlState.touchEvent.cancelled = true;
}

static void
touchHandlerFrame(void *data, struct wl_touch *wl_touch)
{
  struct TouchEvent *touch = &wlState.touchEvent;
  const size_t n = sizeof(touch->points) / sizeof(struct TouchPoint);

  for (size_t i = 0; i < n; i++) {
    struct TouchPoint *point = &touch->points[i];
    if (!point->valid) {
      continue;
    }

    if (point->eventMask & TOUCH_EVENT_DOWN) {
      reportTouchEvent(SDL_FINGERDOWN, point->id, point->surface_x,
                       point->surface_y);
    }

    if (point->eventMask & TOUCH_EVENT_UP) {
      reportTouchEvent(SDL_FINGERUP, point->id, point->surface_x,
                       point->surface_y);
    }

    if (point->eventMask & TOUCH_EVENT_MOTION) {
      reportTouchEvent(SDL_FINGERMOTION, point->id, point->surface_x,
                       point->surface_y);
    }

    if (touch->cancelled) {
      reportTouchEvent(SVGGUI_FINGERCANCEL, point->id, point->surface_x,
                       point->surface_y);
    }

    point->valid = false;
  }
  touch->cancelled = false;
}

static const struct wl_touch_listener touchListener = {
    .down = touchHandlerDown,
    .up = touchHandlerUp,
    .motion = touchHandlerMotion,
    .frame = touchHandlerFrame,
    .cancel = touchHandlerCancel,
};

static void handleSeatCapabilities(void* data, struct wl_seat* seat, 
    uint32_t capabilities)
{
  bool have_touch = capabilities & WL_SEAT_CAPABILITY_TOUCH;
  if (have_touch && wlState.touch == NULL) {
    wlState.touch = wl_seat_get_touch(wlState.seat);
    wl_touch_add_listener(wlState.touch, &touchListener, NULL);
  } else if (!have_touch && wlState.touch != NULL) {
    wl_touch_release(wlState.touch);
    wlState.touch = NULL;
  }
}

static void handleSeatName(void* data, struct wl_seat* seat, const char* name)
{
}

static const struct wl_seat_listener seatListener = {
  .capabilities = handleSeatCapabilities,
  .name = handleSeatName,
};

static void handleTabletToolType(void* data, struct zwp_tablet_tool_v2* tool, uint32_t type)
{
  ToolState* toolState = data;
  toolState->toolType = type;
}

static void handleHardwareSerial(void* data, struct zwp_tablet_tool_v2* tool, uint32_t hi, uint32_t lo)
{
}

static void handleHardwareIdWacom(void* data, struct zwp_tablet_tool_v2* tool, uint32_t hi, uint32_t lo)
{
}

void handleTabletToolCapability(void *data,
                                struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                                uint32_t capability)
{
}
void handleTabletToolDone(void *data,
                          struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2)
{
}
void handleTabletToolRemoved(void *data,
                             struct zwp_tablet_tool_v2 *tool)
{
  // TODO: use pointer artihmetic to find index
  for(int i = 0; i < MAX_TOOLS; i++) {
    if(wlState.tools[i].tool == tool) {
      wlState.tools[i] = (ToolState){0};
      break;
    }
  }

  zwp_tablet_tool_v2_destroy(tool);
}
void handleTabletToolProximityIn(void *data,
                                 struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                                 uint32_t serial,
                                 struct zwp_tablet_v2 *tablet,
                                 struct wl_surface *surface)
{
}
void handleTabletToolProximityOut(void *data,
                                  struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2)
{
}
void handleTabletToolDown(void *data,
                          struct zwp_tablet_tool_v2* tool,
                          uint32_t serial)
{
  ToolState* toolState = data;
  toolState->framePenDown = 2;
}
void handleTabletToolUp(void *data,
                        struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2)
{
  ToolState* toolState = data;
  toolState->framePenDown = 1;
}
void handleTabletToolMotion(void *data,
                            struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                            wl_fixed_t x,
                            wl_fixed_t y)
{
  ToolState* toolState = data;
  float scale = getDisplayScaleFactor(wlState.window);

  toolState->x = wl_fixed_to_double(x) * scale;
  toolState->y = wl_fixed_to_double(y) * scale;
  toolState->frameMotionSet = true;
}
void handleTabletToolPressure(void *data,
                              struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                              uint32_t pressure)
{
  ToolState* toolState = data;

  // according to spec, pressure is normalized to a value between 0 and 65535
  toolState->pressure = (float)pressure / 65535;
}
void handleTabletToolDistance(void *data,
                              struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                              uint32_t distance)
{
}
void handleTabletToolTilt(void *data,
                          struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                          wl_fixed_t tilt_x,
                          wl_fixed_t tilt_y)
{
  ToolState* toolState = data;
  // Wayland tilt is in degrees, relative to the z-axis of the tablet,
  // and is positive when the top of a tool tilts along the positive x
  // or y axis.
  // Our internal representation should be normalized to -1 .. 1
  toolState->tiltX = wl_fixed_to_double(tilt_x) / 90.f;
  toolState->tiltY = wl_fixed_to_double(tilt_y) / 90.f;
}
void handleTabletToolRotation(void *data,
                              struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                              wl_fixed_t degrees)
{
}
void handleTabletToolSlider(void *data,
                            struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                            int32_t position)
{
}
void handleTabletToolWheel(void *data,
                           struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                           wl_fixed_t degrees,
                           int32_t clicks)
{
}
void handleTabletToolButton(void *data,
                            struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                            uint32_t serial,
                            uint32_t button,
                            uint32_t state)
{
  ToolState* toolState = data;

  if(state == ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED)
    toolState->buttons |= wlToSDLButton(button);
  else // TODO: linuxtablet.c does a ^= , which seems weird to me
    toolState->buttons &= ~wlToSDLButton(button);
}
void handleTabletToolFrame(void *data,
                           struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                           uint32_t time)
{
  ToolState* toolState = data;

  wlReportTabletEvent(toolState);
  toolState->framePenDown = 0;
  toolState->frameMotionSet = false;
}

static const struct zwp_tablet_tool_v2_listener tabletToolListener = {
  .type = handleTabletToolType,
  .hardware_serial = handleHardwareSerial,
  .hardware_id_wacom = handleHardwareIdWacom,
  .capability = handleTabletToolCapability,
  .done = handleTabletToolDone,
  .removed = handleTabletToolRemoved,
  .proximity_in = handleTabletToolProximityIn,
  .proximity_out = handleTabletToolProximityOut,
  .down = handleTabletToolDown,
  .up = handleTabletToolUp,
  .motion = handleTabletToolMotion,
  .pressure = handleTabletToolPressure,
  .distance = handleTabletToolDistance,
  .tilt = handleTabletToolTilt,
  .rotation = handleTabletToolRotation,
  .slider = handleTabletToolSlider,
  .wheel = handleTabletToolWheel,
  .button = handleTabletToolButton,
  .frame = handleTabletToolFrame,
};

static void handleTabletAdded(void* data, struct zwp_tablet_seat_v2* tabSeat,
    struct zwp_tablet_v2 * tablet)
{
  // TODO: should I care
}

static void handleToolAdded(void* data, struct zwp_tablet_seat_v2* tabletSeat, 
    struct zwp_tablet_tool_v2* tool)
{
  if(wlState.tools[0].tool == tool)
    return;

  zwp_tablet_tool_v2_add_listener(tool, &tabletToolListener, &wlState.tools[0]);
}

static void handlePadAdded(void* data, struct zwp_tablet_seat_v2* tabletSeat, struct zwp_tablet_pad_v2* pad)
{
}

static const struct zwp_tablet_seat_v2_listener tabletSeatListener = {
  .tablet_added = handleTabletAdded,
  .tool_added = handleToolAdded,
  .pad_added = handlePadAdded,
};

static enum MimeType readMimeType(const char* mime)
{
  size_t len = sizeof(mimeTypeMap) / sizeof (char *);
  for(size_t i = 0; i < len; i++) {
    if (mimeTypeMap[i] == NULL)
      continue;

    if (strcmp(mime, mimeTypeMap[i])) {
      return i;
    }
  }

  return MIME_TYPE_UNSET;
}

static void dataOfferOffer(void* data, struct wl_data_offer* offer, const char *mime)
{
  enum MimeType mimeType = readMimeType(mime);

  if(mimeType == MIME_TYPE_UNSET)
    return;

  if(wlState.clipboardMimeType < mimeType) {
    wlState.clipboardMimeType = mimeType;
  }
}

static const struct wl_data_offer_listener dataOfferListener = {
  .offer = dataOfferOffer,
  .source_actions = NULL,
  .action = NULL,
};

static void dataOfferReset()
{
  if (wlState.dataOffer) {
    wl_data_offer_destroy(wlState.dataOffer);
    wlState.dataOffer = NULL;
  }

  wlState.clipboardMimeType = MIME_TYPE_UNSET;
}

static void dataDeviceOffer(void *data, struct wl_data_device* device, struct wl_data_offer* offer)
{
  dataOfferReset();
  wlState.dataOffer = offer;
  wl_data_offer_add_listener(offer, &dataOfferListener, NULL);
}

static void dataDeviceSelection(void *data, struct wl_data_device* device, struct wl_data_offer* offer)
{
  if(offer == NULL)
    dataOfferReset();
}

static const struct wl_data_device_listener dataDeviceListener = {
  .data_offer = dataDeviceOffer,
  .selection = dataDeviceSelection,
};

static void tryAddTabletSeat() {
  if(!wlState.tabletManager || !wlState.seat) {
    return;
  }

  if(wlState.tabletSeat) {
    // TODO: destroy old tablet seat?
    return;
  }

  wlState.tabletSeat = zwp_tablet_manager_v2_get_tablet_seat(wlState.tabletManager, wlState.seat);
  zwp_tablet_seat_v2_add_listener(wlState.tabletSeat, &tabletSeatListener, NULL);
}

static void tryAddDataDevice()
{
  if(!wlState.seat || !wlState.dataDeviceManager)
    return;

  if(wlState.dataDevice != NULL) {
    // TODO: destroy old device + clipboard data?
    return;
  }

  struct wl_data_device *dataDevice = wl_data_device_manager_get_data_device(wlState.dataDeviceManager, wlState.seat);

  if (!dataDevice)
    return;

  wlState.dataDevice = dataDevice;
  wl_data_device_add_listener(dataDevice, &dataDeviceListener, NULL);
}

static void registryHandleGlobal(void* data, struct wl_registry* registry, 
    uint32_t name, const char* interface, uint32_t version)
{
  if(strcmp(interface, wl_seat_interface.name) == 0) {
    // TODO: multi-seat?
    wlState.seat = wl_registry_bind(registry, name, &wl_seat_interface, 9);
    wl_seat_add_listener(wlState.seat, &seatListener, NULL);

    tryAddTabletSeat();
    tryAddDataDevice();
  } else if(strcmp(interface, zwp_tablet_manager_v2_interface.name) == 0) {
    wlState.tabletManager = wl_registry_bind(registry, name, &zwp_tablet_manager_v2_interface, 1);
    tryAddTabletSeat();
  } else if(strcmp(interface, wl_data_device_manager_interface.name) == 0) {
    wlState.dataDeviceManager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3);
    tryAddDataDevice();
  }
}

static void registryHandleGlobalRemove(void *data, 
    struct wl_registry *registry, uint32_t name)
{
  // TODO
}

static const struct wl_registry_listener registry_listener = {
  .global = registryHandleGlobal,
  .global_remove = registryHandleGlobalRemove,
};

// TODO: replace with c++ or something else
unsigned char *readAllFromFd(int fd, size_t *out_size)
{
  size_t cap = 4096;
  size_t len = 0;

  unsigned char *buf = malloc(cap);
  if (!buf)
    return NULL;

  for (;;) {
    if (len == cap) {
      size_t new_cap = cap * 2;

      unsigned char *new_buf = realloc(buf, new_cap);
      if (!new_buf) {
        free(buf);
        return NULL;
      }

      buf = new_buf;
      cap = new_cap;
    }

    ssize_t n = read(fd, buf + len, cap - len);

    if (n < 0) {
      if (errno == EINTR)
        continue;

      free(buf);
      return NULL;
    }

    if (n == 0)
      break; // EOF

    len += (size_t)n;
  }

  unsigned char *new_buf = realloc(buf, len + 1);
  if (new_buf)
    buf = new_buf;

  buf[len] = '\0';

  if (out_size)
    *out_size = len;

  return buf;
}

static unsigned char *textFromClipboard(size_t *size, enum MimeType* mimeType)
{
  if (wlState.dataOffer == NULL || wlState.clipboardMimeType == MIME_TYPE_UNSET) {
    return NULL;
  }

  // Prepare a pipe the other client can write its selection to us
  int fds[2];
  if (pipe(fds) == -1) {
    fprintf(stderr, "failed to create pipe");
    return NULL;
  }

  // fprintf(stderr, "receive from clipboard: mime-type=%s",
  //         mimeTypeMap[wlState.clipboardMimeType]);

  int read_fd = fds[0];
  int write_fd = fds[1];

  *mimeType = wlState.clipboardMimeType;

  // Give write-end of pipe to other client
  wl_data_offer_receive(wlState.dataOffer, mimeTypeMap[wlState.clipboardMimeType], write_fd);

  // Don't keep our copy of the write-end open (or we'll never get EOF)
  close(write_fd);

  unsigned char *data = readAllFromFd(read_fd, size);
  // TODO: error handling
  close(read_fd);
  return data;
}

int requestWlClipboard()
{
  size_t size;
  enum MimeType mimeType;
  unsigned char *text = textFromClipboard(&size, &mimeType);
  if(!text)
    return 0;

  bool img = false;
  if(mimeType == MIME_TYPE_APP_IMAGE_SVG_XML)
    img = true;

  clipboardFromBuffer(text, size, img);
  return 1;
}

int linuxInitWayland(SDL_Window* sdlwin)
{
  wlState.window = sdlwin;
  SDL_SysWMinfo wmInfo;
  SDL_VERSION(&wmInfo.version);
  if(!SDL_GetWindowWMInfo(sdlwin, &wmInfo))
    return 0;

  if (wmInfo.subsystem != SDL_SYSWM_WAYLAND) {
    return 0;
  }

  // TODO: doesn SDL expose wl_registry directly?
  struct wl_registry* registry = wl_display_get_registry(wmInfo.info.wl.display);
  wl_registry_add_listener(registry, &registry_listener, NULL);

  wl_display_roundtrip(wmInfo.info.wl.display);

  SDL_EventState(SDL_FINGERDOWN, SDL_DISABLE);
  SDL_EventState(SDL_FINGERMOTION, SDL_DISABLE);
  SDL_EventState(SDL_FINGERUP, SDL_DISABLE);

  return 0;
}
