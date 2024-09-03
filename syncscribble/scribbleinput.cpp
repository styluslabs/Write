#include "scribbleinput.h"
#include "ugui/svggui.h"
#include "scribbleview.h"


// instance methods

int ScribbleInput::pressedKey = 0;
bool ScribbleInput::disableTouch = false;
bool ScribbleInput::simulatePenBtn = false;

ScribbleInput::ScribbleInput(ScribbleView* _parent) : parent(_parent) {}

void ScribbleInput::loadConfig()
{
  ScribbleConfig* cfg = parent->cfg;
  int stm = cfg->Int("singleTouchMode");
  singleTouchMode = stm > 2 ? INPUTMODE_DRAW : inputmode_t(stm);
  singleTouchModeMod = stm > 2 ? stm << 24 : 0;  // using MODEMOD_PRESSEDMASK bits
  multiTouchMode = (inputmode_t)cfg->Int("multiTouchMode");
  mouseMode = (inputmode_t)cfg->Int("mouseMode");
  penType = (PenType)cfg->Int("penType");
  //pressureScale = cfg->Float("pressureScale");
  palmThreshold = cfg->Float("palmThreshold");
}

static inputevent_t typeFromSDLFinger(int sdltype)
{
  if(sdltype == SDL_FINGERMOTION) return INPUTEVENT_MOVE;
  else if(sdltype == SDL_FINGERDOWN) return INPUTEVENT_PRESS;
  else if(sdltype == SDL_FINGERUP) return INPUTEVENT_RELEASE;
  else if(sdltype == SVGGUI_FINGERCANCEL) return INPUTEVENT_CANCEL;
  return INPUTEVENT_NONE;
}

bool ScribbleInput::sdlEvent(SvgGui* gui, SDL_Event* event)
{
  switch(event->type) {
  case SDL_FINGERDOWN:
  case SDL_FINGERMOTION:
  case SDL_FINGERUP:
  {
    inputsource_t inputsrc = INPUTSOURCE_TOUCH;
    int modemod = MODEMOD_NONE;
    if(event->tfinger.touchId == PenPointerPen || event->tfinger.touchId == PenPointerEraser) {
      // ignore pen hover motion?
      //if(event->type == SDL_FINGERMOTION &&
      //    !(enableHoverEvents || (scribbling != NOT_SCRIBBLING && currInputSource == INPUTSOURCE_PEN)))
      //  return true;

      inputsrc = INPUTSOURCE_PEN;
      modemod = (event->tfinger.fingerId & ~SDL_BUTTON_LMASK) ? MODEMOD_PENBTN : MODEMOD_NONE;
      if(event->tfinger.touchId == PenPointerEraser)
        modemod = MODEMOD_ERASE;
    }
    else if(event->tfinger.touchId == SDL_TOUCH_MOUSEID) {
      inputsrc = INPUTSOURCE_MOUSE;
      modemod = (event->tfinger.fingerId & SDL_BUTTON_RMASK) ? MODEMOD_PENBTN : 0;
    }
    if(event->type != SDL_FINGERMOTION || enableHoverEvents
        || (scribbling != NOT_SCRIBBLING && currInputSource == inputsrc)) {
      // add keyboard modifiers to modemod ... moved out of doInputEvent() to prevent affecting tests!
      // TODO: move this to ScribbleMode
      SDL_Keymod kbmod = SDL_GetModState();
      // on Linux, non-modifier key down (e.g. space bar) blocks mouse events, so Ctrl+Shift is alternate
      if(pressedKey == SDLK_SPACE || ((kbmod & KMOD_CTRL) && (kbmod & KMOD_SHIFT)))
        modemod |= MODE_PAN << 24;
      else if(kbmod & KMOD_SHIFT)
        modemod |= MODE_ERASE << 24;
      else if(kbmod & KMOD_CTRL)
        modemod |= MODE_SELECT << 24;
      else if(kbmod & KMOD_ALT)
        modemod |= MODE_INSSPACE << 24;
      else if(pressedKey == SDLK_VOLUMEUP || pressedKey == SDLK_VOLUMEDOWN)
        modemod |= MODEMOD_PENBTN;

      Point p = Point(event->tfinger.x, event->tfinger.y) - parent->screenOrigin;
      //Dim pr = (inputsrc == INPUTSOURCE_PEN ? pressureScale : 1) * event->tfinger.pressure;
      //doInputEvent(p.x, p.y, pr, inputsrc, typeFromSDLFinger(event->type), modemod, event->tfinger.timestamp);
      Dim maxw = std::max(event->tfinger.dx, event->tfinger.dy);
      InputEvent ievent(inputsrc, modemod, event->tfinger.timestamp, maxw);
      ievent.points.push_back(InputPoint(typeFromSDLFinger(event->type),
          p.x, p.y, event->tfinger.pressure, event->tfinger.dx, event->tfinger.dy));
      doInputEvent(ievent);
    }
    return true;
  }
  default:
    if(event->type == SvgGui::MULTITOUCH) {
      SDL_Event* fevent = static_cast<SDL_Event*>(event->user.data1);
      Dim maxw = std::max(fevent->tfinger.dx, fevent->tfinger.dy);
      InputEvent ievent(INPUTSOURCE_TOUCH, MODEMOD_NONE, event->user.timestamp, maxw);
      Point p0 = parent->screenOrigin;
      auto points = static_cast<std::vector<SDL_Finger>*>(event->user.data2);
      for(const SDL_Finger& pt : *points) {
        inputevent_t t = pt.id == fevent->tfinger.fingerId ? typeFromSDLFinger(fevent->type) : INPUTEVENT_NONE;
        ievent.points.push_back(InputPoint(t, pt.x - p0.x, pt.y - p0.y, pt.pressure > 0 ? pt.pressure : 1));
      }
      if(!ievent.points.empty())  // seems this can still happen, so prevent crash
        doInputEvent(ievent);
      return true;
    }
    return false;
  }
}

