#include "scribblearea.h"

#include "usvg/svgpainter.h"
#include "scribbledoc.h"
#include "scribbleapp.h"
#include "scribblewidget.h"
#include "strokebuilder.h"
#include "bookmarkview.h"


const Dim ScribbleArea::ERASESTROKE_RADIUS = 7;
const Dim ScribbleArea::ERASEFREE_RADIUS = 7;
const Dim ScribbleArea::PATHSELECT_RADIUS = 7;
const Dim ScribbleArea::MIN_LASSO_POINT_DIST = 2;
const Dim ScribbleArea::GROW_STEP = 40;
const Dim ScribbleArea::GROW_TRIGGER = 1.625;  // in multiples of GROW_STEP or ruling
const Dim ScribbleArea::GROW_EXTRA = 2.5;  // in multiples of GROW_STEP or ruling
const Dim ScribbleArea::AUTOSCROLL_BORDER = 60;
const Dim ScribbleArea::MIN_CURSOR_RADIUS = 2;
const Color ScribbleArea::BACKGROUND_COLOR = 0xFF444444;

Image* ScribbleArea::watermark = NULL;
#if !PLATFORM_MOBILE
bool ScribbleArea::staticInited = false;
struct SDL_Cursor_Deleter { void operator()(void* x) { if(x) SDL_FreeCursor(static_cast<SDL_Cursor*>(x)); } };
std::unique_ptr<SDL_Cursor, SDL_Cursor_Deleter> ScribbleArea::penCursor;
std::unique_ptr<SDL_Cursor, SDL_Cursor_Deleter> ScribbleArea::panCursor;
std::unique_ptr<SDL_Cursor, SDL_Cursor_Deleter> ScribbleArea::eraseCursor;
#endif

ScribbleArea::ScribbleArea() : ScribbleView()
{
  currMode = MODE_NONE;
  posHistoryPos = posHistory.begin();
  // always need hover events to check for mouse motion so we can reset cursor to default
  scribbleInput->enableHoverEvents = true;
}

// Members that are affected by config values get set here.  Right now, these are just config values that are
//  accessed very frequently and so "cached"
void ScribbleArea::loadConfig(ScribbleConfig* _cfg)
{
  ScribbleView::loadConfig(_cfg);
  pageSpacing = cfg->Float("pageSpacing");
  viewMode = (viewmode_t)cfg->Int("viewMode");
  centerPages = cfg->Bool("centerPages");
  reflowWordSep = cfg->Float("minWordSep", 0.3f);
  selColMode = RuledSelector::ColMode(cfg->Int("columnDetectMode"));
  drawCursor = cfg->Int("drawCursor");
  //scribbleInput->enableHoverEvents = (drawCursor == 2);
#ifdef ONE_TIME_TIPS
  showHelpTips = scribbleDoc->scribbleMode && (app->oneTimeTip("ghostpage") || app->oneTimeTip("scalesel") ||
      app->oneTimeTip("rotatesel") || app->oneTimeTip("movesel") || app->oneTimeTip("cropsel") ||
      app->oneTimeTip("rectsel"));
#endif
  // this is done here because we need to update immediately when pen is detected
  RectSelector::HANDLE_PAD = cfg->Int("singleTouchMode") == INPUTMODE_DRAW ? 8 : 4;

#if !PLATFORM_MOBILE
  if(!staticInited) {
    staticInited = true;
    // cursors
    const Dim penradius = MIN_CURSOR_RADIUS/unitsPerPx;
    const Dim eraserradius = ERASESTROKE_RADIUS/unitsPerPx;

    Image penimg(int(2*penradius + 3.5), int(2*penradius + 3.5));
    Painter penpaint(Painter::PAINT_SW | Painter::NO_TEXT, &penimg);
    penpaint.setBackgroundColor(Color::TRANSPARENT_COLOR);
    penpaint.beginFrame();
    penpaint.setStroke(Color::WHITE, 1);
    penpaint.setFillBrush(Color::BLACK);
    penpaint.drawPath(Path2D().addEllipse(penradius + 1, penradius + 1, penradius+0.5, penradius+0.5));
    penpaint.endFrame();
    SDL_Surface* pensurf = SDL_CreateRGBSurfaceFrom((void*)penimg.bytes(),
        penimg.width, penimg.height, 32, 4*penimg.width, Color::R, Color::G, Color::B, Color::A);
    penCursor.reset(SDL_CreateColorCursor(pensurf, int(penradius + 1.5), int(penradius + 1.5)));
    SDL_FreeSurface(pensurf);

    Image eraserimg(int(2*eraserradius + 3), int(2*eraserradius + 3));
    Painter eraserpaint(Painter::PAINT_SW | Painter::NO_TEXT, &eraserimg);
    eraserpaint.setBackgroundColor(Color::TRANSPARENT_COLOR);
    eraserpaint.beginFrame();
    eraserpaint.setAntiAlias(true);
    eraserpaint.setFillBrush(Color::WHITE);
    eraserpaint.setStrokeBrush(Color::BLACK);
    eraserpaint.setStrokeWidth(1);
    eraserpaint.drawPath(Path2D().addEllipse(eraserradius + 1, eraserradius + 1, eraserradius, eraserradius));
    eraserpaint.endFrame();
    SDL_Surface* erasersurf = SDL_CreateRGBSurfaceFrom((void*)eraserimg.bytes(),
        eraserimg.width, eraserimg.height, 32, 4*eraserimg.width, Color::R, Color::G, Color::B, Color::A);
    eraseCursor.reset(SDL_CreateColorCursor(erasersurf, int(eraserradius + 1.5), int(eraserradius + 1.5)));
    SDL_FreeSurface(erasersurf);

    panCursor.reset(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND));
  }
#endif
#ifdef SCRIBBLE_IAP
  if(!watermark && !iosIsPaid()) {
    Dim w = 227, h = 113;
    watermark = new Image(w, h, Image::PNG);
    Painter wmpaint(Painter::PAINT_SW, watermark);
    wmpaint.setBackgroundColor(0x00FFFFFF);  //Color::TRANSPARENT_COLOR);
    wmpaint.beginFrame();
    wmpaint.setFontSize(40);
    //wmpaint.setCompOp(Painter::CompOp_Src);  // don't blend w/ BG
    //nvgGlobalCompositeBlendFuncSeparate(Painter::vg, NVG_ONE, NVG_ZERO, NVG_ONE, NVG_ONE_MINUS_SRC_ALPHA);
    wmpaint.setFillBrush(Color(0xFF000000));
    wmpaint.setStroke(Color(0xFFFFFFFF), 1.0);
    wmpaint.rotate(-45*M_PI/180.0);
    for(Dim y = 0; y < h + w; y += 40) {
      for(Dim x = 0; x < 2*y && x < 1.414*w; x += 400)
        wmpaint.drawText(x - y + 20, y, "Stylus Labs Write");
    }
    wmpaint.endFrame();
    // fill + stroke w/ alpha < 1 doesn't give desired effect...
    int nbytes = watermark->dataLen();
    unsigned char* bytes = watermark->bytes();
    for(int ii = 3; ii < nbytes; ii += 4)
      bytes[ii] >>= 3;  // alpha /= 8
  }
#endif
}

void ScribbleArea::reset()
{
  doCancelAction();
  recentStrokes.clear();  // occasionally saw crashes ... document being opened while still in MODE_STROKE?
  posHistory.clear();
  posHistoryPos = posHistory.begin();
  if(currSelection)
    clearSelection();
  currPage = NULL;
}

Page* ScribbleArea::page(int n) const
{
  return n < numPages() ? scribbleDoc->document->pages[n] : NULL;
}

int ScribbleArea::numPages() const
{
  return scribbleDoc->document->numPages();
}

// changed from 375x625 and 0.5 zoom to 240x400 and 0.32 zoom to decrease thumbnail size
void ScribbleArea::drawThumbnail(Image* dest)
{
  //Painter thumbpaint(Painter::PAINT_SW, dest);  // note sRGB disabled
  Painter& thumbpaint = *Application::painter;  // need to use GL painter for ScribbleTest
  thumbpaint.setTarget(dest);
  thumbpaint.beginFrame();
  thumbpaint.save();
  // aliasing in thumbnails is quite noticible on high-DPI displays
  thumbpaint.setAntiAlias(true);
  // thumbPaint.reset();  -- should make thumbPaint a member of ScribbleArea
  Rect dirty = thumbpaint.deviceRect;
  // for narrow pages, set zoom so that page width fills preview
  Dim minscale = cfg->Int("saveThumbnail") == 2 ? 0.32 : 0.25;  // ... for tests
  Dim scale = std::max(minscale, dirty.width()/currPage->width());
  dirty.right /= scale;
  dirty.bottom /= scale;
  Point dimpos(0,0);
  if(!cfg->Bool("thumbFirstPage")) {
    dimpos = screenToDim(Point(0,0));
    // avoid useless grey space at bottom of thumbnail, if possible
    if(currPageNum == numPages() - 1)
      dimpos.y = std::min(dimpos.y, currPageYOrigin + currPage->height() - dirty.bottom);
    // no grey space on top or left (and none at right if possible)
    dimpos.x = std::max(Dim(0), std::min(dimpos.x, currPageXOrigin + currPage->width() - dirty.right));
    dimpos.y = std::max(Dim(0), dimpos.y);
  }
  dirty.translate(dimpos.x, dimpos.y);
  // this doesn't handle split view case in general!
  Element::FORCE_NORMAL_DRAW = true;  // suppress STROKEDRAW_SEL
  // setup painter
  thumbpaint.scale(scale, scale);
  thumbpaint.translate(-dimpos.x, -dimpos.y);
  drawImage(&thumbpaint, dirty);  // draw content
  // draw page number
  thumbpaint.restore();
  Element::FORCE_NORMAL_DRAW = false;
  // saveThumbnail == 2 for tests to hide page number
  /*if(cfg->Int("saveThumbnail") != 2) {
    dirty = thumbpaint.getSize();
    int dd = int(5*preScale + 0.5);  // text offset
    int n = cfg->Bool("thumbFirstPage") ? 0 : currPageNum;  // or hide page num if thumbFirstPage?
    thumbpaint.setBrush(Brush(TEXT_FG_COLOR));
    thumbpaint.setFontSize(fontSize);
    thumbpaint.setTextAlign(Painter::AlignRight | Painter::AlignBottom);
    drawTextwithBG(&thumbpaint, fstring("%d / %d", n+1, numPages()).c_str(),
        dirty.right - dd, dirty.bottom - dd, TEXT_FG_COLOR, TEXT_BG_COLOR);
  }*/
  thumbpaint.endFrame();
  thumbpaint.setTarget(NULL);
}

Point ScribbleArea::getPageOrigin(int pagenum) const
{
  Point origin(0, 0);
  Page* p = pagenum == numPages() ? scribbleDoc->ghostPage.get() : page(pagenum);
  if(viewMode == VIEWMODE_SINGLE || !p) {}
  else if(viewMode == VIEWMODE_VERT) {
    if(centerPages)
      origin.x = (contentWidth - p->width())/2;
    for(int ii = 0; ii < pagenum; ii++)
      origin.y += page(ii)->height() + pageSpacing;
  }
  else if(viewMode == VIEWMODE_HORZ) {
    if(centerPages)
      origin.y = (contentHeight - p->height())/2;
    for(int ii = 0; ii < pagenum; ii++)
      origin.x += page(ii)->width() + pageSpacing;
  }
  return origin;
}

void ScribbleArea::zoomCenter(Dim newZoom, bool snap)
{
  zoomTo(newZoom, getViewWidth()/2, getViewHeight()/2);
  if(snap)
    roundZoom(getViewWidth()/2, getViewHeight()/2);
  uiChanged(UIState::Zoom);
  doRefresh();
}

void ScribbleArea::roundZoom(Dim px, Dim py)
{
  // should we still snap to width, height if continuous zoom enabled? use continuousZoom > 1?
  if(cfg->Bool("continuousZoom"))
    return;
  Dim xborder = cfg->Float("horzBorder");
  Dim wzoom = (getViewWidth() - 2*xborder)/currPage->width()/preScale;
  Dim hzoom = getViewHeight()/currPage->height()/preScale;
  Dim zoom = mZoom;
  ScribbleView::roundZoom(px, py);
  // zoom = 100% always has priority
  if(mZoom == 1) {}
  else if(wzoom < 1.1*zoom && zoom < 1.1*wzoom && (wzoom > 1.05 || wzoom < 0.95)) {
    Point currCorner = screenToDim(Point(0, 0));
    Point pageCorner = pageDimToDim(Point(0, 0));
    zoomTo(wzoom, px, py);
    setCornerPos(Point(pageCorner.x - xborder/mScale, currCorner.y));
  }
  else if(hzoom < 1.1*zoom && zoom < 1.1*hzoom && (hzoom > 1.05 || hzoom < 0.95)) {
    setZoom(hzoom);
    setCenterPos(pageDimToDim(Point(currPage->width()/2, currPage->height()/2)));
  }
}

void ScribbleArea::doPan(Dim dx, Dim dy)
{
  ScribbleView::doPan(dx, dy);
  if(viewMode == VIEWMODE_SINGLE || (currMode != MODE_NONE && currMode != MODE_PAN))
    return;

  Rect screenrect = screenToDim(screenRect);
  Rect pagerect = pageDimToDim(currPage->rect());
  // shrink screenrect so page changes before prev page is completely invisible
  screenrect.pad(-screenrect.width()/6, -screenrect.height()/6);
  if(!pagerect.overlaps(screenrect)) {
    int firstvispage = dimToPageNum(Point(screenrect.left, screenrect.top));
    if(currPageNum < firstvispage)
      setPageNum(firstvispage);
    else {
      int lastvispage = dimToPageNum(Point(screenrect.right, screenrect.bottom));
      if(currPageNum > lastvispage)
        setPageNum(lastvispage);
      else
        return;
    }
    uiChanged(UIState::Pan);
  }
#ifdef ONE_TIME_TIPS
  if(showHelpTips && dimToPageNum(Point(screenrect.right, screenrect.bottom + 80)) == numPages()) {
    app->oneTimeTip("ghostpage", Point(screenRect.center().x, screenRect.bottom + 80),
      _("Double tap or drag selection here to add a new page."));
  }
#endif
}

void ScribbleArea::pageSizeChanged()
{
  repaintAll();
  updateContentDim();
  ScribbleView::pageSizeChanged();
  Point origin = getPageOrigin(currPageNum);
  currPageXOrigin = origin.x;
  currPageYOrigin = origin.y;
  // zoom, page number now displayed by UI, so must update
  uiChanged(UIState::PageSizeChange);
}

