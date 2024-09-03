#include "scribbleview.h"
#include "usvg/svgpainter.h"
#include "scribblewidget.h"


const Dim ScribbleView::zoomSteps[] = {0.1, 0.125, 0.15, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9,
                                       1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5, 6, 7, 8, 9, 10};
#define NUM_ZOOM_STEPS (sizeof(zoomSteps)/sizeof(zoomSteps[0]))
const unsigned int ScribbleView::DEFAULT_ZOOM_IDX = 12;  // default zoom = 1
const int ScribbleView::IMAGEBUFFER_BORDER = 200;
Dim ScribbleView::unitsPerPx = 1;

// constants for pan/zoom behavior (these should be reviewed w/ new 150 dpi input reference)
// const int and constexpr int are the same, but not so for floats (!)
//static constexpr Dim TOUCH_TOLERANCE = 2;  // pixels
//const unsigned int MAX_CLICK_TIME = 500;  // milliseconds
// for rejecting two finger touch near edge of screen (likely to be accidental)
static constexpr Dim TOUCH_REJECT_FRAC = 0.1;
static constexpr Dim TOUCH_REJECT_PIX = 75;
// w/ single touch pan is disabled, so that two fingers are needed to pan, fingers closer than this will only pan - not zoom
static Dim TOUCH_MIN_ZOOM_POINTER_DIST = 150;  // was 150 * preScale
// touch points closer than this dist are assumed to be unintended and result in zoom/pan cancellation
static Dim TOUCH_MIN_POINTER_DIST = 40;

// kinetic scrolling
static constexpr size_t MAX_PREV_INPUT = 12;  // maximum length of prevInput list
// minimum time diff (in seconds) between points added to list
static constexpr Dim MIN_INPUT_DT = 0.005;
// minimum time period (in seconds) over which to calculate fling velocity
static constexpr Dim FLING_AVG_TIME = 0.05;
// these were previously read from config and multiplied by preScale (~1.33 on X1 Yoga) - need to review
// ... but if any of these vars should be in config, it's probably the touch rejection constants
static constexpr Dim FLING_DRAG = 0;
static constexpr Dim FLING_DECEL = 100;
static constexpr Dim FLING_MIN_V = 200;


ScribbleView::ScribbleView()
{
  zoomStepsIdx = DEFAULT_ZOOM_IDX;
  // shouldn't really be necessary since we expect loadConfig to be called
  // just pick some random but valid size to start
  screenRect = Rect::wh(100, 100);
  scribbleInput.reset(new ScribbleInput(this));
}

// should be called from child class constructor and whenever config changes
void ScribbleView::loadConfig(ScribbleConfig* _cfg)
{
  cfg = _cfg;
  preScale = 1; //cfg->Float("preScale"); -- do not remove preScale yet!
  mScale = mZoom * preScale;
  timerPeriod = cfg->Int("timerPeriod", 50);
  TOUCH_MIN_ZOOM_POINTER_DIST = cfg->Float("touchMinZoomPtrDist", 150);
  TOUCH_MIN_POINTER_DIST = cfg->Float("touchMinPtrDist", 40);
  scribbleInput->loadConfig();
}

void ScribbleView::doResizeEvent(const Rect& newsize)
{
  screenRect = newsize.toSize();
  viewportRect = screenToDim(screenRect.toSize());

  doCancelAction();
  pageSizeChanged();
  doRefresh();
}

// should be called for content or screen size change - we assume min/maxOriginX/Y have already been updated
void ScribbleView::pageSizeChanged()
{
  // ensure that position is still valid
  doPan(0,0);
  Dim scrpos = maxOriginY > minOriginY ? (maxOriginY - rawyoffset)/(maxOriginY - minOriginY) : -1;
  if(widget)
    widget->setScrollPosition(scrpos, viewportRect.height()/(viewportRect.height() + maxOriginY - minOriginY));
}

bool ScribbleView::doTimerEvent(Timestamp)
{
  //bool needrefresh = false;
  if(flingV.x != 0 || flingV.y != 0) {
    // stop kinetic scrolling if we've hit a limit
    // TODO: can we move this into doPan?
    //Dim totalxoffset = xorigin + panxoffset;
    //Dim totalyoffset = yorigin + panyoffset;
    bool xlimit = rawxoffset <= minOriginX || rawxoffset >= maxOriginX;
    bool ylimit = rawyoffset <= minOriginY || rawyoffset >= maxOriginY;
    flingV = Point(xlimit ? 0 : flingV.x, ylimit ? 0 : flingV.y);
    doKineticScroll();
    //needrefresh = true;
    doRefresh();
  }
  return flingV.x != 0 || flingV.y != 0;
}

