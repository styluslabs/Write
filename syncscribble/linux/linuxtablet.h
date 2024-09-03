#ifndef LINUXTABLET_H
#define LINUXTABLET_H

#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Window;
union SDL_Event;
int linuxInitTablet(SDL_Window* sdlwin);
//void linuxPollTablet(void);
void linuxProcessXEvent(SDL_Event* event);
//void linuxCloseTablet(void);
int requestClipboard(SDL_Window* sdlwin);
#ifdef __cplusplus
}
#endif

#endif