// pageCountChanged handles change in number or ordering of pages - tries to preserve view position
// pagenum indicates that page that was inserted or removed
void ScribbleArea::pageCountChanged(int pagenum, int prevpages)
{
  int prevpagenum = currPageNum;
  Point prevpos = dimToPageDim(screenToDim(Point(0,0)));
  // make sure current page is valid and consistent with page number
  setPageNum(currPageNum);
  pageSizeChanged();
  int currpages = numPages();
  if(prevpages >= 0 && pagenum >= 0 && prevpages != currpages && pagenum <= prevpagenum)
    prevpagenum += prevpages < currpages ? 1 : -1;
  // TODO: update items in posHistory!
  // don't jump if first or last page removed
  if(prevpagenum < 0)
    gotoPage(-1);  // go to beginning of first page
  else if(currpages < prevpages && pagenum >= currpages)
    gotoPage(currpages);  // go to end of last page
  else
    gotoPos(prevpagenum, prevpos, false);
}

void ScribbleArea::setPageDims(Dim width, Dim height, bool pending)
{
  PageProperties props = currPage->getProperties();
  if((width <= 0 || props.width == width) && (height <= 0 || props.height == height))
    return;
  props.width = width > 0 ? width : props.width;
  props.height = height > 0 ? height : props.height;
  currPage->setProperties(&props);
  if(pending)
    scribbleDoc->repaintAll();
  else
    scribbleDoc->pageSizeChanged();
}

void ScribbleArea::nextPage(bool appendnew)
{
  // Note that we don't append a new page if the current page is empty
  if(currPageNum < numPages() - 1)
    gotoPage(currPageNum + 1);
  else if(appendnew && currPage->strokeCount() > 0)
    scribbleDoc->newPage();
}

void ScribbleArea::prevPage()
{
  if(currPageNum > 0)
    gotoPage(currPageNum - 1);
}

void ScribbleArea::setPageNum(int pagenum)
{
  // should be a Document::getPageCount() method!
  int totalpages = numPages();
  if(totalpages < 1) return;
  pagenum = std::max(0, std::min(pagenum, totalpages - 1));
  // on page insertion and deletion, page numbering changes, so also compare pointers
  if(currPage != page(pagenum) || currPageNum != pagenum) {
    // process recent strokes before changing page
    groupStrokes();
    currPage = page(pagenum);
    currPageNum = pagenum;
    currPage->ensureLoaded();  // needed for memory usage check if nothing else
    if(viewMode == VIEWMODE_SINGLE)
      pageSizeChanged();
    else {
      Point origin = getPageOrigin(currPageNum);
      currPageXOrigin = origin.x;
      currPageYOrigin = origin.y;
    }
    if(!currSelection)
      currSelPageNum = currPageNum;
    // page number must be updated in UI
    uiChanged(UIState::PageNumChange);
  }
}

void ScribbleArea::expandDown()
{
  Dim ystep = currPage->yruling() > 0 ? currPage->yruling() : GROW_STEP;
  scribbleDoc->startAction(currPageNum);
  setPageDims(-1, currPage->height() + (GROW_EXTRA+1)*ystep);
  scribbleDoc->endAction();
}

void ScribbleArea::expandRight()
{
  Dim xstep = currPage->xruling() > 0 ? currPage->xruling() : GROW_STEP;
  scribbleDoc->startAction(currPageNum);
  setPageDims(currPage->width() + (GROW_EXTRA+1)*xstep, -1);
  scribbleDoc->endAction();
}

// seems like some of the code here belongs in Page class
void ScribbleArea::growPage(Rect bbox, Dim maxdx, Dim maxdy, bool pending)
{
  Dim newwidth = -1;
  Dim newheight = -1;
  Dim xstep = currPage->xruling() > 0 ? currPage->xruling() : GROW_STEP;
  Dim ystep = currPage->yruling() > 0 ? currPage->yruling() : GROW_STEP;
  if(cfg->Bool("growDown") && currPage->height() - bbox.bottom < GROW_TRIGGER*ystep)
    newheight = ystep * (int)(bbox.bottom/ystep + 1) + GROW_EXTRA*ystep;
  if(cfg->Bool("growRight") && currPage->width() - bbox.right < GROW_TRIGGER*xstep)
    newwidth = xstep * (int)(bbox.right/xstep + 1) + GROW_EXTRA*xstep;
  if(maxdx >= 0) newwidth = std::min(currPage->width() + maxdx, newwidth);
  if(maxdy >= 0) newheight = std::min(currPage->height() + maxdy, newheight);
  if(newheight > currPage->height() || newwidth > currPage->width())
    setPageDims(newwidth, newheight, pending);
}

void ScribbleArea::setStrokeProperties(const StrokeProperties& props, bool undoable)
{
  if(!currSelection)
    return;
  viewSelection();
  if(undoable) {
    scribbleDoc->startAction(currPageNum);
    currSelection->setStrokeProperties(props);
    scribbleDoc->endAction();
  }
  else {
    for(Element* s : currSelection->strokes)
      s->setProperties(props);
  }
  // adjust selection region to account for possible stroke width change
  currSelection->shrink();
  doRefresh();
}

ScribblePen ScribbleArea::getPenForSelection() const
{
  if(!currSelection)
    return ScribblePen(Color::INVALID_COLOR, -1);
  StrokeProperties props = currSelection->getStrokeProperties();
  //auto isPressurePenFn = [](const Element* t) {
  //  return t->node->hasClass(Element::FLAT_PEN_CLASS) || t->node->hasClass(Element::ROUND_PEN_CLASS);
  //};
  //bool haspressure = currSelection->getFirstElement(isPressurePenFn) != NULL;
  return ScribblePen(props.color, props.width);  //, haspressure ? ScribblePen::WIDTH_PR : 0);
}

// The following two fns are used only for shared whiteboarding at the moment (specifically, when a stroke or
//  page is deleted by another user) ... also called when stroke is moved, since that can invalidate sel BG
void ScribbleArea::invalidateStroke(Element* s)
{
  RecentStrokesIter it = std::find(recentStrokes.begin(), recentStrokes.end(), s);
  if(it != recentStrokes.end())
    recentStrokes.erase(it);
  if(currSelection && currSelection->removeStroke(s)) {
    if(currSelection->count() == 0)
      clearSelection();
    else
      currSelection->shrink();
  }
}

// note that we do not handle setting new page - that is done in the same way as local undo (pageCountChanged)
void ScribbleArea::invalidatePage(Page* p)
{
  if(currSelection && currSelection->page == p)
    clearSelection();
  // recent strokes are always on currPage since groupStrokes() is called in setPageNum()
  if(currPage == p)
    recentStrokes.clear();
}

bool ScribbleArea::clearSelection()
{
  if(!currSelection) {
    scribbleDoc->clearSelection();
    return false;
  }
  //scribbleDoc->showSelToolbar(false); ... selection toolbar is closed like a regular popup menu
  int pagenum = currPageNum;
  if(currSelPageNum != currPageNum && viewMode != VIEWMODE_SINGLE)
    setPageNum(currSelPageNum);
  // necessary for case of selection on different page since dirtyScreen
  //  uses pageDimToDim
  dirtyScreen(selBGRect.rectUnion(currSelection->getBGBBox()));
  selBGRect = Rect();
  delete currSelection;
  currSelection = NULL;
  if(pagenum != currPageNum) {
    scribbleDoc->dirtyPage(currPageNum);
    setPageNum(pagenum);
  }
  recentStrokeSelPos = -1;
  currSelPageNum = currPageNum;
  return true;
}

bool ScribbleArea::clearTempSelection()
{
  // selectors are owned by selection
  pathSelector = NULL;
  ruledSelector = NULL;
  rectSelector = NULL;
  lassoSelector = NULL;
  insSpaceEraseSelector = NULL;
  if(insSpaceEraseSelection) {
    insSpaceEraseSelection->clear();
    delete insSpaceEraseSelection;
    insSpaceEraseSelection = NULL;
  }
  if(tempSelection) {
    // must clear selection to update dirtyRect
    tempSelection->clear();
    delete tempSelection;
    tempSelection = NULL;
    return true;
  }
  return false;
}

void ScribbleArea::selectAll(Selection::StrokeDrawType drawtype)
{
  clearSelection();
  // select strokes from current page...
  currSelection = new Selection(currPage, drawtype);
  new RectSelector(currSelection, mZoom, true);  //isMoveSelFree());
  currSelection->selectAll();
  if(currSelection->count() == 0) {
    delete currSelection;
    currSelection = NULL;
  }
  else
    currSelection->shrink();
}

// TODO: option to display dialog box to allow user to select which properties to use
void ScribbleArea::selectSimilar()
{
  if(currSelection) {
    viewSelection();
    currSelection->selectbyProps(NULL, true, true);
    currSelection->shrink();
    repaintAll();
  }
}

void ScribbleArea::invertSelection()
{
  if(currSelection) {
    //doCancelAction();
    viewSelection();
    currSelection->invertSelection();
    if(currSelection->count() > 0)
      currSelection->shrink();
    else
      clearSelection();
    repaintAll();
  }
  else
    selectAll();
}

void ScribbleArea::deleteSelection()
{
  if(currSelection) {
    viewSelection();
    scribbleDoc->startAction(currPageNum);
    currSelection->deleteStrokes();
    scribbleDoc->endAction();
    clearSelection();
  }
}

// Recent stroke select: selecting N most recent strokes with undo dial alternative mode
// it is safe to assume undo history is const, as any undo or redo will clear selection
bool ScribbleArea::recentStrokeSelect()
{
  bool selcleared = recentStrokeSelPos < 0;  // && currSelection;  -- split view may have selection
  if(selcleared)
    clearSelection();
  scribbleDoc->exitPageSelMode();
  if(!currSelection)
    recentStrokeSelPos = scribbleDoc->history->pos;
  // continue until we hit beginning of history or find a StrokeAddedItem
  bool strokeadded = false;
  UndoHistoryItem* item;
  while(recentStrokeSelPos > 0
      && (!(item = scribbleDoc->history->hist[--recentStrokeSelPos])->isA(UndoHistoryItem::HEADER) || !strokeadded)) {
    if(item->isA(UndoHistoryItem::STROKE_ADDED_ITEM)) {
      Element* s = static_cast<StrokeAddedItem*>(item)->getStroke();
      // we may have skipped over a StrokeDeletedItem for this stroke
      if(!s->parent())
        continue;
      if(!currSelection) {
        // need to get page number from stroke - we could have skipped over add/delete page items, so page
        //  number in header may not be valid
        setPageNum(scribbleDoc->document->pageForElement(s)->getPageNum());
        currSelection = new Selection(currPage);
        rectSelector = new RectSelector(currSelection, mZoom, true);  //isMoveSelFree());
      }
      if(s->node->parent() == currSelection->page->contentNode) {
        currSelection->addStroke(s);
        strokeadded = true;
      }
    }
  }
  if(strokeadded) {
    currSelection->shrink();
    viewSelection();  // ensure selection is visible
  }
  else if(!currSelection)
    recentStrokeSelPos = -1;  // keep state consistent (!currSelection implies recentStrokeSelPos == -1)
  if(strokeadded || selcleared) {
    uiChanged(UIState::SelChange);
    doRefresh();
  }
  return strokeadded;
}

bool ScribbleArea::recentStrokeDeselect()
{
  int histsize = scribbleDoc->history->hist.size();
  if(!currSelection || recentStrokeSelPos < 0 || recentStrokeSelPos >= histsize)
    return false;
  UndoHistoryItem* item;
  bool strokeremoved = false;
  while(++recentStrokeSelPos < histsize
      && (!(item = scribbleDoc->history->hist[recentStrokeSelPos])->isA(UndoHistoryItem::HEADER) || !strokeremoved)) {
    if(item->isA(UndoHistoryItem::STROKE_ADDED_ITEM)) {
      Element* s = static_cast<StrokeAddedItem*>(item)->getStroke();
      if(s->isSelected(currSelection)) {
        currSelection->removeStroke(s);
        strokeremoved = true;
      }
    }
  }
  if(currSelection->count() == 0)
    clearSelection();
  else
    currSelection->shrink();
  uiChanged(UIState::SelChange);
  doRefresh();
  return strokeremoved;
}

void ScribbleArea::recentStrokeSelDone()
{
  if(currSelection && recentStrokeSelPos >= 0 && cfg->Bool("popupToolbar")) {
    //recentStrokeSelPos = -1; ... I think we actually don't want to do this
    Rect r = currSelection->getBGBBox();
    app->showSelToolbar(screenToGlobal(dimToScreen(pageDimToDim(Point(r.right, r.bottom)))));
  }
}

// we take Selection instead of Clipboard so we can access source page
bool ScribbleArea::selectionDropped(Selection* selection, Point globalPos, Point offset, bool replaceids)
{
  // for now, selection is not shifted at all from drop position; no consideration of ruling
  Point pos = screenToDim(globalToScreen(globalPos));
  int pagenum = dimToPageNum(pos);
  Point pagepos = pos - getPageOrigin(pagenum);
  Page* newpage = page(pagenum);
  if(!newpage || !newpage->rect().contains(pagepos))
    return false;

  Clipboard clip;
  selection->toSorted(&clip);  // this clones strokes, so use PasteMoveClipboard so we don't clone again
  if(replaceids) {
    clip.replaceIds();
    // This has the effect of clearing timestamp for drops from clippings; are there other instances where
    //  we should clear timestamp of pasted content?  Whenever ids are replaced?
    // since clipbboard came from selection, we know every node has an Element
    for(SvgNode* node : clip.content->children())
      static_cast<Element*>(node->ext())->setTimestamp(0);
  }
  return clipboardDropped(&clip, globalPos, offset);
}

bool ScribbleArea::clipboardDropped(Clipboard* clip, Point globalPos, Point offset)
{
  Point pos = screenToDim(globalToScreen(globalPos));
  // if already in undo action (as for drop on same doc split), don't call start/end in doPasteAt
  int flags = scribbleDoc->history->undoable() ? 0 : PasteUndoable;
  flags |= offset.isNaN() ? PasteCenter : PasteOrigin;
  // drag and drop selection currently doesn't support ruling, so don't use PasteNoHandles in any case
  doPasteAt(clip, offset.isNaN() ? pos : pos + offset, PasteFlags(flags | PasteMoveClipboard));
  uiChanged(UIState::Paste);
  doRefresh();
  return true;
}

void ScribbleArea::doPaste(Clipboard* clipboard, const Page* srcpage, int flags)
{
  Point pos = screenToDim(screenRect.center());
  // "write-page" class used to identify whole pages; works even pasting between separate processes
  if(clipboard->content->firstChild()->hasClass("write-page")) {
    // paste into page break nearest center of screen
    int pagenum = dimToPageNum(pos);
    Point wherep = (pos - getPageOrigin(pagenum)) - page(pagenum)->rect().center();
    int where = (viewMode == VIEWMODE_HORZ ? wherep.x : wherep.y) < 0 ? pagenum : pagenum + 1;
    scribbleDoc->pastePages(clipboard, where);
  }
  else {
    // paste onto the currently centered page
    bool origpos = page(std::min(dimToPageNum(pos), numPages() - 1)) != srcpage;
    doPasteAt(clipboard, pos, PasteFlags(flags |
        (origpos ? PasteOrigPos : PasteCenter) | PasteOffsetExisting | PasteRulingShift | PasteUndoable));
  }
}

