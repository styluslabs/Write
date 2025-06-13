#include "linuxwayland.h"
#include "SDL_error.h"
#include "SDL_events.h"
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
  uint32_t type;
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

static void wlReportTabletEvent(uint32_t type, float x, float y)
{
  SDL_Event event = {
    .tfinger = {
      .type = type,
      .timestamp = SDL_GetTicks(),
      .touchId = PenPointerPen, // TODO
      .fingerId = 0, // TODO
      .x = x,
      .y = y,
      .dx = 0, // TODO
      .dy = 0, // TODO
      .pressure = 1.0, // TODO
      // .windowID = 0,    /**< The window underneath the finger, if any */
    }
  };

  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
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

static void handleTabletToolType(void* , struct zwp_tablet_tool_v2* , uint32_t )
{
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
  toolState->frame.type = SDL_FINGERDOWN;
}
void handleTabletToolUp(void *data,
                        struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2)
{
  ToolState* toolState = data;
  toolState->frame.type = SDL_FINGERUP;
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
  if(toolState->frame.type == 0)
    toolState->frame.type = SDL_FINGERMOTION;
}
void handleTabletToolPressure(void *data,
                              struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                              uint32_t pressure)
{
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
}
void handleTabletToolFrame(void *data,
                           struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                           uint32_t time)
{
  ToolState* toolState = data;
  if(toolState->frame.type == 0) {
    printf("bad frame: type == 0\n");
    // TODO: log?
    return;
  }

  // not sure if this is needed
  if((toolState->frame.type & SDL_FINGERMOTION) == 0)
    wlReportTabletEvent(SDL_FINGERMOTION, toolState->x, toolState->y);

  wlReportTabletEvent(toolState->frame.type, toolState->x, toolState->y);
  toolState->frame.type = 0;
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
