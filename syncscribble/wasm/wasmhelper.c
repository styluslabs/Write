#include <string.h>
#include "emscripten.h"
#include "emscripten/html5.h"
#include "ugui/svggui_platform.h"

// Refs:
// - www.w3.org/TR/pointerevents/
// - emscripten.org/docs/

// neither Chrome nor Firefox appear to access ptHimetricLocation anywhere, and thus do not support subpixel
//  resolution on Windows, even though x,y were changed from int to float

void emPtrEvent(const char* device, int evtype, int ptrid, int buttons, double x, double y, double pr, double t)
{
  static int mouseBtns = 0;
  SDL_Event event = {0};
  if(strcmp(device, "pen") == 0) {
    event.tfinger.touchId = buttons & 32 ? PenPointerEraser : PenPointerPen;
    event.tfinger.fingerId = buttons;
  }
  else if(strcmp(device, "touch") == 0) {
    event.tfinger.touchId = 0;
    event.tfinger.fingerId = ptrid;
  }
  else if(strcmp(device, "mouse") == 0) {
    // swap bits 1 and 2 (if they are different) - middle button / right button
    if((buttons & 0x6) == 0x4 || (buttons & 0x6) == 0x2)
      buttons ^= 0x6;
    event.tfinger.touchId = SDL_TOUCH_MOUSEID;
    event.tfinger.fingerId = (evtype == 1) ? buttons : (buttons ^ mouseBtns);
    mouseBtns = buttons;
  }
  else
    return;

  event.type = evtype == 1 ? SDL_FINGERDOWN : (evtype == -1 ? SDL_FINGERUP : SDL_FINGERMOTION);
  event.tfinger.x = x;
  event.tfinger.y = y;
  //event.tfinger.dx = tiltX;  // use dx, dy for tilt
  //event.tfinger.dy = tiltY;
  event.tfinger.pressure = pr;
  event.tfinger.timestamp = t > 0 ? t : SDL_GetTicks();
  // PeepEvents bypasses gesture recognizer and event filters
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
}

EM_JS(void, jsSetupPtrEvents, (),
{
  const emPtrEvent = Module.cwrap('emPtrEvent',
      null, ['string', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const ptrevent = function(ev, evtype) {
    //console.log(ev);
    if(evtype == 0 && typeof ev.getCoalescedEvents === "function") {  // only for move events
      const evs = ev.getCoalescedEvents();
      //console.log(evs);
      for(const e of evs) {
        emPtrEvent(e.pointerType, evtype, e.pointerId, e.buttons, e.clientX, e.clientY, e.pressure, e.timeStamp);
      }
    }
    else {
      emPtrEvent(ev.pointerType, evtype, ev.pointerId, ev.buttons, ev.clientX, ev.clientY, ev.pressure, ev.timeStamp);
    }
  };

  var el = document.getElementById("canvas");
  // Register pointer event handlers
  el.onpointerdown = function(ev) { ptrevent(ev, 1); };
  el.onpointermove = function(ev) { ptrevent(ev, 0); };
  el.onpointerup   = function(ev) { ptrevent(ev, -1); };
  el.onpointercancel = function(ev) { ptrevent(ev, -1); };
});

void wasmSetupInput()
{
  emscripten_set_mousemove_callback("#canvas", NULL, 0, NULL);
  emscripten_set_mousedown_callback("#canvas", NULL, 0, NULL);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, NULL, 0, NULL);
  emscripten_set_touchstart_callback("#canvas", NULL, 0, NULL);
  emscripten_set_touchend_callback("#canvas", NULL, 0, NULL);
  emscripten_set_touchmove_callback("#canvas", NULL, 0, NULL);
  emscripten_set_touchcancel_callback("#canvas", NULL, 0, NULL);
  jsSetupPtrEvents();
}