// paste position:
// - PasteCenter: place center of clipboard bbox at specified position
// - PasteCorner: place upper left corner of clipboard bbox at specified position
// - PasteOrigin: place the (0,0) of the clipboard contents at specified position
// - PasteOrigPos: specified position is ignored (falls back to PasteCenter if not visible or off-page)
// PasteUndoable flag is a horrible hack - but it's the same hack used for insertPage, etc, so at least we're consistent
// instead of PasteMoveClipboard, we could consider passing Clipboard instead of Clipboard* and using
//  Clipboard::clone() (w/ copy ctor hidden) if we don't want to move strokes
void ScribbleArea::doPasteAt(Clipboard* clipboard, Point pos, PasteFlags flags)
{
  static constexpr Dim MIN_PASTE_OFFSET = 20;

  setPageNum(dimToPageNum(pos));
  Point p = dimToPageDim(pos);
  Point dr;
  bool offsetexisting = (flags & PasteOffsetExisting) && currSelection && currSelPageNum == currPageNum;
  Rect selbbox = offsetexisting ? currSelection->getBBox() : Rect();
  clearSelection();
  scribbleDoc->exitPageSelMode();
  currSelection = new Selection(currPage);
  rectSelector = new RectSelector(currSelection, mZoom, !(flags & PasteNoHandles));  //isMoveSelFree());
  if(flags & PasteUndoable)
    scribbleDoc->startAction(currPageNum);
  clipboard->paste(currSelection, flags & PasteMoveClipboard);
  Rect bbox = currSelection->getBBox();
  // scale external content to fit on page
  if(clipboard->content->hasClass("external")) {
    Dim scale = 0.75*std::min(currPage->width()/bbox.width(), currPage->height()/bbox.height());
    if(scale < 0.75) {
      Point tfxy = (1 - scale)*bbox.center();
      currSelection->stealthTransform(Transform2D(scale, 0, 0, scale, tfxy.x, tfxy.y));
      bbox = currSelection->getBBox();
    }
  }
  if(flags & PasteCorner)
    dr = p - bbox.origin();  // top left
  else if(flags & PasteOrigin)
    dr = p;
  else if((flags & PasteCenter) || !isVisible(pageDimToDim(bbox))
      || !currPage->rect().contains(bbox.center()) || !currPage->rect().contains(bbox.origin())) {
    // make sure bottom right corner is on page
    Dim minx = currPage->xruling()/2 + bbox.width()/2;
    Dim miny = currPage->yruling()/2 + bbox.height()/2;
    Dim maxx = currPage->width() - currPage->xruling()/2 - bbox.width()/2;
    Dim maxy = currPage->height() - currPage->yruling()/2 - bbox.height()/2;
    dr = Point(std::min(std::max(minx, p.x), maxx), std::min(std::max(miny, p.y), maxy)) - bbox.center();
  }
  // round dx and dy so that we translate by multiple of ruling
  // obviously not going to accomplish anything if source and target have different rulings
  if(flags & PasteRulingShift) {
    if(currPage->xruling() > 0)
      dr.x = quantize(dr.x, currPage->xruling());
    if(currPage->yruling() > 0)
      dr.y = quantize(dr.y, currPage->yruling());
  }
  // offset from existing selection, mainly to prevent exact overlap if paste is accidentily hit twice
  if(offsetexisting) {
    Dim offset = MAX(MIN_PASTE_OFFSET, MAX(currPage->xruling(), currPage->yruling()));
    if(ABS(bbox.left + dr.x - selbbox.left) < MIN_PASTE_OFFSET && ABS(bbox.top + dr.y - selbbox.top) < MIN_PASTE_OFFSET) {
      dr.x += currPage->xruling() > 0 ? currPage->xruling() : offset;
      dr.y += currPage->yruling() > 0 ? currPage->yruling() : offset;
    }
  }
  // apply new position to each stroke
  currSelection->stealthTransform(Transform2D::translating(dr.x, dr.y));
  currSelection->shrink();
  // endAction sends sync undo items, so must be done after stealthTransform()
  if(flags & PasteUndoable)
    scribbleDoc->endAction();
  // shrink should not be necessary
  //currSelection->shrink();
  clipboard->replaceIds();
#ifdef ONE_TIME_TIPS
  if(showHelpTips && rectSelector->enableCrop) {
    Rect b = dimToScreen(pageDimToDim(currSelection->getBGBBox()));
    app->oneTimeTip("cropsel", Point(b.right, b.bottom), _("Drag red handles to crop image."));
  }
#endif
}

// groupStrokes():
//  called with Stroke* to add to recent stroke list; if passed NULL pointer (the default param) or if
//  stroke is not estimated to be on the same rule line as the previous strokes, all strokes in the recent
//  strokes list will be processed and the list cleared.
// Originally, I had tried to use the undo list instead of saving the Stroke ptrs to separate list; this
//  became very messy; really, it was a dumb idea from the start - what was I thinking?!?

void ScribbleArea::groupStrokes(Element* b)
{
  if(!cfg->Bool("groupStrokes"))
    return;

  if(!recentStrokes.empty()) {
    const Timestamp GROUP_DT_MAX = 2500;  // 2.5 seconds
    const Dim yruling = currPage->yruling(true);
    const Dim GROUP_DX_MIN = -0.25*yruling;
    const Dim GROUP_DX_MAX = 1.5*yruling;
    const Dim GROUP_DY_MIN = -1.25*yruling;
    const Dim GROUP_DY_MAX = 1.25*yruling;
    const Dim TIGHT_DY_MIN = -1.0*yruling;
    const Dim TIGHT_DY_MAX = 0.8*yruling;

    Element* a = recentStrokes.back();
    strokeGroupYCenter += a->bbox().center().y;
    Dim ycenter = strokeGroupYCenter/recentStrokes.size();
    Dim miny = ycenter + (recentStrokes.size() > 3 ? TIGHT_DY_MIN : GROUP_DY_MIN);
    Dim maxy = ycenter + (recentStrokes.size() > 3 ? TIGHT_DY_MAX : GROUP_DY_MAX);
    if(!b || a->timestamp() + GROUP_DT_MAX < b->timestamp()
          || a->bbox().right + GROUP_DX_MAX < b->bbox().left
          || a->bbox().left + GROUP_DX_MIN > b->bbox().right
          || b->bbox().top < miny
          || b->bbox().bottom > maxy) {
      // end of group ... set com.y for each stroke to group's average
      // group must contain at least 4 strokes
      if(recentStrokes.size() > 3) {
        for(Element* s : recentStrokes)
          s->setCom(Point(s->com().x, ycenter));
        // special handling needed for whiteboarding since we are making a change outside the undo system
        scribbleDoc->strokesUpdated(recentStrokes);
      }
      // check for strokes in margin
      if(cfg->Int("bookmarkMode") == BookmarkView::MARGIN_CONTENT) {
        for(Element* s : recentStrokes) {
          if(s->bbox().right < currPage->marginLeft()) {
            scribbleDoc->document->bookmarksDirty = true;
            break;
          }
        }
      }
      // all done!
      recentStrokes.clear();
      strokeGroupYCenter = 0;
    }
  }
  if(b)
    recentStrokes.push_back(b);
}

int ScribbleArea::dimToPageNum(const Point& pos) const
{
  if(viewMode == VIEWMODE_SINGLE)
    return currPageNum;

  Dim d = 0;
  unsigned int npages = numPages();
  if(viewMode == VIEWMODE_VERT) {
    for(unsigned int ii = 0; ii < npages; ii++) {
      d += page(ii)->height() + pageSpacing;
      if(d > pos.y)
        return ii;
    }
  }
  else if(viewMode == VIEWMODE_HORZ) {
    for(unsigned int ii = 0; ii < npages; ii++) {
      d += page(ii)->width() + pageSpacing;
      if(d > pos.x)
        return ii;
    }
  }
  return npages;  // past end (ghost page)
}

Point ScribbleArea::dimToPageDim(const Point& p) const
{
  return Point(p.x - currPageXOrigin, p.y - currPageYOrigin);
}

Rect ScribbleArea::dimToPageDim(Rect r) const
{
  return r.translate(-currPageXOrigin, -currPageYOrigin);
}

Point ScribbleArea::pageDimToDim(const Point& p) const
{
  return Point(p.x + currPageXOrigin, p.y + currPageYOrigin);
}

Rect ScribbleArea::pageDimToDim(Rect r) const
{
  return r.translate(currPageXOrigin, currPageYOrigin);
}

bool ScribbleArea::isPageVisible(int pagenum)
{
  return viewMode == VIEWMODE_SINGLE ? currPageNum == pagenum
      : pagenum < numPages() && isVisible(page(pagenum)->rect().translate(getPageOrigin(pagenum)));
}

// used by bookmarkview and mainwindow (for jumping to page)
// pos is in page Dim of specified page
void ScribbleArea::doGotoPos(int pagenum, Point pos, bool exact)
{
  doCancelAction();
  exact ? gotoPos(pagenum, pos) : viewPos(pagenum, pos);
  doRefresh();
}

void ScribbleArea::viewPos(int pagenum, Point pos)
{
  saveCurrPos(pagenum, pos);
  setPageNum(pagenum);
  setVisiblePos(pageDimToDim(pos));
}

void ScribbleArea::gotoPos(int pagenum, Point pos, bool savepos)
{
  if(savepos)
    saveCurrPos(pagenum, pos);
  setPageNum(pagenum);
  setCornerPos(pageDimToDim(pos));
}

// used for next page, prev page, so we never save previous position
void ScribbleArea::gotoPage(int pagenum)
{
  Point currpos = screenToDim(Point(0,0));
  if(pagenum >= numPages())  // goto end of last page
    gotoPos(numPages()-1, Point(viewMode == VIEWMODE_VERT ? currpos.x : -10, INT_MAX), false);
  else
    gotoPos(pagenum, Point(viewMode == VIEWMODE_VERT ? currpos.x : -10, -10), false);
}

// r is in page Dim of specified page
void ScribbleArea::viewRect(int pagenum, const Rect& r)
{
  if(pagenum != currPageNum)
    setPageNum(pagenum);
  if(!isVisible(pageDimToDim(r)))
    setCenterPos(pageDimToDim(r.center()));
}

// adjust position and zoom to view entire rect r - used for optional view syncing for shared whiteboarding
void ScribbleArea::setViewBox(const DocViewBox& vb)
{
  setPageNum(vb.pagenum);
  Rect r = pageDimToDim(vb.box);
  Rect s = screenToDim(screenRect);
  // ensure viewbox fits on screen
  Dim scale = std::min(s.width()/r.width(), s.height()/r.height());
  // don't set zoom level higher than master (e.g., in case our screen is bigger than master)
  Dim newzoom = zoomSteps[nearestZoomStep(std::min(scale*mZoom, vb.zoom))];
  if(newzoom != mZoom)
    setZoom(newzoom);
  setCenterPos(r.center());
}

// returns box enclosing page content only (not off-page regions); uses pagenum and page dim instead of
//  global dim to handle page growth on slaves due to annotations in SWB lecture mode
DocViewBox ScribbleArea::getViewBox() const
{
  Rect s = dimToPageDim(screenToDim(screenRect));
  // TODO: we don't account for multiple pages visible with different dimensions!
  if(viewMode != VIEWMODE_HORZ && s.width() > currPage->width()) {
    s.left = 0;
    s.right = currPage->width();
  }
  if(viewMode != VIEWMODE_VERT && s.height() > currPage->height()) {
    s.top = 0;
    s.bottom = currPage->height();
  }
  return DocViewBox(currPageNum, s, mZoom);
}

// ensure selection is visible
void ScribbleArea::viewSelection()
{
  if(currSelection)
    viewRect(currSelPageNum, currSelection->getBGBBox());
}

// save current position to history if major change
// posHistoryPos points to "current" pos / next avail slot to insert new pos
bool ScribbleArea::saveCurrPos(int newpagenum, Point newpos)
{
  Point origin = getPageOrigin(newpagenum);
  newpos.x += origin.x;
  newpos.y += origin.y;
  Point currpos = screenToDim(Point(0,0));
  Point lastpos = newpos;
  // this is to avoid creating sequential history entries near the same position
  if(posHistoryPos != posHistory.begin()) {
    DocPosition lastdocpos = *(posHistoryPos - 1);
    origin = getPageOrigin(lastdocpos.pagenum);
    lastpos.x = lastdocpos.pos.x + origin.x;
    lastpos.y = lastdocpos.pos.y + origin.y;
  }
  Dim mindx = getViewWidth()/2;
  Dim mindy = getViewHeight()/2;
  if((ABS(newpos.x - currpos.x) > mindx || ABS(newpos.y - currpos.y) > mindy)
      && (ABS(currpos.x - lastpos.x) > mindx || ABS(currpos.y - lastpos.y) > mindy)) {
    posHistory.erase(posHistoryPos, posHistory.end());
    posHistory.push_back(getPos());
    posHistoryPos = posHistory.end();
    return true;
  }
  return false;
}

void ScribbleArea::prevView()
{
  if(posHistoryPos != posHistory.begin()) {
    DocPosition docpos = *(posHistoryPos - 1);
    if(posHistoryPos == posHistory.end()) {
      if(saveCurrPos(docpos.pagenum, docpos.pos))
        posHistoryPos--;
    }
    posHistoryPos--;
    gotoPos(docpos.pagenum, docpos.pos, false);
  }
}

void ScribbleArea::nextView()
{
  if(posHistoryPos != posHistory.end())
    posHistoryPos++;
  if(posHistoryPos != posHistory.end()) {
    DocPosition docpos = *posHistoryPos;
    gotoPos(docpos.pagenum, docpos.pos, false);
  }
}

DocPosition ScribbleArea::getPos() const
{
  return DocPosition(currPageNum, dimToPageDim(screenToDim(Point(0,0))));
}

// double tap to zoom to 100 percent
void ScribbleArea::doDblClickAction(Point pos)
{
  //scribbleDoc->setActiveArea(this);
  zoomTo(1, pos.x, pos.y);
  roundZoom(pos.x, pos.y); // update zoom step index
}

bool ScribbleArea::viewHref(const char* href)
{
  int targetpagenum = currPageNum;  // search is done backwards from this page
  SvgNode* n = scribbleDoc->document->findNamedNode(href, &targetpagenum);
  if(!n)
    return false;
  Rect r = n->bounds();
  Page* pg = page(targetpagenum);
  Point cornerpos(r.left - 5, std::min(r.top, pg->getYforLine(pg->getLine(r.center().y)) - 5));
  viewPos(targetpagenum, cornerpos);
  return true;
}

// handle clicking on links
bool ScribbleArea::doClickAction(Point pos)
{
  //scribbleDoc->setActiveArea(this);
  // check for click on link
  Point gpos = screenToDim(pos);
  int pagenum = dimToPageNum(gpos);
  if(pagenum >= numPages())
    return false;
  // change the page ... should we be doing this? (Note that the pan tool already did this)
  if(pagenum != currPageNum)
    setPageNum(pagenum);
  const char* href = page(pagenum)->getHyperRef(dimToPageDim(gpos));
  // check for bookmark link
  if(!href || !href[0]) {}
  else if(href[0] == '#')
    viewHref(href);
  else
    scribbleDoc->openURL(href);
  return href != NULL;
}

