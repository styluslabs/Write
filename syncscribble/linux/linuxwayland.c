#include "linuxwayland.h"
#include "SDL_syswm.h"
#include "tablet-v2.h"
#include "wayland-client-protocol.h"
#include "SDL_video.h"
#include <stdio.h>
#include <string.h>

typedef struct WlState {
  struct wl_seat* seat;
  struct zwp_tablet_manager_v2* tabletManager;
  struct zwp_tablet_seat_v2* tabletSeat;
} WlState;

static WlState wlState = {0};

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
                             struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2)
{
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
                          struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                          uint32_t serial)
{
}
void handleTabletToolUp(void *data,
                        struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2)
{
}
void handleTabletToolMotion(void *data,
                            struct zwp_tablet_tool_v2 *zwp_tablet_tool_v2,
                            wl_fixed_t x,
                            wl_fixed_t y)
{
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
  zwp_tablet_tool_v2_add_listener(tool, &tabletToolListener, NULL);
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
