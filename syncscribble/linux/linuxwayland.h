#ifndef LINUXWAYLAND_H
#define LINUXWAYLAND_H

#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Window;
union SDL_Event;
int linuxInitWayland(struct SDL_Window* sdlwin);
// int requestClipboard(struct SDL_Window* sdlwin);
#ifdef __cplusplus
}
#endif

#endif