void ScribbleView::doCancelAction(bool refresh)
{
  // cancel kinetic scrolling
  cancelScrolling();
}

// pos is in Dim space - move to (0,0) in screen space
void ScribbleView::setCornerPos(Point pos)
{
  Point screenpos = dimToScreen(pos);
  doPan(-screenpos.x, -screenpos.y);
}

void ScribbleView::setCenterPos(Point pos)
{
  Point screenpos = dimToScreen(pos);
  Point ul = dimToScreen(Point(-10,-10));
  doPan(MIN(-ul.x, Dim(0.5)*getViewWidth() - screenpos.x),
        MIN(-ul.y, Dim(0.5)*getViewHeight() - screenpos.y));
}

// pan to place pos.y at top of viewport; only pan x position if necessary
void ScribbleView::setVisiblePos(Point pos)
{
  Point screenpos = dimToScreen(pos);
  if(screenpos.x > 0 && screenpos.x < getViewWidth()/2)
    screenpos.x = 0;
  doPan(-screenpos.x, -screenpos.y);
}

// set a new zoom value
void ScribbleView::setZoom(Dim newZoom)
{
  mZoom = MIN(Dim(cfg->Float("MAX_ZOOM")), MAX(Dim(cfg->Float("MIN_ZOOM")), newZoom));
  mScale = mZoom * preScale;
  viewportRect = screenToDim(screenRect.toSize()); /// imgPaint->getSize());
  pageSizeChanged();
}

// change zoom by scale factor s, centered at point px, py
void ScribbleView::zoomBy(Dim s, Dim px, Dim py)
{
  zoomTo(mZoom*s, px, py);
}

// set new zoom value, centered at point px, py
void ScribbleView::zoomTo(Dim newZoom, Dim px, Dim py)
{
  Point p0 = screenToDim(Point(px, py));
  setZoom(newZoom);
  // change origin so that screen point (px, py) corresponds to
  //  same canvas point as before zoom change
  Point p1 = dimToScreen(p0);
  doPan(px - p1.x, py - p1.y);
}

void ScribbleView::zoomIn()
{
  if(mZoom != zoomSteps[zoomStepsIdx])
    roundZoom(getViewWidth()/2, getViewHeight()/2);
  if(zoomStepsIdx < NUM_ZOOM_STEPS-1)
    zoomTo(zoomSteps[++zoomStepsIdx], getViewWidth()/2, getViewHeight()/2);
}

void ScribbleView::zoomOut()
{
  if(mZoom != zoomSteps[zoomStepsIdx])
    roundZoom(getViewWidth()/2, getViewHeight()/2);
  if(zoomStepsIdx > 0)
    zoomTo(zoomSteps[--zoomStepsIdx], getViewWidth()/2, getViewHeight()/2);
}

void ScribbleView::resetZoom()
{
  zoomStepsIdx = DEFAULT_ZOOM_IDX;
  zoomTo(zoomSteps[zoomStepsIdx], getViewWidth()/2, getViewHeight()/2);
}

int ScribbleView::nearestZoomStep(Dim zoom) const
{
  int idx = 0;
  if(zoom <= zoomSteps[0])
    idx = 0;
  else if(zoom >= zoomSteps[NUM_ZOOM_STEPS-1])
    idx = NUM_ZOOM_STEPS-1;
  else {
    for(unsigned int ii = 1; ii < NUM_ZOOM_STEPS; ++ii) {
      if(zoomSteps[ii-1] <= zoom && zoom < zoomSteps[ii]) {
        idx = (zoom/zoomSteps[ii-1] < zoomSteps[ii]/zoom) ? ii-1 : ii;
        break;
      }
    }
  }
  return idx;
}

void ScribbleView::roundZoom(Dim px, Dim py)
{
  // round current zoom to nearest step
  zoomStepsIdx = nearestZoomStep(mZoom);
  if(zoomSteps[zoomStepsIdx] != mZoom)
    zoomTo(zoomSteps[zoomStepsIdx], px, py);
}

// viewport is independent of pan offsets
//Rect ScribbleView::imageToDim(const Rect& r) const
//{
//  return Rect::ltrb((r.left - xorigin)/mScale, (r.top - yorigin)/mScale,
//      (r.right - xorigin)/mScale, (r.bottom - yorigin)/mScale);
//}
//
//Rect ScribbleView::dimToImage(const Rect& r) const
//{
//  return Rect::ltrb(r.left*mScale + xorigin, r.top*mScale + yorigin,
//      r.right*mScale + xorigin, r.bottom*mScale + yorigin);
//}