// consolidated fn for setting properties of current selection, including hyperref and bookmark creation
// initial motivation was to ensure only one undo item was created; also reduces code duplication
void ScribbleArea::setSelProperties(const StrokeProperties* props, const char* target, Element* bkmktarget,
    const char* idstr, bool forcenormal)
{
  doCancelAction();
  if(!currSelection)
    return;
  viewSelection();
  scribbleDoc->startAction(currPageNum);

  if(props && (props->color != Color::INVALID_COLOR || props->width > 0))
    currSelection->setStrokeProperties(*props);

  // create sorted list of strokes, flattening any hyperrefs and reverting any bookmarks
  std::vector<Element*> sorted;
  if(target || bkmktarget || idstr || forcenormal) {
    std::vector<Element*> sorted0;
    sorted0.reserve(currSelection->count());
    for(Element* s : currSelection->sourceNode->children()) {
      if(s->isSelected(currSelection))
        sorted0.push_back(s);
    }

    // can't (easily) add/remove strokes from page or selection while iterating, so do in 2 stages
    sorted.reserve(sorted0.size());
    for(Element* s : sorted0) {
      // flatten our structure nodes (but not external ones)
      if(s->isMultiStroke()) {
        for(Element* ss : s->children()) {
          Element* t = ss->cloneNode();
          currPage->addStroke(t, s);
          if(s->node->hasTransform()) {
            t->applyTransform(s->node->getTransform());
            t->commitTransform();
          }
          currSelection->addStroke(t);
          sorted.push_back(t);
        }
        currSelection->removeStroke(s);
        currPage->removeStroke(s);  // delete original
      }
      else if(s->isBookmark()) {
        Element* t = s->cloneNode();
        t->node->removeClass("bookmark");
        currPage->addStroke(t, s);
        currSelection->addStroke(t);
        currSelection->removeStroke(s);
        currPage->removeStroke(s);  // delete original
        sorted.push_back(t);
      }
      else
        sorted.push_back(s);
    }
  }

  if(target || bkmktarget) {
    // create hyperref, then *replace* strokes with it
    Element* h = new Element(new SvgG());  //SvgNode::A));
    h->node->addClass("hyperref");
    for(Element* s : sorted)
      h->addChild(s->cloneNode());
    // invisible rect (for browsers) is now inserted in Element::serializeAttr
    if(bkmktarget) {
      // bookmark pasted from clipboard (or in legacy document) may not have id set
      if(!bkmktarget->nodeId() || !bkmktarget->nodeId()[0] ) {
        // replace bookmark w/ labeled copy since we don't have a way to undo setting id string of original
        Element* newbkmk = bkmktarget->cloneNode();
        Page* page = scribbleDoc->document->pageForElement(bkmktarget);
        newbkmk->setNodeId(("b-" + randomStr(10)).c_str());
        page->addStroke(newbkmk, bkmktarget);
        page->removeStroke(bkmktarget);
        bkmktarget = newbkmk;
      }
      h->sethref((std::string("#") + bkmktarget->nodeId()).c_str());
    }
    else
      h->sethref(target);
    currPage->addStroke(h, sorted.back());
    currSelection->deleteStrokes();
    currSelection->addStroke(h);
  }
  else if(idstr) {
    // convert to bookmark with id
    Element* bkmk;
    if(sorted.size() > 1) {
      //SvgG* bkmkNode = new SvgG(NULL);
      bkmk = new Element(new SvgG());  //static_cast<Element*>(bkmkNode->ext());
      for(Element* s : sorted)
        bkmk->containerNode()->addChild(s->cloneNode()->node);
    }
    else
      bkmk = sorted.front()->cloneNode();
    bkmk->setNodeId(idstr);
    bkmk->node->addClass("bookmark");
    currPage->addStroke(bkmk, sorted.back());
    currSelection->deleteStrokes();
    currSelection->addStroke(bkmk);
  }
  scribbleDoc->endAction();
  uiChanged(UIState::SetSelProps);
  doRefresh();
}

// return the target of the first HyperRef found in currSelection or NULL
const char* ScribbleArea::getHyperRef() const
{
  Element* s;
  if(currSelection && (s = currSelection->getFirstElement( [](const Element* t){ return t->isHyperRef(); } )))
    return s->href();
  return NULL;
}

// return the idStr of the first bookmark in currSelection
const char* ScribbleArea::getIdStr() const
{
  Element* s;
  if(currSelection && (s = currSelection->getFirstElement( [](const Element* t){ return t->isBookmark(); } )))
    return s->nodeId();
  return NULL;
}

void ScribbleArea::ungroupSelection()
{
  if(!currSelection)
    return;
  viewSelection();
  scribbleDoc->startAction(currPageNum);
  currSelection->ungroup();
  scribbleDoc->endAction();
}

void ScribbleArea::insertImage(Image image)
{
  doCancelAction();
  Clipboard clip;
  Point center = screenToDim(screenRect.center());
  Dim imgw = image.getWidth()*unitsPerPx, imgh = image.getHeight()*unitsPerPx;
  Dim s = std::min(Dim(1), std::min(currPage->width()/2/imgw, currPage->height()/2/imgh));
  Rect bbox = Rect::centerwh(center, imgw*s, imgh*s);
  clip.addStroke(new Element(new SvgImage(std::move(image), bbox)));
  doPasteAt(&clip, center, PasteFlags(PasteOrigPos | PasteMoveClipboard | PasteUndoable));
  uiChanged(UIState::Paste);
  doRefresh();
}

void ScribbleArea::freeErase(Point prevpos, Point pos)
{
  // for now, let's fix the eraser size in screen space so that user can zoom to adjust how much is erased
  //  rather than selecting from different sized erasers
  Dim radius = ERASEFREE_RADIUS/mZoom;
  bool touched = false;
  Rect erasebox = Rect::corners(prevpos, pos).pad(radius);
  auto strokes = currPage->children();
  for(auto ii = strokes.begin(); ii != strokes.end();) {
    Element* s = *ii++;
    if(!s->isSelected(tempSelection) && erasebox.intersects(s->bbox())) {
      if(s->isSelected(freeErasePieces)) {
        //Rect oldbbox = s->bbox();
        touched = s->freeErase(prevpos, pos, radius) || touched;
          //currPage->growDirtyRect(oldbbox.rectUnion(s->bbox()));
      }
      else {
        Element* s2 = s->cloneNode();
        if(s2->freeErase(prevpos, pos, radius)) {
          Element* nexts = ii != strokes.end() ? *ii : NULL;
          currPage->contentNode->addChild(s2->node, nexts ? nexts->node : NULL);
          ii = std::find(strokes.begin(), strokes.end(), nexts);
          freeErasePieces->addStroke(s2);
          // hide original stroke
          tempSelection->addStroke(s);
          //currPage->growDirtyRect(s->bbox());
          touched = true;
        }
        else
          s2->deleteNode();  //delete s2;
      }
    }
  }
  // Element::freeErase() does not set anything dirty
  if(touched)
    scribbleDoc->updateCurrStroke(erasebox.pad(1));
}

// dispatch fn for commands

void ScribbleArea::doCommand(int itemid)
{
  doCancelAction();
  switch(itemid) {
  case ID_SELRECENT: recentStrokeSelect();  break;
  case ID_DESELRECENT:  recentStrokeDeselect();  break;
  case ID_SELALL:  selectAll();  break;
  case ID_SELSIMILAR:  selectSimilar();  break;
  case ID_INVSEL:  invertSelection();  break;
  case ID_EXPANDDOWN:  expandDown();  break;
  case ID_EXPANDRIGHT:  expandRight();  break;
  case ID_ZOOMIN:  zoomIn();  break;
  case ID_ZOOMOUT:  zoomOut();  break;
  // Zoom all and zoom width (restored from r610) are not exposed in UI
  case ID_ZOOMALL:
    setZoom(std::min(getViewWidth()/currPage->width()/preScale, getViewHeight()/currPage->height()/preScale));
    setCenterPos(pageDimToDim(Point(currPage->width()/2/preScale, currPage->height()/2/preScale)));
    break;
  case ID_ZOOMWIDTH:
    setZoom(getViewWidth()/currPage->width()/preScale);
    setCornerPos(pageDimToDim(Point(0, 0)));
    break;
  case ID_RESETZOOM:  resetZoom();  break;
  case ID_PREVVIEW:  prevView();  break;
  case ID_NEXTVIEW:  nextView();  break;
  case ID_PREVPAGE:  prevPage();  break;
  case ID_NEXTPAGE:  nextPage(false);  break;
  case ID_NEXTPAGENEW:  nextPage(true);  break;
  case ID_PREVSCREEN:  doPan(0, screenRect.height());  break;  // doPan dx,dy are in screen, not dim units!
  case ID_NEXTSCREEN:  doPan(0, -screenRect.height());  break;
  case ID_SCROLLUP:  doPan(0, 20);  break;
  case ID_SCROLLDOWN:  doPan(0, -20);  break;
  case ID_STARTOFDOC:  gotoPage(0);  break;
  case ID_ENDOFDOC:  gotoPage(numPages());  break;  // >=numPages() takes us to end of last page
  case ID_UNGROUP:  ungroupSelection();  break;
  default:  return;
  }
  uiChanged(UIState::Command);
  doRefresh();
}

void ScribbleArea::uiChanged(int reason)
{
  scribbleDoc->uiChanged(reason);
}

void ScribbleArea::doRefresh()
{
  scribbleDoc->doRefresh();
}

void ScribbleArea::updateUIState(UIState* state)
{
  if(!currPage) return;  // prevent crash ... saw on Android, not sure how
  // move,invert selection, select similar, delete, copy require an active selection
  state->activeSel = (currSelection != NULL);
  state->pageSel = scribbleDoc->numSelPages > 0;
  state->selHasGroup = currSelection && currSelection->containsGroup();
  state->pagemodified = (currPage->dirtyCount != 0);
  state->pageNum = currPageNum+1;
  state->totalPages = numPages();
  state->zoom = mZoom;
  state->currPageStrokes = currPage->strokeCount();
  state->currSelStrokes = currSelection ? currSelection->count() : 0;
  state->prevView = posHistoryPos != posHistory.begin();
  state->nextView = posHistoryPos != posHistory.end() && (posHistoryPos + 1) != posHistory.end();
  state->pageWidth = currPage->width();
  state->pageHeight = currPage->height();

  if(currSelection)
    currSelection->recalcTimeRange();  // will only calculate if not set
  if(currSelection && currSelection->maxTimestamp > 0) {
    state->minTimestamp = currSelection->minTimestamp;
    state->maxTimestamp = currSelection->maxTimestamp;
  }
  else {
    currPage->recalcTimeRange();
    state->minTimestamp = currPage->minTimestamp;
    state->maxTimestamp = currPage->maxTimestamp;
  }
}

const ScribblePen* ScribbleArea::currPen() const
{
  return app->getPen();
}

int ScribbleArea::selectionHit(Point pos, bool touch)
{
  if(!currSelection || !currSelection->selector)
    return 0;
  // check for selection scale handle hit
  scaleOrigin = currSelection->selector->scaleHandleHit(pos, touch);
  if(!scaleOrigin.isNaN()) {
    // bottom right corner scales with fixed aspect ratio; others scale freely
    scaleLockRatio = scaleOrigin.x < pos.x && scaleOrigin.y < pos.y;
    prevXScale = 1;
    prevYScale = 1;
    return MODEMOD_SCALESEL;
  }

  // next check for rotate handle hit
  scaleOrigin = currSelection->selector->rotHandleHit(pos, touch);
  if(!scaleOrigin.isNaN())
    return MODEMOD_ROTATESEL;

  scaleOrigin = currSelection->selector->cropHandleHit(pos, touch);
  if(!std::isnan(scaleOrigin.x) || !std::isnan(scaleOrigin.y)) {
    prevXScale = 1;
    prevYScale = 1;
    return MODEMOD_CROPSEL;
  }

  // hackish way to see if cursor is down within the (possibly complex) selection region
  SvgPath testStroke(Path2D().addLine(pos, pos), SvgNode::LINE);
  if(currSelection->selector->selectHit(new Element(&testStroke)))
    return MODEMOD_MOVESEL;

  return 0;
}

