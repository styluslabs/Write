#ifndef SCRIBBLEINPUT_H
#define SCRIBBLEINPUT_H

#include <vector>
#include "basics.h"
#include "ulib/geom.h"

enum inputevent_t {INPUTEVENT_RELEASE=-1, INPUTEVENT_MOVE=0, INPUTEVENT_PRESS=1, INPUTEVENT_CANCEL=2,
                   INPUTEVENT_NONE=3, INPUTEVENT_ENTER=4, INPUTEVENT_LEAVE=5};
enum inputsource_t {INPUTSOURCE_NONE=0, INPUTSOURCE_MOUSE=1, INPUTSOURCE_PEN=2, INPUTSOURCE_TOUCH=3};
enum inputmode_t {INPUTMODE_NONE=0, INPUTMODE_PAN=1, INPUTMODE_DRAW=2, INPUTMODE_ZOOM=2};

struct InputPoint {
  Dim x;
  Dim y;
  Dim pressure;
  Dim tiltX;
  Dim tiltY;
  inputevent_t event;
  //unsigned int id;

  InputPoint(inputevent_t _event, Dim _x = 0, Dim _y = 0, Dim _p = 1, Dim _tiltX = 0, Dim _tiltY = 0)
    : x(_x), y(_y), pressure(_p), tiltX(_tiltX), tiltY(_tiltY), event(_event) {}
};

struct InputEvent {
  Timestamp t;
  int modemod;
  inputsource_t source;
  Point com;
  Dim maxTouchWidth;
  std::vector<InputPoint> points;

  InputEvent(inputsource_t _source = INPUTSOURCE_NONE, int _modemod = 0, Timestamp _t = 0, Dim maxw = 0)
      : t(_t), modemod(_modemod), source(_source), maxTouchWidth(maxw) {}
};

class ScribbleView;
class ScribbleConfig;
union SDL_Event;
class SvgGui;

class ScribbleInput
{
public:
  ScribbleView* parent;

  enum scribbling_t {NOT_SCRIBBLING=0, SCRIBBLING_PAN=1, SCRIBBLING_DRAW=3}; //SCRIBBLING_PANZOOM=2
  scribbling_t scribbling = NOT_SCRIBBLING;
  inputsource_t currInputSource = INPUTSOURCE_NONE;
  bool panning = false;
  int currModeMod = 0;
  int expectedPoints = 0;
  Timestamp lastEventTime = 0;
  bool enableHoverEvents = false;
  // for double clicking
  Point prevPointerCOM;
  Dim pointerPathLen = 0;
  Timestamp initPointerTime = 0;
  Point lastClickPos;
  Timestamp lastClickTime = 0;

  //Dim pressureScale = 1;
  Dim palmThreshold = 0;
  inputmode_t multiTouchMode;
  inputmode_t singleTouchMode;
  inputmode_t mouseMode;
  int singleTouchModeMod = 0;

  // The original value of DETECTED_PEN was 10; everytime we make changes that allow for more specific
  //  pen detection, we should increment the value of DETECTED_PEN, which will result in detectPenType()
  //  being rerun once after user installs update
  enum PenType {NO_PEN = 0, THINKPAD_PEN = 1, ICS_PEN = 2, SAMSUNG_PEN = 3, TEGRA_PEN = 4,
                REDETECT_PEN = 10, DETECTED_PEN = 11} penType;  // FORCED_PEN = -1,

  static int pressedKey;
  static bool disableTouch;
  static bool simulatePenBtn;

  ScribbleInput(ScribbleView* _parent);
  void loadConfig();
  void doInputEvent(Dim relx, Dim rely, Dim pressure, inputsource_t source, inputevent_t eventtype, int modemod, Timestamp t);
  bool isTouchAccepted();
  void doInputEvent(InputEvent& event);
  void cancelAction();  //bool cancelpan = false);
  void forcePanMode(const InputEvent& event);
  bool sdlEvent(SvgGui* gui, SDL_Event* event);

  static Point pointerCOM(const std::vector<InputPoint>& points);
};

#endif