Point ScribbleView::screenToDim(const Point& p) const
{
  return Point((p.x - xorigin - panxoffset)/mScale, (p.y - yorigin - panyoffset)/mScale);
}

Rect ScribbleView::screenToDim(const Rect& r) const
{
  return Rect::ltrb((r.left - xorigin - panxoffset)/mScale, (r.top - yorigin - panyoffset)/mScale,
      (r.right - xorigin - panxoffset)/mScale, (r.bottom - yorigin - panyoffset)/mScale);
}

Point ScribbleView::dimToScreen(const Point& p) const
{
  return Point(p.x*mScale + xorigin + panxoffset, p.y*mScale + yorigin + panyoffset);
}

Rect ScribbleView::dimToScreen(const Rect& r) const
{
  return Rect::ltrb(r.left*mScale + xorigin + panxoffset, r.top*mScale + yorigin + panyoffset,
      r.right*mScale + xorigin + panxoffset, r.bottom*mScale + yorigin + panyoffset);
}

Point ScribbleView::screenToGlobal(const Point& p) const
{
  return p + screenOrigin;
}

Point ScribbleView::globalToScreen(const Point& p) const
{
  return p - screenOrigin;
}

bool ScribbleView::isVisible(const Rect& r) const
{
  return r.overlaps(screenToDim(screenRect));
}

void ScribbleView::reqRepaint()
{
  if(!dirtyRectDim.isValid() && !dirtyRectScreen.isValid())
    return;
  Rect dirty = dirtyRectDim;
  dirty.rectIntersect(viewportRect);
  dirtyRectScreen.rectUnion(dimToScreen(dirty));
  // rectToQRect fails badly if Rect dimensions exceed int limits
  dirtyRectScreen.rectIntersect(screenRect);

  if(widget)
    widget->node->setDirty(SvgNode::PIXELS_DIRTY);
}

void ScribbleView::repaintAll(bool imagedirty)
{
  if(imagedirty)
    dirtyRectDim = viewportRect;
  dirtyRectScreen = screenRect;
}

void ScribbleView::scrollFrac(Dim dx, Dim dy)
{
  scrollBy(int(dx*(maxOriginX - minOriginX) + 0.5), int(dy*(maxOriginY - minOriginY) + 0.5));
}

Point ScribbleView::getScrollFrac()
{
  Dim scrollx = maxOriginX > minOriginX ? (panxoffset + xorigin)/(maxOriginX - minOriginX) : 0;
  Dim scrolly = maxOriginY > minOriginY ? (panyoffset + yorigin)/(maxOriginY - minOriginY) : 0;
  return Point(scrollx, scrolly);
}

void ScribbleView::scrollBy(int dx, int dy)
{
  if(!panning) {
    doPan(dx, dy);
    doRefresh();
  }
}

void ScribbleView::doPan(Dim dx, Dim dy)
{
  // note we're assuming xorigin, yorigin == 0
  rawxoffset = std::max(minOriginX, std::min(rawxoffset + dx, maxOriginX));
  rawyoffset = std::max(minOriginY, std::min(rawyoffset + dy, maxOriginY));
  if(quantize(rawxoffset, unitsPerPx) == panxoffset && quantize(rawyoffset, unitsPerPx) == panyoffset)
    return;
  panxoffset = quantize(rawxoffset, unitsPerPx);  //totalxoffset - xorigin;
  panyoffset = quantize(rawyoffset, unitsPerPx);  //totalyoffset - yorigin;
  dirtyRectScreen = screenRect;
  viewportRect = screenToDim(screenRect.toSize());
  // note that we use rawyoffset here since panyoffset could be slightly out of range
  Dim scrpos = maxOriginY > minOriginY ? (maxOriginY - rawyoffset)/(maxOriginY - minOriginY) : -1;

  if(widget) {
    widget->showScroller();
    widget->setScrollPosition(scrpos, viewportRect.height()/(viewportRect.height() + maxOriginY - minOriginY));
  }
}

// pan/zoom and kinetic scrolling - moved back here from ScribbleInput

Dim ScribbleView::pointerDist(const std::vector<InputPoint>& points)
{
  Dim dx = points[0].x - points[1].x;
  Dim dy = points[0].y - points[1].y;
  return sqrt(dx*dx + dy*dy);
}