// For handling touch events, see the touch/fingerpaint example
void ScribbleArea::doPressEvent(const InputEvent& event)
{
  int modemod = event.modemod;
  Point rawpos = Point(event.points[0].x, event.points[0].y);
  Point gpos = screenToDim(rawpos);
  Point pos = dimToPageDim(gpos);
  // take focus
  //scribbleDoc->setActiveArea(this);
  // TODO: maybe refactor this, move logic into setCurrPos
  if((viewMode == VIEWMODE_VERT && (pos.y < -pageSpacing || pos.y > currPage->height() + pageSpacing))
      || (viewMode == VIEWMODE_HORZ && (pos.x < -pageSpacing || pos.x > currPage->width() + pageSpacing))) {
    int newpagenum = dimToPageNum(gpos);
    if(newpagenum != currPageNum) {
      setPageNum(newpagenum);
      pos = dimToPageDim(gpos);
    }
  }

  // offset should be a property of RuledSelector, not Page, but this is easier for now
  if(currPage->yruling() == 0)
    currPage->yRuleOffset = fmod(pos.y - Page::BLANK_Y_RULING/2, Page::BLANK_Y_RULING);

  switch(cfg->Int("panFromEdge")) {
  case 1: {
    Dim border = cfg->Float("panBorder") * preScale;
    // all edges are treated the same for now, so just set EDGEMASK
    if(rawpos.x < border || rawpos.x > getViewWidth() - border || rawpos.y > getViewHeight() - border)
      modemod |= MODEMOD_EDGEMASK;
    break;
  }
  case 2:
    // pointer down off page pans instead of draws
    if(pos.x < 0 || pos.x > currPage->width() || pos.y < 0 || pos.y > currPage->height())
      modemod |= MODEMOD_EDGEMASK;
  }
  prevLine = currPage->getLine(pos.y);
  initialLine = prevLine;
  initialPageSize = currPage->rect();

  bool selvisible = currSelection &&
      currSelPageNum == currPageNum && isVisible(pageDimToDim(currSelection->getBGBBox()));
  if(selvisible)
    modemod |= selectionHit(pos, event.source == INPUTSOURCE_TOUCH);
  // get the mode!
  currMode = scribbleDoc->getScribbleMode(modemod);
  // do pan-from-edge through ScribbleInput to avoid inappropriately reverting to sticky tool after panning
  if(currMode == MODE_PAN && modemod & MODEMOD_EDGEMASK) {
    currMode = MODE_NONE;
    scribbleInput->cancelAction();
    scribbleInput->forcePanMode(event);
  }
  if(currSelection) {
    // clear selection depending on mode
    switch(currMode) {
    case MODE_STROKE:
      // ignore this stroke if it clears selection (optionally)
      if(cfg->Bool("clearSelOnly"))
        currMode = MODE_NONE;
    case MODE_SELECTRECT:
    case MODE_SELECTRULED:
    case MODE_SELECTLASSO:
    case MODE_SELECTPATH:
    case MODE_BOOKMARK:
    case MODE_ERASESTROKE:
    case MODE_ERASERULED:
    case MODE_ERASEFREE:
    case MODE_INSSPACEVERT:
    case MODE_INSSPACEHORZ:
    case MODE_INSSPACERULED:
      clearSelection();
      break;
    case MODE_MOVESEL:
      currMode = currSelection->selector->drawHandles ? MODE_MOVESELFREE : MODE_MOVESELRULED;
      break;
    default:
      break;
    }
  }
  else if(currMode != MODE_PAN && currMode != MODE_NONE && currMode != MODE_PAGESEL)
    scribbleDoc->clearSelection();
  Page* selsource = currPage;

  // process any recent strokes
  if(currMode != MODE_STROKE)
    groupStrokes();
  // second pass
  switch(currMode) {
  case MODE_PAN:
    panZoomStart(event);
    break;
  case MODE_STROKE:
  {
    const ScribblePen* pen = currPen();
    if(pen->hasFlag(ScribblePen::SNAP_TO_GRID)) {
      Dim yr = currPage->yruling(true);
      Dim xr = currPage->xruling() > 0 ? currPage->xruling() : yr;
      pos.x = floor(pos.x/xr + 0.5) * xr;
      pos.y = floor(pos.y/yr + 0.5) * yr;
    }
    //Dim w = cfg->Bool("scalePenWithZoom") ? pen->width/mZoom : pen->width;
    bool lineDrawing = pen->hasFlag(ScribblePen::LINE_DRAWING) || pen->hasFlag(ScribblePen::SNAP_TO_GRID);
    StrokeBuilder* builder = StrokeBuilder::create(*pen);
    // install filters
    if(!lineDrawing) {
      // reasonable values are inputSimplify = 2 (0.1 pixel) and inputSmoothing = 5
      Dim simp = cfg->Int("inputSimplify")*0.05/mZoom;
      if(simp > 0)
        builder->addFilter(new SimplifyFilter(simp, pen->usesPressure() ? simp/pen->width : 1.0));
      // low pass preceeds simplify
      int smooth = cfg->Int("inputSmoothing");
      if(smooth > 0)
        builder->addFilter(new LowPassIIR(smooth*0.5/mZoom));   //SymmetricFIR(smooth));
    }
    if(!cfg->Bool("dropFirstPenPoint") || event.source != INPUTSOURCE_PEN) {
      // add two points for line drawing, since second will be removed
      if(pen->hasFlag(ScribblePen::LINE_DRAWING))
        builder->addInputPoint(StrokePoint(pos.x, pos.y, lineDrawPressure, 0, 0, event.t));
      builder->addInputPoint(StrokePoint(pos.x, pos.y,
          event.points[0].pressure, event.points[0].tiltX, event.points[0].tiltY, event.t));
    }
    scribbleDoc->strokeBuilder = builder;
    scribbleDoc->updateCurrStroke(builder->getDirty());  // necessary for single point stroke to show up
    break;
  }
  case MODE_BOOKMARK:
  {
    const Dim BKMK_W = 16;
    const Dim BKMK_H = 30;
    const Point BOOKMARK_POINTS[] = {{0,0}, {BKMK_W,0}, {BKMK_W,BKMK_H}, {BKMK_W/2,(5*BKMK_H)/6}, {0,BKMK_H}, {0,0}};
    Path2D bkmk;
    for(const Point& p : BOOKMARK_POINTS)
      bkmk.addPoint(p);

    currStroke = new Element(new SvgPath(bkmk));
    currStroke->node->addClass("bookmark");
    setSvgFillColor(currStroke->node, app->bookmarkColor);
    //currStroke->setCom(currStroke->bbox().center());
    // note no background for this selection
    currSelection = new Selection(currPage);
    currSelection->addStroke(currStroke);
    Rect bbox = currStroke->bbox();
    bookmarkSnapX = cfg->Bool("snapBookmarks") ? currPage->marginLeft() - 1.5*bbox.width() : 0;
    if(currPage->xruling() > 0)
      pos.x = currPage->xruling() * int(pos.x/currPage->xruling());
    else if(bookmarkSnapX > 0 && pos.x > bookmarkSnapX)
      pos.x -= std::min(pos.x - bookmarkSnapX, bbox.width());
    if(currPage->yruling() > 0)
      pos.y = currPage->yruling() * int(pos.y/currPage->yruling());
    // note the adjustment of dy to move to center vertically in ruleline
    // I'm not sure there is anyway to avoid bookmark not being centered in x if bookmark snap enabled
    currSelection->translate(pos.x, pos.y + (currPage->yruling() - bbox.height())/2);
    break;
  }
  case MODE_ERASESTROKE:
    tempSelection = new Selection(selsource, Selection::STROKEDRAW_NONE);
    tempSelection->selMode = Selection::SELMODE_UNION;
    pathSelector = new PathSelector(tempSelection);
    pathSelector->selectPath(pos, ERASESTROKE_RADIUS/mZoom);
    break;
  case MODE_ERASERULED:
    tempSelection = new Selection(selsource, Selection::STROKEDRAW_NONE);
    tempSelection->selMode = Selection::SELMODE_UNION;
    ruledSelector = new RuledSelector(tempSelection, selColMode);
    if(cfg->Bool("greedyRuledErase"))
      ruledSelector->selMode = RuledSelector::SEL_OVERLAP;
    eraseCurrLine = prevLine;
    eraseXmax = pos.x;
    eraseXmin = pos.x;
    break;
  case MODE_ERASEFREE:
    // use tempSelection to track strokes touched by free eraser
    tempSelection = new Selection(selsource, Selection::STROKEDRAW_NONE);
    tempSelection->selMode = Selection::SELMODE_UNION;
    freeErasePieces = new Selection(selsource, Selection::STROKEDRAW_NORMAL);
    freeErase(pos, pos);
    break;
  case MODE_SELECTRECT:
    currSelection = new Selection(currPage);  // cfg->Bool("liveSelect") ? Selection::SELMODE_NONE
    rectSelector = new RectSelector(currSelection, mZoom, false);  // no handles while selecting
    break;
  case MODE_SELECTRULED:
    currSelection = new Selection(currPage);
    ruledSelector = new RuledSelector(currSelection, selColMode);
    break;
  case MODE_SELECTLASSO:
    currSelection = new Selection(currPage);
    lassoSelector = new LassoSelector(currSelection, 0.5/mZoom);
    lassoSelector->addPoint(pos.x, pos.y);
    break;
  case MODE_SELECTPATH:
    currSelection = new Selection(currPage);
    currSelection->selMode = Selection::SELMODE_UNION;
    pathSelector = new PathSelector(currSelection);
    pathSelector->selectPath(pos, PATHSELECT_RADIUS/mZoom);
    break;
  case MODE_CROPSEL:
  {
    Element* s = currSelection->strokes.front();
    const Transform2D& tf = s->node->getTransform();
    if(tf.isRotating() || tf.xscale() < 0 || tf.yscale() < 0) {
      Element* t = s->cloneNode();
      SvgImage* svgimg = static_cast<SvgImage*>(t->node);
      if(svgimg->srcRect.isValid() && svgimg->srcRect != Rect::wh(svgimg->m_image.width, svgimg->m_image.height))
        svgimg->m_image = svgimg->m_image.cropped(svgimg->srcRect);
      svgimg->srcRect = Rect();
      // actually, we should extract just the rotation from tf, and apply scale to viewport rect
      svgimg->m_image = svgimg->m_image.transformed(tf);
      svgimg->m_bounds = tf.mapRect(svgimg->m_bounds);
      t->node->setTransform(Transform2D());
      // show copy, hide original
      currPage->contentNode->addChild(t->node, s->node);
      currSelection->removeStroke(s);
      currSelection->addStroke(t);
      tempSelection = new Selection(currSelection->page, Selection::STROKEDRAW_NONE);
      tempSelection->addStroke(s);
    }
    break;
  }
  case MODE_SCALESEL:
  case MODE_SCALESELW:
  case MODE_ROTATESEL:
  case MODE_ROTATESELW:
  case MODE_MOVESELFREE:
  case MODE_MOVESELRULED:
    // protect against crash if UI allows move sel mode when it shouldn't
    if(!currSelection)
      currMode = MODE_NONE;
    // nothing to do until mouse actually moves
    break;
  case MODE_INSSPACEVERT:
    tempSelection = new Selection(selsource);
    rectSelector = new RectSelector(tempSelection);
    rectSelector->selectRect(MIN_DIM, pos.y, MAX_DIM, MAX_DIM);
    break;
  case MODE_INSSPACEHORZ:
    tempSelection = new Selection(selsource);
    rectSelector = new RectSelector(tempSelection);
    rectSelector->selectRect(pos.x, MIN_DIM, MAX_DIM, MAX_DIM);
    break;
  case MODE_INSSPACERULED:
    tempSelection = new Selection(selsource);
    ruledSelector = new RuledSelector(tempSelection, selColMode);
    ruledSelector->selectRuledAfter(pos.x, prevLine);
    // if cursor down past left margin, we sort strokes, but we'll only
    //  enable inserting horz space if there are strokes on the first line
    if(pos.x > currPage->marginLeft() && tempSelection->sortRuled() == prevLine)
      insertSpaceX = true;
    else
      insertSpaceX = false;
    // erase strokes convered by negative ruled insert space
    if(cfg->Bool("insSpaceErase")) {
      // second ruled selector for erasing strokes covered by negative insert space
      insSpaceEraseSelection = new Selection(selsource, Selection::STROKEDRAW_NONE);
      insSpaceEraseSelector = new RuledSelector(insSpaceEraseSelection, selColMode);
    }
  default:
    break;
  }
  prevPos = pos;
  initialPos = pos;
  prevRawPos = rawpos;

  if(currMode != MODE_STROKE && widget)
    widget->startTimer(timerPeriod);  // for autoscroll
}