// used by ScribbleTest
void ScribbleInput::doInputEvent(Dim relx, Dim rely, Dim pressure, inputsource_t source, inputevent_t eventtype, int modemod, Timestamp t)
{
  InputEvent event(source, modemod, t);
  event.points.push_back(InputPoint(eventtype, relx, rely, pressure));
  doInputEvent(event);
}

// action can be canceled by escape key or by releasing mode/modifier button before cursor (if multitouch)
void ScribbleInput::cancelAction()
{
  if(scribbling == SCRIBBLING_DRAW)
    parent->doCancelAction();
  else if(scribbling == SCRIBBLING_PAN)  // || cancelpan)
    parent->panZoomCancel();
  scribbling = NOT_SCRIBBLING;
}

// assumes cancelAction() has been called
void ScribbleInput::forcePanMode(const InputEvent& event)
{
  scribbling = SCRIBBLING_PAN;
  parent->panZoomStart(event);
}

bool ScribbleInput::isTouchAccepted()
{
  // touch input can never override another input source
  return (currInputSource == INPUTSOURCE_TOUCH || scribbling == NOT_SCRIBBLING)
      && (singleTouchMode != INPUTMODE_NONE || multiTouchMode != INPUTMODE_NONE) && !disableTouch;
}

// TODO: test these!
static constexpr Dim PANLENGTH_CLICK = 14;  // max total pointer path dist for a ciick
static constexpr Dim PANLENGTH_DBLCLICK = 20;  // max distance between clicks for a double click
static constexpr int MAX_CLICK_TIME = 500;  // max press-to-release time for click
static constexpr int MAX_DBLCLICK_TIME = 500;  // max time from end of first click to end of second for double click
static constexpr int MIN_DBLCLICK_TIME = 40;  // min time from end of first click to end of second for double click

Point ScribbleInput::pointerCOM(const std::vector<InputPoint>& points)
{
  Dim x = 0, y = 0;
  int n = 0;
  for(unsigned int ii = 0; ii < points.size(); ii++) {
    if(points[ii].event != INPUTEVENT_RELEASE) {
      x += points[ii].x;
      y += points[ii].y;
      n++;
    }
  }
  return n > 0 ? Point(x/n, y/n) : Point(0,0);
}

