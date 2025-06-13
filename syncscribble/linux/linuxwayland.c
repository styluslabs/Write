#include "linuxwayland.h"
#include "SDL_syswm.h"
#include "tablet-v2.h"
#include "wayland-client-protocol.h"
#include "SDL_video.h"
#include <stdio.h>
#include <string.h>

typedef struct WlState {
  struct wl_seat* seat;
} WlState;

static WlState wlState;

static void handleSeatCapabilities(void* data, struct wl_seat* seat, 
    uint32_t capabilities)
{
  // TODO
}

static void handleSeatName(void* data, struct wl_seat* seat, const char* name)
{
  printf("handleSeatName: %s\n", name);
}

static const struct wl_seat_listener seatListener = {
  .capabilities = handleSeatCapabilities,
  .name = handleSeatName,
};

static void registryHandleGlobal(void* data, struct wl_registry* registry, 
    uint32_t name, const char* interface, uint32_t version)
{
  if(strcmp(interface, wl_seat_interface.name) == 0) {
    // TODO: multi-seat?
    wlState.seat = wl_registry_bind(registry, name, &wl_seat_interface, 9);
    wl_seat_add_listener(wlState.seat, &seatListener, NULL);
  } else if(strcmp(interface, zwp_tablet_manager_v2_interface.name) == 0);
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