void ScribbleArea::doMoveEvent(const InputEvent& event)
{
  Point rawpos = Point(event.points[0].x, event.points[0].y);
  Point pos = dimToPageDim(screenToDim(rawpos));
  // tablet will happily send many points with same position
  if(pos.x == prevPos.x && pos.y == prevPos.y)
    return;

  Dim dx = pos.x - prevPos.x;
  Dim dy = pos.y - prevPos.y;
  int line = currPage->getLine(pos.y);
  switch(currMode) {
  case MODE_PAN:
    panZoomMove(event, event.points.size(), event.points.size());
    break;
  case MODE_STROKE:
    if(currPen()->hasFlag(ScribblePen::SNAP_TO_GRID)) {
      Dim yr = currPage->yruling(true);
      Dim xr = currPage->xruling() > 0 ? currPage->xruling() : yr;
      Point snappos(floor(pos.x/xr + 0.5) * xr, floor(pos.y/yr + 0.5) * yr);
      // second check provides some hysteresis
      if((snappos.x == prevPos.x && snappos.y == prevPos.y) || 3*pos.dist(snappos) > pos.dist(prevPos))
        return;
      pos = snappos;
    }
    if(currPen()->hasFlag(ScribblePen::LINE_DRAWING)) {
      // some stuff to facilitate debugging of stroke builder
      if(ScribbleInput::pressedKey == SDLK_LEFTBRACKET)
        lineDrawPressure = std::max(Dim(0), 0.8*lineDrawPressure);
      else if(ScribbleInput::pressedKey == SDLK_RIGHTBRACKET)
        lineDrawPressure = std::max(Dim(0), 1.25*lineDrawPressure);
      // press '\' to insert a vertex in line
      if(ScribbleInput::pressedKey == SDLK_BACKSLASH)
        ScribbleInput::pressedKey = 0;
      else
        scribbleDoc->strokeBuilder->removePoints(1);
      if(scribbleDoc->strokeBuilder->getPath()->empty())
        scribbleDoc->strokeBuilder->addInputPoint(StrokePoint(pos.x, pos.y, lineDrawPressure, 0, 0, event.t));
      scribbleDoc->strokeBuilder->addInputPoint(StrokePoint(pos.x, pos.y, lineDrawPressure, 0, 0, event.t));
    }
    else
      scribbleDoc->strokeBuilder->addInputPoint(StrokePoint(pos.x, pos.y,
          event.points[0].pressure, event.points[0].tiltX, event.points[0].tiltY, event.t));
    // current stroke is drawn directly to screen; note that in reqRepaint(),
    //  currPage->getDirty() will return invalid rect since currStroke is not added
    //  to page until cursor release.
    scribbleDoc->updateCurrStroke(scribbleDoc->strokeBuilder->getDirty());
    break;
  case MODE_ERASESTROKE:
    pathSelector->selectPath(pos, ERASESTROKE_RADIUS/mZoom);
    break;
  case MODE_ERASERULED:
    if(pos.x < currPage->marginLeft()) {
      // behave like select ruled if cursor in left margin - allows for very fast erasing
      ruledSelector->selectRuled(eraseXmin, eraseCurrLine, pos.x, line);
      break;
    }
    else if(eraseCurrLine == line) {
      eraseXmax = std::max(eraseXmax, pos.x);
      eraseXmin = std::min(eraseXmin, pos.x);
    }
    else {
      eraseCurrLine = line;
      eraseXmax = pos.x;
      eraseXmin = pos.x;
    }
    ruledSelector->selectRuled(eraseXmin, line, eraseXmax, line);
    break;
  case MODE_ERASEFREE:
    freeErase(prevPos, pos);
    break;
  case MODE_SELECTRECT:
    // selection can either grow or shrink - dirty rect of union of before and after!
    rectSelector->selectRect(initialPos.x, initialPos.y, pos.x, pos.y);
    break;
  case MODE_SELECTRULED:
    // force redraw of selection BG
    ruledSelector->selectRuled(initialPos.x, initialLine, pos.x, line);
    break;
  case MODE_SELECTLASSO:
    // limit number of points in lasso and number of calls to doSelect
    // we can't use rawpos here because that doesn't work with autoscroll
    if(pos.dist(prevPos) < MIN_LASSO_POINT_DIST/mScale)
      return;
    lassoSelector->addPoint(pos.x, pos.y);
    break;
  case MODE_SELECTPATH:
    pathSelector->selectPath(pos, PATHSELECT_RADIUS/mZoom);
    break;
  case MODE_SCALESEL:
  case MODE_SCALESELW:
  {
    Dim xscale = (pos.x - scaleOrigin.x)/(initialPos.x - scaleOrigin.x);
    Dim yscale = (pos.y - scaleOrigin.y)/(initialPos.y - scaleOrigin.y);
    if(scaleLockRatio) {
      Dim scale = std::max(Dim(0.01), std::min(std::abs(xscale), std::abs(yscale)));
      xscale = SGN(xscale)*scale;
      yscale = SGN(yscale)*scale;
    }
    // prevent divide by zero (and excessive shrinkage in a single action)
    if(ABS(xscale) < 0.01) xscale = SGN(xscale)*0.01;
    if(ABS(yscale) < 0.01) yscale = SGN(yscale)*0.01;
    // last arg: scale stroke widths too?
    currSelection->scale(xscale/prevXScale, yscale/prevYScale, scaleOrigin, currMode == MODE_SCALESELW);
    prevXScale = xscale;
    prevYScale = yscale;
    break;
  }
  case MODE_ROTATESEL:
    currSelection->rotate(calcAngle(prevPos, scaleOrigin, pos), scaleOrigin);
    break;
  case MODE_ROTATESELW:
  {
    // alternate (pen button) mode: rotate in 15 deg increments
    Dim prevangle = (M_PI/12)*int(calcAngle(initialPos, scaleOrigin, prevPos)/(M_PI/12));
    Dim angle = (M_PI/12)*int(calcAngle(initialPos, scaleOrigin, pos)/(M_PI/12));
    if(angle != prevangle)
      currSelection->rotate(angle - prevangle, scaleOrigin);
    break;
  }
  case MODE_CROPSEL:
  {
    SvgImage* svgimg = static_cast<SvgImage*>(currSelection->strokes.front()->node);
    if(!svgimg->srcRect.isValid())
      svgimg->srcRect = Rect::wh(svgimg->image()->width, svgimg->image()->height);
    if(std::isnan(scaleOrigin.x)) {
      Dim yscale = (pos.y - scaleOrigin.y)/(initialPos.y - scaleOrigin.y);
      if(yscale <= 0) break;
      Dim maxheight = scaleOrigin.y < svgimg->m_bounds.center().y ?
            svgimg->image()->height - svgimg->srcRect.top : svgimg->srcRect.bottom;
      Dim sy = std::min(yscale/prevYScale, maxheight/svgimg->srcRect.height());
      // internalScale == 0 indicates crop
      currSelection->applyTransform(ScribbleTransform(Transform2D(1, 0, 0, sy, 0, (1 - sy)*scaleOrigin.y), 0, 0));
      prevYScale *= sy;
    }
    else {
      Dim xscale = (pos.x - scaleOrigin.x)/(initialPos.x - scaleOrigin.x);
      if(xscale <= 0) break;
      Dim maxwidth = scaleOrigin.x < svgimg->m_bounds.center().x ?
            svgimg->image()->width - svgimg->srcRect.left : svgimg->srcRect.right;
      Dim sx = std::min(xscale/prevXScale, maxwidth/svgimg->srcRect.width());
      currSelection->applyTransform(ScribbleTransform(Transform2D(sx, 0, 0, 1, (1 - sx)*scaleOrigin.x, 0), 0, 0));
      prevXScale *= sx;
    }
    currSelection->shrink();
    break;
  }
  case MODE_BOOKMARK:
    if(currPage->xruling() == 0 && bookmarkSnapX > 0 && pos.x > bookmarkSnapX) {
      pos.x -= std::min(pos.x - bookmarkSnapX, currStroke->bbox().width());
      dx = pos.x - prevPos.x;
    }
    scribbleDoc->updateCurrStroke(currStroke->bbox());
    if(currPage->xruling() > 0)
      dx = currPage->xruling() * (int(pos.x/currPage->xruling()) - int(prevPos.x/currPage->xruling()));
    if(currPage->yruling() > 0)
      dy = currPage->yruling() * (int(pos.y/currPage->yruling()) - int(prevPos.y/currPage->yruling()));
    currSelection->translate(dx, dy);
    scribbleDoc->updateCurrStroke(currStroke->bbox());
    break;  // note that we don't draw new bookmark on overlay (crashes anyway due to lack of selector)
  case MODE_MOVESELRULED:
  case MODE_MOVESELFREE:
    // we had a comment saying setOffset didn't work as well as translate, but we've arrogantly ignored it to
    //  fix a minor issue with dragging selections between pages
    dx = pos.x - initialPos.x;
    dy = pos.y - initialPos.y;
    // if pos is outside page, draw selection on overlay - we don't use overlay to draw selection inside
    //  page to preserve z-order (relative to unselected strokes)
    if(!currPage->rect().contains(pos) || !screenRect.contains(rawpos)) {
      currSelection->setOffset(dx, dy);
      // if we encounter further issues here, consider using a copy of selection (cloning strokes) for overlay
      currSelection->setDrawType(Selection::STROKEDRAW_NONE);
      app->overlayWidget->drawSelection(currSelection, screenToGlobal(rawpos), -pos, mScale);
      break;
    }
    if(currSelection->drawType() != Selection::STROKEDRAW_SEL) {
      currSelection->setDrawType(Selection::STROKEDRAW_SEL);
      app->overlayWidget->drawSelection(NULL);
    }
    if(currMode == MODE_MOVESELRULED) {
      if(currPage->xruling() > 0)
        dx = currPage->xruling() * (int(pos.x/currPage->xruling()) - int(initialPos.x/currPage->xruling()));
      if(currPage->yruling() > 0)
        dy = currPage->yruling() * (int(pos.y/currPage->yruling()) - int(initialPos.y/currPage->yruling()));
    }
    currSelection->setOffset(dx, dy);
    break;
  case MODE_INSSPACEVERT:
  {
    tempSelection->translate(0, dy);
    Dim ystep = currPage->yruling() > 0 ? currPage->yruling() : GROW_STEP;
    Dim h = tempSelection->count() > 0 ?
        std::max(quantize(tempSelection->getBBox().bottom + 2*ystep, ystep), initialPageSize.height()) :
        quantize(initialPageSize.height() + (pos.y - initialPos.y), ystep/2);
    setPageDims(-1, h, true);
    break;
  }
  case MODE_INSSPACEHORZ:
  {
    tempSelection->translate(dx, 0);
    Dim xstep = currPage->xruling() > 0 ? currPage->xruling() : GROW_STEP;
    Dim w = tempSelection->count() > 0 ?
        std::max(quantize(tempSelection->getBBox().right + 2*xstep, xstep), initialPageSize.width()) :
        quantize(initialPageSize.width() + (pos.x - initialPos.x), xstep/2);
    setPageDims(w, -1, true);
    break;
  }
  case MODE_INSSPACERULED:
  {
    Dim dy0 = tempSelection->count() > 0 ? tempSelection->strokes.back()->pendingTransform().yoffset() : 0;
    if(insertSpaceX && cfg->Bool("reflow"))
      tempSelection->reflowStrokes(pos.x - initialPos.x, line - initialLine, reflowWordSep);
    else
      tempSelection->insertSpace(insertSpaceX ? dx : 0, line - prevLine);
    // resizing page
    if(tempSelection->count() > 0) {
      // this could be greater due to reflow
      Dim maxdy = tempSelection->strokes.back()->pendingTransform().yoffset();
      if(dy0 != maxdy) {
        setPageDims(initialPageSize.width(), initialPageSize.height(), true);
        growPage(tempSelection->getBBox(), 0, std::max(Dim(0), maxdy), true);
      }
    }
    else
      setPageDims(-1, initialPageSize.height() + currPage->yruling(true) * (line - initialLine), true);
    // erase for negative insert space
    if(insSpaceEraseSelection) {
      if(line < initialLine || (line == initialLine && pos.x < initialPos.x))
        insSpaceEraseSelector->selectRuled(pos.x, line, initialPos.x, initialLine);
      else
        insSpaceEraseSelection->clear();
    }
  }
  default:
    break;
  }
  prevPos = pos;
  prevRawPos = rawpos;
  // note that we don't actually need prevLine since it is just getLine(prevPos.y)
  prevLine = line;
}

