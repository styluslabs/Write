#pragma once

#include <deque>
#include <memory>
#include "ulib/painter.h"
#include "scribbleconfig.h"
#include "scribblemode.h"
#include "scribbleinput.h"

struct InputPoint;
class ScribbleWidget;

class ScribbleView
{
friend class ScribbleInput;
public:
  ScribbleView();
  virtual ~ScribbleView() {}

  virtual void loadConfig(ScribbleConfig* _cfg);
  //Dim getPreScale() const { return preScale; }
  void scrollBy(int dx, int dy);
  void scrollFrac(Dim dx, Dim dy);
  Point getScrollFrac();

  Dim getZoom() const { return mZoom; }
  Dim getScale() const { return mScale; }
  int getViewWidth() const { return screenRect.right; }
  int getViewHeight() const { return screenRect.bottom; }
  void repaintAll(bool imagedirty = true);

  virtual void doPressEvent(const InputEvent& event) {}
  virtual void doMoveEvent(const InputEvent& event) {}
  virtual void doReleaseEvent(const InputEvent& event) {}
  virtual bool doClickAction(Point pos) { return true; }
  virtual void doDblClickAction(Point pos) {}
  virtual void doLongPressAction(Point pos) {}
  virtual void doMotionEvent(const InputEvent& event, inputevent_t eventtype) {}
  virtual void doCancelAction(bool refresh = true);
  virtual bool doTimerEvent(Timestamp t);
  virtual void doRefresh() { reqRepaint(); }

  void panZoomStart(const InputEvent& event);
  void panZoomMove(const InputEvent& event, int prevpoints, int nextpoints);
  void panZoomFinish(const InputEvent& event);
  void panZoomCancel();
  void doKineticScroll();
  void cancelScrolling();

  Rect dirtyRectScreen;
  Rect dirtyRectDim;
  std::unique_ptr<ScribbleInput> scribbleInput;
  ScribbleConfig* cfg = NULL;
  ScribbleWidget* widget = NULL;

  static Dim unitsPerPx;

protected:
  friend class ScribbleWidget;

  virtual void drawImage(Painter* imgpaint, const Rect& dirty) {}
  virtual void drawScreen(Painter* painter, const Rect& dirty) {}
  virtual void doPan(Dim dx, Dim dy);
  virtual void pageSizeChanged();

  void doPaintEvent(Painter* qpainter, const Rect& dirty = Rect());
  void doResizeEvent(const Rect& newsize);

  static const Dim zoomSteps[];
  static const unsigned int DEFAULT_ZOOM_IDX;
  unsigned int zoomStepsIdx = 0;

  void setCornerPos(Point pos);
  void setCenterPos(Point pos);
  void setVisiblePos(Point pos);
  int nearestZoomStep(Dim zoom) const;
  // zoom stuff is only called via ScribbleArea
  void setZoom(Dim newZoom);
  void zoomTo(Dim newZoom, Dim px, Dim py);
  void zoomBy(Dim s, Dim px, Dim py);
  virtual void roundZoom(Dim px, Dim py);
  void zoomIn();
  void zoomOut();
  void resetZoom();

  Dim pointerDist(const std::vector<InputPoint>& points);
  Point dimToScreen(const Point& p) const;
  Rect dimToScreen(const Rect& r) const;
  Point screenToDim(const Point& p) const;
  Rect screenToDim(const Rect& r) const;
  //Rect imageToDim(const Rect& r) const;
  //Rect dimToImage(const Rect& r) const;
  Point screenToGlobal(const Point& p) const;
  Point globalToScreen(const Point& p) const;
  bool isVisible(const Rect& r) const;
  void reqRepaint();
  //void updateImage(Rect dirty);
  //void scrollImage();
  //Rect drawTextwithBG(Painter* p, const char* str, Dim x, Dim y, Color textcolor, Color boxcolor);

  //Image* contentImage = NULL;
  //Painter* imgPaint = NULL;
  //Painter* screenPaint = NULL;
  bool panning = false;
  Rect viewportRect;
  Rect screenRect;
  Point screenOrigin;
  Dim mZoom = 1;
  Dim preScale = 1;
  Dim mScale = 1;
  //Dim fontSize;
  Dim xorigin = 0;
  Dim yorigin = 0;
  Dim rawxoffset = 0;
  Dim rawyoffset = 0;
  Dim panxoffset = 0;
  Dim panyoffset = 0;
  Dim minOriginX = 0;
  Dim minOriginY = 0;
  Dim maxOriginX = 0;
  Dim maxOriginY = 0;
  // for FPS display
  int frameCount = 0;
  // for pan/zoom
  Point prevPointerCOM;
  Dim prevPointerDist = 0;
  Point initPanOrigin;
  Dim initPanZoom = 0;
  Dim currPanLength = 0;
  Dim maxPointerDist = 0;
  // for kinetic scrolling
  Point flingV;
  Point prevFlingV;
  Timestamp initPointerTime = 0;
  Timestamp prevPointerTime = 0;
  std::deque<InputPoint> prevInput;
  // kinetic scrolling constants
  Dim timerPeriod = 50;  // in ms

  static const int IMAGEBUFFER_BORDER;
};