void ScribbleView::panZoomStart(const InputEvent& event)
{
  initPanOrigin = screenToDim(Point(0,0));
  initPanZoom = mZoom;
  currPanLength = 0;
  prevPointerDist = 0;
  maxPointerDist = 0;
  panZoomMove(event, 0, event.points.size());
}

void ScribbleView::panZoomMove(const InputEvent& event, int prevpoints, int nextpoints)
{
  int npoints = event.points.size();
  Point com = event.com;
  if(prevpoints < npoints) {
    if(npoints > 1) {
      prevPointerDist = pointerDist(event.points);
      if(scribbleInput->singleTouchMode != INPUTMODE_PAN && prevPointerDist < TOUCH_MIN_ZOOM_POINTER_DIST)
        prevPointerDist = 0;
    }
    prevPointerCOM = com;
    prevInput.clear();
    prevInput.push_front(InputPoint(INPUTEVENT_PRESS, com.x, com.y, 0));
    prevPointerTime = event.t;
    initPointerTime = prevPointerTime;
    return;
  }
  // reject touches starting near edge (making sure not to block pan from edge events!)
  Rect touchacceptrect = Rect(screenRect).pad(
      -MIN(screenRect.width()*TOUCH_REJECT_FRAC, preScale*TOUCH_REJECT_PIX),
      -MIN(screenRect.height()*TOUCH_REJECT_FRAC, preScale*TOUCH_REJECT_PIX));
  // if we started inside touch accept region, currPanLength will be > 0; we check
  // ... actually, let's not reject single touch input anywhere in screen
  //|| (scribbleInput->singleTouchMode == INPUTMODE_PAN && event.source == INPUTSOURCE_TOUCH))
  if(currPanLength == 0 && npoints > 1 && !touchacceptrect.contains(prevPointerCOM))
    return;

  if(npoints >= 2 && nextpoints < 2) {
    if(mZoom != initPanZoom)
      roundZoom(prevPointerCOM.x, prevPointerCOM.y);
    //initPanZoom = mZoom;'
    prevPointerCOM = com;
    return;
  }

  const Dim TOUCH_TOLERANCE = 0.5*unitsPerPx;  // 0.5 px
  if(npoints > 1 && scribbleInput->multiTouchMode == INPUTMODE_ZOOM && prevPointerDist > 0) {
    Dim newPointerDist = pointerDist(event.points);
    maxPointerDist = std::max(maxPointerDist, newPointerDist);
    if(newPointerDist < preScale*TOUCH_MIN_POINTER_DIST
        || newPointerDist / prevPointerDist < 0.5 || newPointerDist / prevPointerDist > 2) {
      scribbleInput->cancelAction();
      return;
    }
    if(ABS(newPointerDist - prevPointerDist) >= TOUCH_TOLERANCE) {
      zoomBy(newPointerDist / prevPointerDist, com.x, com.y);
      prevPointerDist = newPointerDist;
    }
  }
  // don't call this fn unless you want to pan!
  Dim dx = com.x - prevPointerCOM.x;
  Dim dy = com.y - prevPointerCOM.y;
  if(ABS(dx) >= TOUCH_TOLERANCE || ABS(dy) >= TOUCH_TOLERANCE) {
    doPan(dx, dy);
    currPanLength += sqrt(dx*dx + dy*dy);
    Dim dt = (Dim)0.001 * (Dim)(event.t - prevPointerTime);  // milliseconds to seconds
    // save point if it isn't too soon after prev point
    if(dt > MIN_INPUT_DT) {
      while(prevInput.size() >= MAX_PREV_INPUT)
        prevInput.pop_back();
      //SCRIBBLE_LOG("Saved point: %.3f, %.3f, dt = %f", com.x, com.y, dt);
      // hack: we're storing dt in pressure
      prevInput.push_front(InputPoint(INPUTEVENT_MOVE, com.x, com.y, dt));
      prevPointerTime = event.t;
    }
    prevPointerCOM = com;
  }
}