void ScribbleArea::doReleaseEvent(const InputEvent& event)
{
  // MODE_PAGESEL is unique in that it involves also clicking on pages
  if(event.modemod & MODEMOD_DBLCLICK && dimToPageNum(screenToDim(prevRawPos)) == numPages() && currMode != MODE_PAGESEL) {
    doCancelAction();
    Point oldpos = screenToDim(Point(0, 0));  // save before newPage() jumps position
    scribbleDoc->newPage();
    Point pagetop = pageDimToDim(Point(0,0));  // top-left of new page
    // net effect to scroll up so new page takes 2/3 of screen
    if(viewMode == VIEWMODE_VERT)
      setCornerPos(Point(oldpos.x, pagetop.y - getViewHeight()/mScale/3));
    return;
  }

  // restore original page size (MODE_INSERTSPACE)
  setPageDims(initialPageSize.width(), initialPageSize.height(), true);
  // no changes are made to persistent data structures until release event
  auto currModeType = ScribbleMode::getModeType(currMode);
  // move sel has several cases that require special handling for undo, e.g. drag to different page
  bool undoable = ScribbleMode::isUndoable(currMode) && currModeType != MODE_MOVESEL;
  if(undoable)
    scribbleDoc->startAction(currPageNum);

  switch(currMode) {
  case MODE_PAN:
    panZoomFinish(event);
    break;
  case MODE_PAGESEL:
    if(event.modemod & MODEMOD_CLICK) {
      int pagenum = dimToPageNum(screenToDim(prevRawPos));
      Page* p = page(pagenum);
      if(p && p->rect().contains(prevPos)) {
        // select page range; MODE_ERASE used to detect Shift key pressed
        bool isRange = (event.modemod & MODEMOD_PENBTN) || (event.modemod >> 24) == MODE_ERASE;
        scribbleDoc->selectPages(isRange ? -pagenum-1 : pagenum);  //, prevRawPos + Point(4,4));
      }
      if(scribbleDoc->numSelPages > 0 && cfg->Bool("popupToolbar"))
        app->showSelToolbar(screenToGlobal(prevRawPos + Point(4,4)));  // shift slightly from tap point
    }
    break;
  case MODE_BOOKMARK:
    // previously, we fell through to MODE_STROKE, but now bookmarks are handled separately
    scribbleDoc->updateCurrStroke(currStroke->bbox());
    if(currStroke->bbox().overlaps(currPage->rect())) {
      // set id for bookmark (otherwise, bookmark page will be dirtied when link to bookmark is created)
      currStroke->setNodeId(("b-" + randomStr(10)).c_str());
      // just commit translation directly instead of using Selection::commitTransform() so that we don't
      //  unnecessarily create undo item
      currStroke->commitTransform();
      currPage->addStroke(currStroke);
      delete currSelection;
    }
    else {
      // bookmark dropped off page
      currSelection->strokes.clear();
      delete currSelection;
      currStroke->deleteNode();
    }
    app->overlayWidget->drawSelection(NULL);
    currStroke = NULL;
    currSelection = NULL;
    // we're going to set bookmark id only when it's actually linked to
    break;
  case MODE_STROKE:
  {
    strokeCounter++;
    currStroke = scribbleDoc->strokeBuilder->finish();
    scribbleDoc->updateCurrStroke(scribbleDoc->strokeBuilder->getDirty());
    delete scribbleDoc->strokeBuilder;  // we now own the stroke
    scribbleDoc->strokeBuilder = NULL;
    // discard stroke if entirely off page
    if(!currPage->rect().intersects(currStroke->bbox()) || currPen()->hasFlag(ScribblePen::EPHEMERAL)) {
      // no more growing page w/ off-page strokes - interferes w/ ghost page
      scribbleDoc->updateCurrStroke(currStroke->bbox());  //dirtyScreen(currStroke->bbox());
      currStroke->deleteNode();
      currStroke = NULL;
      break;
    }
    // if stroke is partially off-page, we need to dirty its bbox since strokes on page are clipped to page
    if(!currPage->rect().contains(currStroke->bbox()))
      dirtyScreen(currStroke->bbox());
    if(cfg->Bool("growWithPen"))
      growPage(currStroke->bbox());  // grow page if needed

    // if page is clean, clear page dirty after adding normal stroke so we don't redraw it unnecessarily
    // addChild clears m_renderedBounds, so restore since these are needed if stroke is immediately changed
    Rect r = currStroke->node->m_renderedBounds;
    if(currPen()->hasFlag(ScribblePen::DRAW_UNDER) && currPage->strokeCount() > 0)
      currPage->addStroke(currStroke, *currPage->children().begin());
    else if(currPage->getDirty().isValid())  // we expect page to usually be clean, so this call is cheap
      currPage->addStroke(currStroke);
    else {
      currPage->addStroke(currStroke);
      currPage->clearDirty();
    }
    currStroke->node->m_renderedBounds = r;
    // if this is the first stroke created since last call to groupStrokes(), record it
    groupStrokes(currStroke);
    currStroke = NULL;
    break;
  }
  case MODE_ERASEFREE:
  {
    auto strokes = currPage->children();
    for(auto ii = strokes.begin(); ii != strokes.end();) {
      Element* s = *ii++;
      if(s->isSelected(freeErasePieces)) {
        Element* nexts = ii != strokes.end() ? *ii : NULL;
        s->setSelected(NULL);
        if(s->isPathElement() || s->isMultiStroke()) {
          currPage->contentNode->removeChild(s->node);
          for(Element* ss : s->getEraseSubPaths())
            currPage->addStroke(ss, nexts);
          freeErasePieces->removeStroke(s);
          s->deleteNode();
          ii = std::find(strokes.begin(), strokes.end(), nexts);
          continue;
        }
        else
          scribbleDoc->history->addItem(new StrokeAddedItem(s, currPage, nexts));
      }
    }
    delete freeErasePieces;
    freeErasePieces = NULL;
    // fall through to delete tempSelection
  }
  case MODE_ERASESTROKE:
  case MODE_ERASERULED:
    tempSelection->deleteStrokes();
    break;
  case MODE_SELECTRECT:
    rectSelector->drawHandles = true;
    break;
  case MODE_SELECTRULED:
    // lasso and ruled selectors don't support any interaction, so always convert to rect sel for now
    if(currSelection->count() > 0) {
      // replace ruled selector with rect selector
      delete ruledSelector;
      ruledSelector = NULL;
      rectSelector = new RectSelector(currSelection, mZoom, false);  // ruled sel starts in move ruled mode
    }
    break;
  case MODE_SELECTLASSO:
    if(currSelection->count() > 0) {
      // replace lasso selector with rect selector
      delete lassoSelector;
      lassoSelector = NULL;
      rectSelector = new RectSelector(currSelection, mZoom, true);
    }
    break;
  case MODE_SELECTPATH:
    if(currSelection->count() > 0) {
      delete pathSelector;
      pathSelector = NULL;
      rectSelector = new RectSelector(currSelection, mZoom, true);
    }
    break;
  case MODE_MOVESELFREE:
  case MODE_MOVESELRULED:
  {
    // NOTE: move sel modes must handle undo (startAction, endAction) explicitly!
    // remove from overlay
    currSelection->setDrawType(Selection::STROKEDRAW_SEL);
    app->overlayWidget->drawSelection(NULL);
    // use fast click thresholds to minimize interference with small intentional movements
    if(event.modemod & MODEMOD_FASTCLICK) {
      // tap selection to toggle move free/ruled
      currSelection->selector->drawHandles = !currSelection->selector->drawHandles;
      scribbleDoc->scribbleMode->moveSelMode =
          currSelection->selector->drawHandles ? MODE_MOVESELFREE : MODE_MOVESELRULED;
      // cancel any translation
      currSelection->setOffset(0, 0);
      // force redraw of selection BG
      currSelection->xchgBGDirty(true);
      // redisplay tools
      if(cfg->Bool("popupToolbar")) {
        Rect r = currSelection->getBGBBox();
        app->showSelToolbar(screenToGlobal(dimToScreen(pageDimToDim(Point(r.right, r.bottom)))));
      }
      break;
    }

    // handle drop outside of widget
    if(!screenRect.contains(prevRawPos)) {
      currSelection->setOffset(0, 0);  // leave selected
      scribbleDoc->startAction(currPageNum | UndoHistory::MULTIPAGE);
      // if we pass offset to dropSelection, pos + offset is used by doPasteAt to determine page; instead,
      //  pos and offset should be passed separately to doPasteAt, and only pos used to determine page
      currSelection->setOffset(-initialPos);
      // hack to prevent selection from being cleared in case of same-doc drop
      Selection* sel = currSelection;
      currSelection = NULL;
      bool accepted = app->overlayWidget->dropSelection(sel, screenToGlobal(prevRawPos), Point(0,0));  //-initialPos);
      currSelection = sel;
      currSelection->setOffset(0, 0);
      if(accepted) {
        currSelection->deleteStrokes();
        clearSelection();
      }
      scribbleDoc->endAction();
      break;
    }

    Point pos = screenToDim(prevRawPos);
    int newpagenum = dimToPageNum(pos);
    Point pagepos = pos - getPageOrigin(newpagenum);  // getPageOrigin now works for ghost page
    Page* newpage = page(newpagenum);
    // dropped on ghost page - create the page ... after starting undo action!
    if(newpagenum == numPages() && scribbleDoc->ghostPage && scribbleDoc->ghostPage->rect().contains(pagepos)) {}
    else if(!newpage || !newpage->rect().contains(pagepos)) {
      // dropped outside page - do nothing
      currSelection->setOffset(0, 0);
      break;
    }
    if(newpagenum != currPageNum) {
      // handle drop outside of current page
      bool isMoveFree = currSelection->selector->drawHandles;  // preserve mode
      Point origin1 = pageDimToDim(currSelection->getBBox().origin());
      if(!newpage) {
        scribbleDoc->startAction(newpagenum | UndoHistory::MULTIPAGE);
        scribbleDoc->document->insertPage(scribbleDoc->generatePage(newpagenum), newpagenum);
        pageSizeChanged();
        uiChanged(UIState::InsertPage);
      }
      else
        scribbleDoc->startAction(currPageNum | UndoHistory::MULTIPAGE);
      Clipboard clipboard;
      currSelection->setOffset(0, 0);
      currSelection->toSorted(&clipboard);
      currSelection->deleteStrokes();
      clearSelection();
      setPageNum(newpagenum);
      currSelection = new Selection(currPage);
      rectSelector = new RectSelector(currSelection, mZoom, isMoveFree);
      clipboard.paste(currSelection, true);
      Point origin2 = pageDimToDim(currSelection->getBBox().origin());
      Point dr = origin1 - origin2;

      if(currMode == MODE_MOVESELRULED && currPage->xruling() > 0)
        dr.x = quantize(dr.x, currPage->xruling());
      if(currMode == MODE_MOVESELRULED && currPage->yruling() > 0)
        dr.y = quantize(dr.y, currPage->yruling());
      currSelection->stealthTransform(Transform2D::translating(dr));
    }
    else {
      scribbleDoc->startAction(currPageNum);
      currSelection->commitTransform();
    }
    growPage(currSelection->getBBox());
    scribbleDoc->endAction();
    break;
  }
  case MODE_SCALESEL:
  case MODE_SCALESELW:
  case MODE_ROTATESEL:
  case MODE_ROTATESELW:
    currSelection->commitTransform();
    // possible shrink of image on commit() can invalidate bbox
    currSelection->invalidateBBox();
    growPage(currSelection->getBBox());
    break;
  case MODE_CROPSEL:
    if(tempSelection) {
      Element* s = tempSelection->strokes.front();
      Element* t = currSelection->strokes.front();
      currPage->contentNode->removeChild(t->node);
      currPage->addStroke(t, s);
      currPage->removeStroke(s);
    }
    // internalScale == 0 defeats check for identity tf in commitTransform()
    if(approxEq(currSelection->transform.tf(), Transform2D(), 1E-7))
      currSelection->resetTransform();
    else
      currSelection->commitTransform();
    break;
  case MODE_INSSPACEVERT:
    // TODO: enforcing a minimum page size; maybe in Page::setProperties()
    // use insert space tool above empty space to change page size
    // This probably should happen in real time (i.e. in moveEvent!)
    if(tempSelection->count() > 0) {
      tempSelection->commitTransform();
      growPage(tempSelection->getBBox(), 0, std::max(Dim(0), prevPos.y - initialPos.y));
    }
    else
      setPageDims(-1, currPage->height() + (prevPos.y - initialPos.y));
    break;
  case MODE_INSSPACEHORZ:
    if(tempSelection->count() > 0) {
      tempSelection->commitTransform();
      growPage(tempSelection->getBBox(), std::max(Dim(0), prevPos.x - initialPos.x), 0);
    }
    else
      setPageDims(currPage->width() + (prevPos.x - initialPos.x), -1);
    break;
  case MODE_INSSPACERULED:
    // use insert space tool above empty space to change page size
    if(tempSelection->count() > 0) {
      // this could be greater due to reflow
      Dim maxdy = tempSelection->strokes.back()->pendingTransform().yoffset();
      tempSelection->commitTransform();
      growPage(tempSelection->getBBox(), 0, std::max(Dim(0), maxdy));
    }
    else
      setPageDims(-1, currPage->height() + currPage->yruling(true) * (prevLine - initialLine));
    if(insSpaceEraseSelection)
      insSpaceEraseSelection->deleteStrokes();
  default:
    break;
  }
  if(currSelection) {
    if(currSelection->count() == 0)
      clearSelection();  // for select or erase within selection
    else {
      currSelection->shrink();
      Rect b = dimToScreen(pageDimToDim(currSelection->getBGBBox()));
      if(currModeType == MODE_SELECT) {
        if(cfg->Bool("popupToolbar")) {
          // prevent overlap w/ selection; might need to revisit to handle being shifted to stay on-screen
          Dim y = b.right + 2 > prevRawPos.x && b.bottom + 2 > prevRawPos.y ? b.bottom + 2 : prevRawPos.y;
          if(cfg->Bool("popupToolbar"))
            app->showSelToolbar(screenToGlobal(Point(prevRawPos.x, y)));
        }
      }
#ifdef ONE_TIME_TIPS
      // one-time help tips for selection - on mobile, there is no hover to allow for regular tooltips
      if(!showHelpTips) {}
      else if(currMode == MODE_SCALESEL)
        app->oneTimeTip("scalesel", Point(b.right, b.bottom), _("Bottom right handle scales proportionally.\n"
            "Use other corners to scale freely.\nHold pen button to also scale stroke widths.\n"));
      else if(currMode == MODE_ROTATESEL)
        app->oneTimeTip("rotatesel", Point(b.right, b.bottom), _("Hold pen button to rotate in 15 degree steps.\n"));
      else if(currModeType != MODE_SELECT) {}
      else if(!rectSelector->drawHandles || !rectSelector)
        app->oneTimeTip("movesel", Point(b.right, b.bottom), _("Tap selection to toggle ruled/free mode."));
      else if(rectSelector->enableCrop)
        app->oneTimeTip("cropsel", Point(b.right, b.bottom), _("Drag red handles to crop image."));
      else  // sel toolbar will be shown in this case, so shift the help tip up
        app->oneTimeTip("rectsel", Point(b.right, b.bottom + 50), _("Scale with square handles, rotate with round handle."));
#endif
    }
  }
  clearTempSelection();
  if(undoable)
    scribbleDoc->endAction();

  if(currPage->yruling() == 0)
    currPage->yRuleOffset = 0;
  uiChanged(UIState::ReleaseEvent | (currMode == MODE_STROKE ? UIState::PenRelease : 0));
  currMode = MODE_NONE;
  scribbleDoc->scribbleDone();
}

// For stroke, erase, insert space, and move selection, cancel is equiv to release + undo
// But we still need cancel for pan and select
void ScribbleArea::doCancelAction(bool refresh)
{
  // Should we also call BookmarkView's doCancelAction() in case it's scrolling?
  ScribbleView::doCancelAction();
  switch(currMode) {
  case MODE_NONE:
    groupStrokes();
    return;
  case MODE_PAN:
    panZoomCancel();
    break;
  case MODE_BOOKMARK:
    // must commit selection so that stroke bbox is correct for repaint
    currSelection->commitTransform();
    delete currSelection;
    app->overlayWidget->drawSelection(NULL);
    currSelection = NULL;
    // fall through
  case MODE_STROKE:
    if(scribbleDoc->strokeBuilder) {
      currStroke = scribbleDoc->strokeBuilder->finish();
      delete scribbleDoc->strokeBuilder;
      scribbleDoc->strokeBuilder = NULL;
    }
    if(currStroke) {
      // need to redraw screen (but not image)
      scribbleDoc->updateCurrStroke(currStroke->bbox());
      currStroke->deleteNode();
      currStroke = NULL;
    }
    break;
  case MODE_ERASEFREE:
    if(freeErasePieces)  // should never be NULL, but was in one case due to a bug
      freeErasePieces->deleteStrokes();
    delete freeErasePieces;
    freeErasePieces = NULL;
  case MODE_ERASESTROKE:
  case MODE_ERASERULED:
    // handled by clearTempSelection()
    break;
  case MODE_SELECTRECT:
  case MODE_SELECTRULED:
  case MODE_SELECTLASSO:
  case MODE_SELECTPATH:
    // note that we have not called shrink() yet here!
    clearSelection();
    break;
  case MODE_MOVESELFREE:
  case MODE_MOVESELRULED:
    // return to previous position but leave selected
    currSelection->setOffset(0, 0);
    currSelection->setDrawType(Selection::STROKEDRAW_SEL);
    app->overlayWidget->drawSelection(NULL);
    break;
  case MODE_SCALESEL:
  case MODE_SCALESELW:
  case MODE_ROTATESEL:
  case MODE_ROTATESELW:
    currSelection->resetTransform();
    break;
  case MODE_CROPSEL:
    if(tempSelection) {
      Element* s = tempSelection->strokes.front();
      Element* t = currSelection->strokes.front();
      currPage->contentNode->removeChild(t->node);
      currSelection->removeStroke(t);
      currSelection->addStroke(s);
      t->deleteNode();
    }
    currSelection->resetTransform();
    break;
  case MODE_INSSPACEVERT:
  case MODE_INSSPACEHORZ:
  case MODE_INSSPACERULED:
    setPageDims(initialPageSize.width(), initialPageSize.height());
    // delete selection without commiting - will return strokes to original position
    // setOffset(0, 0) is still necessary to update dirtyRect properly
    tempSelection->setOffset(0, 0);
    break;
  default:
    break;
  }
  clearTempSelection();

  if(currPage->yruling() == 0)
    currPage->yRuleOffset = 0;
  currMode = MODE_NONE;
  if(refresh) {  // && currMode != MODE_NONE ???
    scribbleDoc->scribbleDone();
    uiChanged(UIState::CancelEvent);
  }
}

bool ScribbleArea::doTimerEvent(Timestamp t)
{
  bool res = ScribbleView::doTimerEvent(t);
  // previously, we had config flags for erase, select, move sel and ins space - but these were never touched,
  //  so just hard code for select and move sel w/ a single flag
  if(!cfg->Bool("autoScrollSelect"))
    return res;
  // since selection can be dragged between areas, only auto scroll for move sel if still within current area
  int modetype = ScribbleMode::getModeType(currMode);
  if(modetype != MODE_MOVESEL && modetype != MODE_SELECT)
    return res;
  if(modetype == MODE_MOVESEL && !screenRect.contains(prevRawPos)
       && app->overlayWidget->canDrop(screenToGlobal(prevRawPos)))
    return true;  // don't autoscroll if over a drop target ... but don't stop timer

  Dim autoScrollJump = -cfg->Float("autoScrollSpeed");
  Dim dxright = std::min(AUTOSCROLL_BORDER, prevRawPos.x - (getViewWidth() - AUTOSCROLL_BORDER));
  Dim dxleft = std::max(-AUTOSCROLL_BORDER, prevRawPos.x - AUTOSCROLL_BORDER);
  int pandx = (dxright > 0 ? dxright : (dxleft < 0 ? dxleft : 0)) * autoScrollJump;
  Dim dyright = std::min(AUTOSCROLL_BORDER, prevRawPos.y - (getViewHeight() - AUTOSCROLL_BORDER));
  Dim dyleft = std::max(-AUTOSCROLL_BORDER, prevRawPos.y - AUTOSCROLL_BORDER);
  int pandy = (dyright > 0 ? dyright : (dyleft < 0 ? dyleft : 0)) * autoScrollJump;
  // do the actual panning
  if(pandx != 0 || pandy != 0) {
    doPan(pandx, pandy);
    // position of cursor in Dim space changes to keep screen position the same...
    InputEvent inevent;
    inevent.points.push_back(InputPoint(INPUTEVENT_MOVE, prevRawPos.x, prevRawPos.y));
    doMoveEvent(inevent);
  }
  // kinetic scrolling and "autoscroll" never happen simultaneously, so doRefresh() is only called once
  doRefresh();
  return true;
}

// cursor mode values (in addition to scribble modes, which start at 10)
static constexpr int CURSORMODE_SYSTEM = 0;
static constexpr int CURSORMODE_HIDE = 1;