// doInputEvent serves as a nexus for all input events from stylus, mouse, and touch
// It was originally introduced to allow for the possibility of move events generating press and release, in
//  particular for requiring a minimum pressure threshold for registering pen events.
// Qt does not pass coordinates for a release event, but Android does, so we are going to pass coordinates
//  for release events (but ignore them).
void ScribbleInput::doInputEvent(InputEvent& event)
{
#if 0
  static const char* srcs[] = { "NONE", "MOUSE", "PEN", "TOUCH" };
  static const char* events[] = { "RELEASE", "MOVE", "PRESS", "CANCEL", "NONE", "ENTER", "LEAVE" };
  for(InputPoint& p : event.points)
    PLATFORM_LOG("InputEvent(%.3f, %.3f, %.3f, %s, %s, %d) at %u\n",
        p.x, p.y, p.pressure, srcs[event.source], events[p.event+1], event.modemod, (unsigned int)event.t);
  //SCRIBBLE_LOG("ie(%.3f, %.3f, %.3f, %d, %d, %d);", relx, rely, pressure, (int)source, (int)eventtype, modemod);
#endif
  // ignored events
  if(event.source == INPUTSOURCE_TOUCH && !isTouchAccepted())
    return;
  if(event.source == INPUTSOURCE_MOUSE && mouseMode == INPUTMODE_NONE && (!enableHoverEvents || event.points[0].event == INPUTEVENT_PRESS))
    return;
#if PLATFORM_IOS || PLATFORM_ANDROID  // not tested on Android, but disabled by default
  // fat touch cancels input
  if(event.source == INPUTSOURCE_TOUCH && palmThreshold > 0 && event.maxTouchWidth > palmThreshold) {
    //PLATFORM_LOG("Rejecting palm!\n");
    cancelAction();
    return;
  }
#endif
  if(event.source == INPUTSOURCE_PEN && penType <= 0) {
    ScribbleConfig* upcfg = parent->cfg->getUpConfig();
    penType = DETECTED_PEN;
    singleTouchMode = INPUTMODE_PAN;  // switching to PAN instead of NONE seems less confusing
    mouseMode = INPUTMODE_PAN;  // PAN is less confusing than NONE and allows mouse to click links
    upcfg->set("penType", (int)penType);
    upcfg->set("singleTouchMode", singleTouchMode);
    upcfg->set("mouseMode", mouseMode);
    parent->loadConfig(parent->cfg);  // reload config (for selection handle size)
  }
  if(event.t == 0)
    event.t = mSecSinceEpoch();
  // allow for multiple press or release events
  int npoints = event.points.size();
  int nextpoints = npoints;
  int prevpoints = npoints;
  for(int ii = 0; ii < npoints; ii++) {
    if(event.points[ii].event == INPUTEVENT_RELEASE)
      nextpoints--;
    else if(event.points[ii].event == INPUTEVENT_PRESS)
      prevpoints--;
    else if(event.points[ii].event == INPUTEVENT_CANCEL) {
      nextpoints--;
      if(currInputSource == INPUTSOURCE_TOUCH)  // cancel is only sent for touch currently
        cancelAction();
    }
  }
  // centroid of pointer positions (== only point unless multi-touch)
  event.com = pointerCOM(event.points);

  // pen overrides all other input sources
  if(event.source == INPUTSOURCE_PEN && currInputSource != INPUTSOURCE_PEN)  //&& scribbling != NOT_SCRIBBLING)
    cancelAction();

  // ignore input event with unexpected number of points (this happens, e.g., with WM_POINTER since pointer
  //  update message with new pointer can preceed pointer down message)
  // hasn't been a problem on Android, so don't risk breaking anything
#ifdef QT_VERSION
  if(event.source == INPUTSOURCE_TOUCH && prevpoints != expectedPoints) {
    // this is meant to handle pen entering proximity when touch points down
    if(expectedPoints > 1 && nextpoints == 0)
      cancelAction();
    expectedPoints = nextpoints == 0 ? 0 : expectedPoints + (nextpoints - prevpoints);
    return;
  }
#endif

  bool finishing = nextpoints == 0 || (event.source == INPUTSOURCE_TOUCH
      && singleTouchMode != INPUTMODE_PAN && nextpoints == 1 && npoints > 1);
  expectedPoints = nextpoints;
  // any press event cancels kinetic scrolling
  if(npoints > prevpoints)
    parent->cancelScrolling();
  // special case of 2nd touch point pressed when singleTouchMode == DRAW - cancel draw and set NOT_SCRIBBLING
  //  so we can switch to SCRIBBLING_PAN
  if(currInputSource == INPUTSOURCE_TOUCH && event.source == INPUTSOURCE_TOUCH && scribbling == SCRIBBLING_DRAW && npoints > 1) {
    // && singleTouchMode == INPUTMODE_DRAW && prevpoints == 1
    parent->doCancelAction();
    scribbling = NOT_SCRIBBLING;
  }

  if(scribbling == NOT_SCRIBBLING) {
    if(npoints > prevpoints) {
      if(event.source == INPUTSOURCE_TOUCH && ((npoints == 1 && singleTouchMode == INPUTMODE_NONE)
          || (npoints > 1 && multiTouchMode == INPUTMODE_NONE)))
        return;
      else if((event.source == INPUTSOURCE_TOUCH && (npoints > 1 || singleTouchMode != INPUTMODE_DRAW))
          || (event.source == INPUTSOURCE_MOUSE && mouseMode == INPUTMODE_PAN)) {
        scribbling = SCRIBBLING_PAN;
        parent->panZoomStart(event);
      }
      else {
        currModeMod = event.modemod;
        if(event.source == INPUTSOURCE_TOUCH)
          event.modemod |= singleTouchModeMod;
        else if(event.source == INPUTSOURCE_PEN && simulatePenBtn)
          event.modemod |= MODEMOD_PENBTN;
        simulatePenBtn = false;
        scribbling = SCRIBBLING_DRAW;
        parent->doPressEvent(event);
      }
      currInputSource = event.source;
      pointerPathLen = 0;
      prevPointerCOM = event.com;
      initPointerTime = event.t;
      parent->doMotionEvent(event, INPUTEVENT_PRESS);  // used to give ScribbleArea focus on cursor down
    }
    else if(event.source != INPUTSOURCE_TOUCH)  // touch cannot send hover events
      parent->doMotionEvent(event, INPUTEVENT_MOVE);
  }
  else if(currInputSource == event.source) {
    // cancel and restart if pen button (incl. eraser btn) goes down (unless we're using scroll tab)
    if(event.source == INPUTSOURCE_PEN && event.modemod != currModeMod
        && (event.modemod == MODEMOD_ERASE || event.modemod == MODEMOD_PENBTN)) {
      parent->doCancelAction(false);  // refresh = false - don't do UI update until actual release event
      currModeMod = event.modemod;
      parent->doPressEvent(event);
      parent->doMotionEvent(event, INPUTEVENT_PRESS);
    }
    inputevent_t eventtype = INPUTEVENT_MOVE;
    if(npoints > prevpoints) {
      // >1 point now down
      eventtype = INPUTEVENT_PRESS;

      // TODO: need to figure out this case
      pointerPathLen = 0;
      prevPointerCOM = event.com;
      initPointerTime = event.t;
    }
    else if(nextpoints < npoints) {
      eventtype = INPUTEVENT_RELEASE;
      // click?
      if(finishing && pointerPathLen < PANLENGTH_CLICK && event.t - initPointerTime < MAX_CLICK_TIME) {
        event.modemod |= MODEMOD_CLICK;
        // thresholds are pretty generous - typical click qualify as fast click:
        if(pointerPathLen < PANLENGTH_CLICK/2 && event.t - initPointerTime < MAX_CLICK_TIME/2)
          event.modemod |= MODEMOD_FASTCLICK;
        int dcdt = event.t - lastClickTime;
        Dim dcdr = prevPointerCOM.dist(lastClickPos);
        if(dcdt < MAX_DBLCLICK_TIME && dcdt > MIN_DBLCLICK_TIME && dcdr < PANLENGTH_DBLCLICK)
          event.modemod |= MODEMOD_DBLCLICK;
        lastClickPos = (event.modemod & MODEMOD_DBLCLICK) ? Point(NAN, NAN) : prevPointerCOM;
        lastClickTime = (event.modemod & MODEMOD_DBLCLICK) ? 0 : event.t;
      }
    }
    else {
      pointerPathLen += (event.com - prevPointerCOM).dist();
      prevPointerCOM = event.com;
      // used for highlighting stuff between press and release of a click
      if(pointerPathLen < PANLENGTH_CLICK && event.t - initPointerTime < MAX_CLICK_TIME)
        event.modemod |= MODEMOD_MAYBECLICK;
    }

    if(scribbling == SCRIBBLING_PAN) {
      if(finishing) {
        parent->panZoomFinish(event);
        scribbling = NOT_SCRIBBLING;
      }
      else
        parent->panZoomMove(event, prevpoints, nextpoints);
    }
    else if(eventtype == INPUTEVENT_RELEASE) {
      parent->doReleaseEvent(event);
      currModeMod = MODEMOD_NONE;
      scribbling = NOT_SCRIBBLING;
    }
    else if(eventtype == INPUTEVENT_MOVE)
      parent->doMoveEvent(event);

    parent->doMotionEvent(event, scribbling == NOT_SCRIBBLING ? INPUTEVENT_RELEASE : INPUTEVENT_MOVE);
  }
  else
    return;
  // save timestamp of most recent event to any ScribbleInput to static lastEventTime - currently, this is
  //  used to suppress SWB view syncing for some amount of time after last user input
  lastEventTime = mSecSinceEpoch(); //event.t;  -- event timestamp origin is arbitrary
}