void ScribbleView::panZoomFinish(const InputEvent& event)
{
  // any zooming prevents clicking or kinetic scroll
  if(mZoom != initPanZoom) {
    roundZoom(prevPointerCOM.x, prevPointerCOM.y);
    return;
  }
  // this prevents attempted zooming when at zoom limits from triggering kinetic scroll
  if(maxPointerDist > TOUCH_MIN_ZOOM_POINTER_DIST)
    return;
  if(event.modemod & MODEMOD_CLICK) {
    // undo the little bit of panning
    setCornerPos(initPanOrigin);
    if(event.modemod & MODEMOD_DBLCLICK)
      doDblClickAction(prevPointerCOM);
    else if(doClickAction(prevPointerCOM)) {
      // if click was accepted, don't use for potential double click
      //lastClickPos = Point();
      scribbleInput->lastClickTime = 0;  // bit of a hack now that click handling lives in ScribbleInput
    }
  }
  else if(prevInput.size() > 1 && FLING_MIN_V > 0 && currPanLength > TOUCH_MIN_POINTER_DIST) {
    // include time between last move event and release event in dt
    Dim dt = Dim(0.001) * Dim(event.t - prevPointerTime);
    auto ii = prevInput.begin();
    for(; ii+1 != prevInput.end() && dt < FLING_AVG_TIME; ++ii)
      dt += ii->pressure;  // we're storing dt in pressure
    // if gesture duration is less than FLING_AVG_TIME, it is ignored
    // minimum time requirement together with min velocity req gives us a minimum gesture distance req!
    //if(ii != prevInput.end()) {
      Dim velx = (prevInput.front().x - ii->x)/dt;
      Dim vely = (prevInput.front().y - ii->y)/dt;
      // only fling along one axis (zero the smaller component of flingV); not sure about this
      if(ABS(velx) < ABS(vely))
        velx = 0;
      else
        vely = 0;
      Dim minflinginitv = FLING_MIN_V + FLING_DECEL;
      if(velx*velx + vely*vely >= minflinginitv*minflinginitv) {
        flingV = Point(velx, vely);
        if(dot(flingV, prevFlingV) > 0)  // only add previous velocity if in same direction!
          flingV += prevFlingV;
        prevFlingV = Point(0, 0);
        if(widget)
          widget->startTimer(timerPeriod);
      }
    //}
  }
}

void ScribbleView::panZoomCancel()
{
  if(mZoom != initPanZoom)
    setZoom(initPanZoom);
  setCornerPos(initPanOrigin);
}

void ScribbleView::doKineticScroll()
{
  // fling gesture scrolling
  if(flingV.x != 0 || flingV.y != 0) {
    //SCRIBBLE_LOG("flingV: %f, %f", flingV.x, flingV.y);
    doPan(flingV.x * (timerPeriod/1000), flingV.y * (timerPeriod/1000));
    // alternative deceleration curve
    //Dim v2 = flingV.x*flingV.x + flingV.y*flingV.y;
    //Dim newv = sqrt(v2 - flingEnergyLoss);
    Dim v = flingV.dist();
    Dim newv = (1 - FLING_DRAG)*v - FLING_DECEL*(timerPeriod/33);  // FLING_DECEL was set for 30 fps default
    if(newv > FLING_MIN_V) {
      Dim s = newv/v;
      flingV.x *= s;
      flingV.y *= s;
    }
    else
      flingV = Point(0,0);
  }
}

// cancel kinetic scrolling
void ScribbleView::cancelScrolling()
{
  prevFlingV = flingV;
  flingV = Point(0, 0);
}

// drawing stuff