// generic input event handler called for all motion (pressed or not) over area
void ScribbleArea::doMotionEvent(const InputEvent& event, inputevent_t eventtype)
{
  if(!event.points.empty() && eventtype != INPUTEVENT_RELEASE)
    rawPos = Point(event.points[0].x, event.points[0].y);

  // previously, this was a separate fn, but we need input source information to fully determine cursorMode
  // ideally, we would hide cursor and reset releasePos on pen proximity exit
  int newCursorMode = cursorMode;
  // show system cursor as soon as the mouse moves unless it is enabled for drawing; also use system cursor if
  //  selection present (e.g., to make grabbing handles easier) - also hides frozen cursor bug w/ popup
  //  toolbar after clicking selection; consider using SvgGui.hoveredWidget instead of enter/leave events
  if(currSelection || eventtype == INPUTEVENT_LEAVE ||
      (event.source == INPUTSOURCE_MOUSE && scribbleInput->mouseMode != INPUTMODE_DRAW))
    newCursorMode = CURSORMODE_SYSTEM;
  // hide cursor on finger up; also need to do this for pen on mobile since we don't get hover events
  else if(eventtype == INPUTEVENT_RELEASE && (PLATFORM_MOBILE || event.source == INPUTSOURCE_TOUCH))
    newCursorMode = CURSORMODE_HIDE;
  // we need to set cursor on enter but we need to wait for the motion event with input source set
  // there are too many complications with touch+pen+mouse+window focus gain/lost to not check every event
  else if(eventtype == INPUTEVENT_PRESS || eventtype == INPUTEVENT_RELEASE || eventtype == INPUTEVENT_MOVE) {
    // except for above cases, cursor can only change on press, release, and enter
    int mode = scribbleDoc->getScribbleMode(event.modemod);
    // never hide mouse cursor
    if(currMode == MODE_STROKE)
      newCursorMode = CURSORMODE_HIDE;
    else if(drawCursor == 0 && mode == MODE_STROKE) {
      if(eventtype == INPUTEVENT_RELEASE) {
        releasePos = rawPos;
        newCursorMode = CURSORMODE_HIDE;
      }
      else if(!releasePos.isNaN() && releasePos.dist(rawPos) < 100)
        newCursorMode = CURSORMODE_HIDE;
      else {
        releasePos = Point(NAN, NAN);
        newCursorMode = MODE_STROKE;
      }
    }
    else {
      // current mode overrides mode passed in
      if(scribbleInput->scribbling == ScribbleInput::SCRIBBLING_PAN)
        mode = MODE_PAN;
      else if(scribbleInput->scribbling == ScribbleInput::SCRIBBLING_DRAW)
        mode = currMode;
      // override MODE_BOOKMARK mode type of MODE_STROKE
      newCursorMode = mode == MODE_BOOKMARK ? CURSORMODE_SYSTEM : ScribbleMode::getModeType(mode);
    }
  }
  // mouse cursor can NEVER be hidden; CURSORMODE_HIDE implies MODE_STROKE; must check this every time in
  //  case, e.g., mouse is moved after pen input with drawCursor == 0
  if(event.source == INPUTSOURCE_MOUSE && newCursorMode == CURSORMODE_HIDE) // || scribbleInput->currInputSource == INPUTSOURCE_MOUSE
    newCursorMode = MODE_STROKE;
  if(newCursorMode != cursorMode) {
    cursorMode = newCursorMode;
#if !PLATFORM_MOBILE
    // in the future, we will have custom cursors for other modes to support pen hover on iOS/Android
    if(cursorMode == CURSORMODE_HIDE || (drawCursor == 2 && (cursorMode == MODE_STROKE || cursorMode == MODE_ERASE))) {
      SDL_ShowCursor(SDL_DISABLE);
    }
    else {
      if(cursorMode == MODE_PAN)
        SDL_SetCursor(panCursor.get());
      else if(cursorMode == MODE_STROKE)
        SDL_SetCursor(penCursor.get());
      else if(cursorMode == MODE_ERASE)
        SDL_SetCursor(eraseCursor.get());
      else
        SDL_SetCursor(SDL_GetDefaultCursor());
      SDL_ShowCursor(SDL_ENABLE);
    }
#endif
  }

  if(drawCursor == 2 && (cursorMode == MODE_STROKE || cursorMode == MODE_ERASE)) {
    Dim hw = MIN_CURSOR_RADIUS*preScale, hh = MIN_CURSOR_RADIUS*preScale;
    if(cursorMode == MODE_STROKE) {
      Rect penbox = currPen()->getBBox();
      hw = std::max(hw, mScale*penbox.width()/2);
      hh = std::max(hh, mScale*penbox.height()/2);
    }
    else {
      hw = preScale*(ERASEFREE_RADIUS + 1);
      hh = hw;
    }
    Rect r = Rect::centerwh(rawPos, 2*hw, 2*hh).pad(1.5);  //ltrb(-hw, -hh, hw, hh).pad(1.5).translate(rawPos);
    dirtyRectScreen.rectUnion(hoverRect.rectUnion(r));
    hoverRect = r;
  }
  else if(hoverRect.isValid()) {
    // hide custom cursor
    dirtyRectScreen.rectUnion(hoverRect);
    hoverRect = Rect();
  }
}

// drawing related methods

void ScribbleArea::reqRepaint()
{
  //scribbleDoc->dirtyPage(currPageNum); ... this has to be done for all views before reqRepaint!
  // see if we need to redraw selection background
  Rect newbg = currSelection ? currSelection->getBGBBox() : Rect();
  if((currSelection && currSelection->xchgBGDirty(false)) || newbg != selBGRect)
    dirtyScreen(selBGRect.rectUnion(newbg));
  selBGRect = newbg;
  ScribbleView::reqRepaint();
}

void ScribbleArea::dirtyPage(int pagenum, Rect dirty)
{
  //Rect dirty = page(pagenum)->getDirty();
  dirty = (pagenum == currPageNum) ? pageDimToDim(dirty) : dirty.translate(getPageOrigin(pagenum));
  // pad by 2 units in Dim space since stroke bbox does not include widening of selected stroke
  // TODO: it would be better to do this in StrokeGroup if STROKEDRAW_SEL; also why 2 instead of 1???
  // this is only needed after union with dirtyBG for selections < min sel size and zoom > 1
  dirty.pad(2);
  // avoid merging in a dirty rect outside viewport - it would unnecessarily enlarge dirtyRectDim
  if(dirty.overlaps(viewportRect))
    dirtyRectDim.rectUnion(dirty);
}

void ScribbleArea::dirtyScreen(const Rect& dirty)
{
  // pad dirty rect by 2 pixels to account for antialiasing ... padding for AA now handled at a higher level
  dirtyRectScreen.rectUnion(dimToScreen(pageDimToDim(dirty)));  //.pad(2));
}

// drawing strokes must be as fast as possible, so just draw on top without
//  using the dirty rect mechanism when possible
void ScribbleArea::drawStrokeOnImage(Element* stroke)
{
#ifdef QT_CORE_LIB
  imgPaint->reset();
  imgPaint->setAntiAlias(true);
  imgPaint->translate(xorigin, yorigin);
  imgPaint->scale(mScale, mScale);
  imgPaint->translate(currPageXOrigin, currPageYOrigin);
  //stroke->draw(&imgPaint, Rect());
  SvgExtraStates extrastates;
  stroke->node->draw(imgPaint, extrastates);
#endif
}

void ScribbleArea::updateContentDim()
{
  contentHeight = 0;
  contentWidth = 0;
  // determine content dimensions based on view mode
  // TODO: I think we can cache these values and update in pageSizeChanged()
  switch(viewMode) {
  case VIEWMODE_SINGLE:
    contentHeight = currPage->height();
    contentWidth = currPage->width();
    break;
  case VIEWMODE_VERT:
    for(int ii = 0; ii < numPages(); ii++) {
      contentHeight += page(ii)->height() + pageSpacing;
      // TODO: this should be based on visible pages, not all pages!
      contentWidth = std::max(contentWidth, page(ii)->width());
    }
    break;
  case VIEWMODE_HORZ:
    for(int ii = 0; ii < numPages(); ii++) {
      // TODO: this should be based on visible pages, not all pages!
      contentHeight = std::max(contentHeight, page(ii)->height());
      contentWidth += page(ii)->width() + pageSpacing;
    }
    //default: break;
  }
  // calculate scroll limits from content and view dimensions
  // our approach to limiting horz scrolling is a bit cheesy ... can we improve it?
  Dim viewwidth = getViewWidth();
  Dim viewheight = getViewHeight();
  Dim border = cfg->Float("BORDER_SIZE");
  maxOriginX = cfg->Float("horzBorder");
  if(maxOriginX <= 0 || viewMode == VIEWMODE_HORZ)  // disable for horz scroll - need room for ghost page
    maxOriginX = border * viewwidth;
  //maxOriginX = MAX(maxOriginX, cfg->Float("panBorder")*preScale + 5);
  maxOriginY = border * viewheight;
  minOriginX = -contentWidth*mScale - maxOriginX + viewwidth;
  minOriginY = -contentHeight*mScale - maxOriginY + viewheight;
  // minOriginX/Y == maxOriginX/Y means no scrolling is possible, so make sure
  //  content is centered in window
  if(minOriginX > maxOriginX)
    minOriginX = maxOriginX = (minOriginX + maxOriginX)/2;
  // +10 to prevent very small scroll range which causes undesired behavior with scroll handle
  if(minOriginY + 10 > maxOriginY)
    minOriginY = maxOriginY = (minOriginY + maxOriginY)/2;
}

void ScribbleArea::drawWatermark(Painter* painter, Page* page, const Rect& dirty)
{
#ifdef SCRIBBLE_IAP
  if(!watermark)
    return;
  painter->clipRect(Rect::wh(page->props.width, page->props.height));
  Dim w = watermark->width, h = watermark->height;
  for(Dim y = 0; y < page->props.height; y += h) {
    for(Dim x = 0; x < page->props.width; x += w) {
      Rect dest = Rect::ltwh(x, y, w, h);
      if(dest.intersects(dirty))
        painter->drawImage(dest, *watermark);
    }
  }
#endif
}

// Note that we assume dirty rect is larger than clip rect so we don't have to deal with weirdness at the
//  boundaries from some stuff being antialiased but other stuff not, non-pixel aligned boundaries, etc.
void ScribbleArea::drawImage(Painter* painter, const Rect& dirty)
{
  // draw the gray background
  painter->setAntiAlias(false);
  painter->fillRect(dirty, BACKGROUND_COLOR);
  painter->setAntiAlias(true);

  // handle special case of dirty rect limited to current page
  Rect pagedirty = dimToPageDim(dirty);
  if(viewMode == VIEWMODE_SINGLE ||
     Rect::ltwh(0, 0, currPage->width() + pageSpacing, currPage->height() + pageSpacing).contains(pagedirty)) {
    painter->save();
    painter->translate(currPageXOrigin, currPageYOrigin);
    if(currPage->scaleFactor != 1) {
      painter->scale(currPage->scaleFactor);
      pagedirty = Transform2D::scaling(1/currPage->scaleFactor).mult(pagedirty);
    }
    currPage->draw(painter, pagedirty);
    drawWatermark(painter, currPage, pagedirty);
    painter->restore();
    return;
  }
  // general case
  Dim w = 0;
  Dim h = 0;
  for(int ii = 0; ii <= numPages(); ii++) {
    bool ghost = ii == numPages();
    Page* pg = ghost ? scribbleDoc->ghostPage.get() : page(ii);
    if(!pg) break;  // clipping area, e.g., has no ghost page
    if((viewMode == VIEWMODE_VERT && h + pg->height() > dirty.top)
       || (viewMode == VIEWMODE_HORZ && w + pg->width() > dirty.left))  {
      // account for page centering
      if(!centerPages) {}
      else if(viewMode == VIEWMODE_VERT)
        w = (contentWidth - pg->width())/2;
      else if(viewMode == VIEWMODE_HORZ)
        h = (contentHeight - pg->height())/2;
      // convert global dirty rect to page dirty rect
      pagedirty = dirty;
      pagedirty.translate(-w, -h);
      // draw
      painter->save();
      painter->translate(w, h);
      if(pg->scaleFactor != 1) {
        painter->scale(pg->scaleFactor);
        pagedirty = Transform2D::scaling(1/pg->scaleFactor).mult(pagedirty);
      }
      if(ghost)
        painter->setOpacity(0.25);
      pg->draw(painter, pagedirty);
      drawWatermark(painter, pg, pagedirty);
      painter->restore();
    }
    if(viewMode == VIEWMODE_VERT)
      h += pg->height() + pageSpacing;
    else if(viewMode == VIEWMODE_HORZ)
      w += pg->width() + pageSpacing;
    if((viewMode == VIEWMODE_VERT && h > dirty.bottom) || (viewMode == VIEWMODE_HORZ && w > dirty.right))
      break;
  }
}

void ScribbleArea::drawScreen(Painter* painter, const Rect& dirty)
{
  // we want the option of not having to redraw strokes while selection is being
  //  made (this is liveSelect = false), so selection background must be drawn on top of
  //  paper and strokes
  // Note that background is not drawn for tempSelection
  painter->save();
  painter->translate(xorigin + panxoffset, yorigin + panyoffset);
  painter->scale(mScale, mScale);
  painter->translate(currPageXOrigin, currPageYOrigin);
  // draw in-progress stroke
  if(scribbleDoc->strokeBuilder) {
    painter->save();
    // this is a hack to fix the drawing of current stroke on top of any page except the correct one in other
    //  views; an alternative fix would be to just add the current stroke to the page (also allowing
    //  strokeBuilder to be moved back to ScribbleArea, among other simplifications), but I was concerned
    //  (probably prematurely and irrationally) about performance for very big pages
    if(scribbleDoc->activeArea->currPageNum != currPageNum) {
      Point origin = getPageOrigin(scribbleDoc->activeArea->currPageNum);
      painter->translate(origin.x - currPageXOrigin, origin.y - currPageYOrigin);
    }
    //scribbleDoc->strokeBuilder->draw(painter, Rect());
    SvgPainter(painter).drawNode(scribbleDoc->strokeBuilder->getElement()->node);
    painter->restore();
  }
  // currStroke is now only used for Add Bookmark
  if(currStroke)
    SvgPainter(painter).drawNode(currStroke->node);

  if(currSelection && currSelection->drawType() == Selection::STROKEDRAW_SEL
      && (currSelPageNum == currPageNum || viewMode != VIEWMODE_SINGLE)) {
    if(currSelPageNum != currPageNum) {
      Point origin = getPageOrigin(currSelPageNum);
      painter->translate(origin.x - currPageXOrigin, origin.y - currPageYOrigin);
    }
    currSelection->setZoom(mZoom);
    currSelection->drawBG(painter);
  }
  painter->restore();

  // draw the current pen or tool when hovering - note different scaling for pen vs. eraser
  if(dirty.overlaps(hoverRect)) {
    painter->save();
    painter->translate(rawPos.x, rawPos.y);
    if(cursorMode == MODE_STROKE) {
      Rect penbox = currPen()->getBBox();
      // minimum pen cursor size = 2 pix * preScale
      Dim hw = std::max(MIN_CURSOR_RADIUS, mScale*penbox.width()/2);
      Dim hh = std::max(MIN_CURSOR_RADIUS, mScale*penbox.height()/2);
      painter->setStroke(Color::WHITE, 1);
      painter->setFillBrush(currPen()->color);
      if(currPen()->hasFlag(ScribblePen::TIP_CHISEL))
        painter->drawPath(Path2D().addRect(
            Rect::centerwh(Point(0,0), 2*hw/FilledStrokeBuilder::CHISEL_ASPECT_RATIO, 2*hw)));
      else
        painter->drawPath(Path2D().addEllipse(0, 0, hw + 0.5, hh + 0.5));
      // if rawPos is ever updated w/o updating hoverRect, this needs to become a rectUnion
      hoverRect = Rect::centerwh(rawPos, 2*hw, 2*hh).pad(1.5);
    }
    else if(cursorMode == MODE_ERASE) {
      Dim d = 2*ERASEFREE_RADIUS;
      //painter->scale(preScale, preScale);
      painter->setStroke(Color::BLACK, 1);
      painter->setFillBrush(Color::WHITE);
      painter->drawPath(Path2D().addEllipse(0, 0, d/2, d/2));
      hoverRect = Rect::centerwh(rawPos, d, d).pad(1);
    }
    painter->restore();
  }
}
