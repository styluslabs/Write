#include "linuxwayland.h"
#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_mouse.h"
#include "SDL_syswm.h"
#include "SDL_timer.h"
#include "SDL_version.h"
#include "tablet-v2.h"
#include "ugui/svggui_platform.h"
#include "wayland-client-protocol.h"
#include "SDL_video.h"
#include "wayland-util.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define MAX_TOOLS 32

typedef struct FrameInfo {
  uint32_t toolType;

  // 0 means not set this frame
  // 1 means pen up
  // 2 means pen down
  int penDown;

  bool moved;
  float tiltX; // normalized to -1 .. +1
  float tiltY; // normalized to -1 .. +1
  float pressure; // normalized to 0..1
  unsigned int buttons;
} FrameInfo;

typedef struct ToolState {
  struct zwp_tablet_tool_v2* tool;
  SDL_Window* window; // TODO: move somewhere else
  float x;
  float y;
  FrameInfo frame;
} ToolState;

typedef struct WlState {
  SDL_Window* window;
  struct wl_seat* seat;
  struct zwp_tablet_manager_v2* tabletManager;
  struct zwp_tablet_seat_v2* tabletSeat;
  ToolState tools[MAX_TOOLS];
} WlState;

static WlState wlState = {0};

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
  if(state->frame.penDown != 0)
    eventType = state->frame.penDown == 1 ? SDL_FINGERUP : SDL_FINGERDOWN;

  SDL_Event event = {
    .tfinger = {
      .type = eventType,
      .timestamp = SDL_GetTicks(),
      .touchId = state->frame.toolType == ZWP_TABLET_TOOL_V2_TYPE_ERASER ? PenPointerEraser : PenPointerPen,
      .fingerId = state->frame.buttons,
      .x = state->x,
      .y = state->y,
      .dx = state->frame.tiltX,
      .dy = state->frame.tiltY,
      .pressure = state->frame.pressure,
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

static void handleSeatCapabilities(void* data, struct wl_seat* seat, 
    uint32_t capabilities)
{
  // TODO
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
  toolState->frame.toolType = type;
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
  toolState->frame.penDown = 2;
}
void handleTabletToolUp(void *data,
                        struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2)
{
  ToolState* toolState = data;
  toolState->frame.penDown = 1;
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
  toolState->frame.moved = true;
}
void handleTabletToolPressure(void *data,
                              struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                              uint32_t pressure)
{
  ToolState* toolState = data;

  // according to spec, pressure is normalized to a value between 0 and 65535
  toolState->frame.pressure = (float)pressure / 65535;
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
  toolState->frame.tiltX = wl_fixed_to_double(tilt_x) / 90.f;
  toolState->frame.tiltY = wl_fixed_to_double(tilt_y) / 90.f;
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
    toolState->frame.buttons |= wlToSDLButton(button);
  else // TODO: linuxtablet.c does a ^= , which seems weird to me
    toolState->frame.buttons &= ~wlToSDLButton(button);
}
void handleTabletToolFrame(void *data,
                           struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                           uint32_t time)
{
  ToolState* toolState = data;

  wlReportTabletEvent(toolState);
  toolState->frame.penDown = 0;
  toolState->frame.moved = false;
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

static void registryHandleGlobal(void* data, struct wl_registry* registry, 
    uint32_t name, const char* interface, uint32_t version)
{
  if(strcmp(interface, wl_seat_interface.name) == 0) {
    // TODO: multi-seat?
    wlState.seat = wl_registry_bind(registry, name, &wl_seat_interface, 9);
    wl_seat_add_listener(wlState.seat, &seatListener, NULL);
  } else if(strcmp(interface, zwp_tablet_manager_v2_interface.name) == 0) {
    wlState.tabletManager = wl_registry_bind(registry, name, &zwp_tablet_manager_v2_interface, 1);
    // TODO: is wlState.seat guaranteed to be valid at this point?
    wlState.tabletSeat = zwp_tablet_manager_v2_get_tablet_seat(wlState.tabletManager, wlState.seat);
    zwp_tablet_seat_v2_add_listener(wlState.tabletSeat, &tabletSeatListener, NULL);
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

  // do I need this?
  wl_display_roundtrip(wmInfo.info.wl.display);

  return 0;
}