/*
void ScribbleView::scrollImage()
{
  if(panxoffset < -2*IMAGEBUFFER_BORDER || panxoffset > 0) {
    int imagedx = panxoffset + IMAGEBUFFER_BORDER;
    //imgPaint.scroll(imagedx, 0);
    imgPaint->drawImage(Rect::ltwh(imagedx, 0, contentImage->width, contentImage->height), *contentImage);
    xorigin += imagedx;
    panxoffset -= imagedx;
    Rect newViewportRect = imageToDim(imgPaint->getSize());
    Rect dirtyX = newViewportRect;
    if(imagedx < 0)
      dirtyX.left = viewportRect.right;
    else if(imagedx > 0)
      dirtyX.right = viewportRect.left;
    viewportRect = newViewportRect;
    //willupdate = dirtyX.overlaps(screenToDim(srect));
    //emit paintImg(dirtyX, willupdate ? srect : Rect());
    updateImage(dirtyX);
  }
  if(panyoffset < -2*IMAGEBUFFER_BORDER || panyoffset > 0) {
    int imagedy = panyoffset + IMAGEBUFFER_BORDER;
    //imgPaint.scroll(0, imagedy);
    imgPaint->drawImage(Rect::ltwh(0, imagedy, contentImage->width, contentImage->height), *contentImage);
    yorigin += imagedy;
    panyoffset -= imagedy;
    Rect newViewportRect = imageToDim(imgPaint->getSize());
    Rect dirtyY = newViewportRect;
    if(imagedy < 0)
      dirtyY.top = viewportRect.bottom;
    else if(imagedy > 0)
      dirtyY.bottom = viewportRect.top;
    viewportRect = newViewportRect;
    //willupdate = dirtyY.overlaps(screenToDim(srect));
    //emit paintImg(dirtyY, willupdate ? srect : Rect());
    updateImage(dirtyY);
  }
}

void ScribbleView::updateImage(Rect dirty)
{
  dirty.rectIntersect(viewportRect);
  if(!dirty.isValid())
    return;
  // NOTE: currently, calling reset() after beginFrame() will incorrectly clear -y scale needed
  //imgPaint->reset();
  imgPaint->beginFrame();

  // with OpenGL, have to draw entire orignal image, then draw on top of it
  imgPaint->drawImage(Rect::wh(contentImage->width, contentImage->height), *contentImage);

  imgPaint->translate(xorigin, yorigin);
  imgPaint->scale(mScale, mScale);
  imgPaint->setClipRect(dirty);
  // ensure that dirty rect is bigger than clip rect
  dirty = dirty.pad(1/mScale);
  drawImage(imgPaint, dirty);
#ifdef DEBUG_RENDERING
  // draw dirty rect for testing
  //imgPaint.fillRect(dirty, Painter::setAlpha(Painter::GREEN, 0x3F));
  SCRIBBLE_LOG("Rendering image (Dim LTRB): %.3f %.3f %.3f %.3f", dirty.left, dirty.top, dirty.right, dirty.bottom);
#endif
  imgPaint->endFrame();
}
*/

// Drawing overview:
// 1. strokes are drawn to image (contained in imgPaint Painter); only dirty region (as determined from currPage->getDirty())
//   and any regions not previously included in image are redrawn by passing dirty rect to Layer::qtDraw.
//  - viewportRect is region of Dim space covered by image (not the visible screen region!)
// 2. image is drawn to screen, clipped by dirty screen region (set by update() calls and determined from QPaintEvent::rect())
//  - clipping rect for screen is not implemented for skia ... because of how double buffering mechanism works, it may
//   not actually be any faster (we're drawing to an old buffer ... last buffer is being used by compositor)
// 3. selection "background" highlighting (semi-transparent) is drawn direcly to screen, clipped by screen dirty rect
// 4. current stroke is drawn directly to screen
// To force a complete redraw, delete image
// Debugging steps:
// 1. First try switching drawImage to repaint entire screen in paintEvent
// 2. Then try not using dirty rect in updateImage

void ScribbleView::doPaintEvent(Painter* painter, const Rect& dirty)
{
#ifdef QT_CORE_LIB
  int imgwidth = getViewWidth() + 2*IMAGEBUFFER_BORDER;
  int imgheight = getViewHeight() + 2*IMAGEBUFFER_BORDER;
  if(imgPaint->getSize() != Rect::ltwh(0, 0, imgwidth, imgheight)) {
    delete imgPaint;
    delete contentImage;
    contentImage = new Image(imgwidth, imgheight);
    imgPaint = new Painter(contentImage);
    xorigin += panxoffset + IMAGEBUFFER_BORDER;
    yorigin += panyoffset + IMAGEBUFFER_BORDER;
    panxoffset = -IMAGEBUFFER_BORDER;
    panyoffset = -IMAGEBUFFER_BORDER;
    // viewportRect is in Dim space
    viewportRect = imageToDim(imgPaint->getSize());
    dirtyRectDim = viewportRect;
    dirtyRectScreen = screenRect;
  }
  else
    scrollImage();

  updateImage(dirtyRectDim);
#endif

  dirtyRectDim = Rect();
  dirtyRectScreen = dirty.isValid() ? Rect(dirty).rectIntersect(screenRect) : screenRect;
  //painter->clipRect(screenRect);  -- this is done by ScribbleWidget
  painter->save();
  painter->translate(xorigin + panxoffset, yorigin + panyoffset);
  painter->scale(mScale, mScale);
  if(cfg->Bool("invertColors"))
    painter->setColorXorMask(color_t(cfg->Int("colorXorMask")));
  drawImage(painter, screenToDim(dirtyRectScreen));
  painter->restore();

  if(cfg->Bool("invertColors"))
    painter->setColorXorMask(color_t(cfg->Int("colorXorMask")));
  drawScreen(painter, dirtyRectScreen);
  //painter->endFrame();
  dirtyRectScreen = Rect();
  ++frameCount;
}
