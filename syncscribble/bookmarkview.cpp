#include "bookmarkview.h"

#include "page.h"
#include "scribbledoc.h"

static constexpr int BOOKMARK_MARGIN = 5;
static constexpr Dim MAX_BOOKMARK_WIDTH = 800;

BookmarkView::BookmarkView(ScribbleConfig* _cfg, ScribbleDoc* doc) : scribbleDoc(doc)
{
  loadConfig(_cfg);
}

void BookmarkView::setScribbleDoc(ScribbleDoc* doc)
{
  setCornerPos(Point(0,0));  // reset scroll position
  scribbleDoc = doc;
}

void BookmarkView::repaintBookmarks()
{
  // we assume this is called only if bookmark view is visible
  //scribbleDoc->document->ensurePagesLoaded();
  Dim prevh = bookmarksHeight;
  getContentDim(getViewWidth(), getViewHeight());
  if(bookmarksHeight != prevh)
    ScribbleView::pageSizeChanged();
  repaintAll();
  reqRepaint();
}

void BookmarkView::loadConfig(ScribbleConfig* _cfg)
{
  ScribbleView::loadConfig(_cfg);
  // direct all (single pointer) input through doPress/Move/ReleaseEvent()
  scribbleInput->mouseMode = INPUTMODE_DRAW;
  scribbleInput->singleTouchMode = INPUTMODE_DRAW;
  scribbleInput->multiTouchMode = INPUTMODE_ZOOM;  //INPUTMODE_PAN;
}

void BookmarkView::doPressEvent(const InputEvent& event)
{
  panZoomStart(event);
  Point pos = screenToDim(prevPointerCOM);
  Element* b = findBookmark(scribbleDoc->document, pos.y);
  if(b) {
    unHighlightHit();
    bookmarkHit = b;
    highlightHit(pos);
  }
}

void BookmarkView::doMoveEvent(const InputEvent& event)
{
  panZoomMove(event, event.points.size(), event.points.size());
  if(!(event.modemod & MODEMOD_MAYBECLICK))
    unHighlightHit();
}

void BookmarkView::doReleaseEvent(const InputEvent& event)
{
  unHighlightHit();
  panZoomFinish(event);
}

// not actually used since Esc key isn't passed to BookmarkView!
void BookmarkView::doCancelAction(bool refresh)
{
  //panZoomCancel();
}

bool BookmarkView::doClickAction(Point pos)
{
  pos = screenToDim(pos);
  // returns document position given bookmark list y pos
  int pagenum;
  Element* b = findBookmark(scribbleDoc->document, pos.y, &pagenum);
  if(b) {
    // clicking bookmark with pen button is a shortcut for creating hyperref
    if(scribbleInput->currModeMod == MODEMOD_PENBTN) {
      StrokeProperties props(Color::fromArgb(cfg->Int("linkColor")), -1);
      scribbleDoc->activeArea->createHyperRef(b, &props);
    }
    else
      scribbleDoc->bookmarkHit(pagenum, b);
  }
  return true; // always accept, since no double click action
}

// needed to cancel highlight if 2nd finger goes down
void BookmarkView::doMotionEvent(const InputEvent& event, inputevent_t eventtype)
{
  if(event.points.size() > 1)
    unHighlightHit();
}

void BookmarkView::unHighlightHit()
{
  if(bookmarkHit) {
    //scribbleDoc->document->bookmarkHit = NULL;
    bookmarkHit = NULL;
    dirtyRectDim.rectUnion(hitDirtyRect);
    reqRepaint();
  }
}

static Dim bookmarkRowHeight(Page* page)
{
  // alternatively, consider yruling + <some padding>
  return MAX((Dim)40, page->yruling(true));
}

void BookmarkView::highlightHit(Point pos)
{
  //scribbleDoc->document->bookmarkHit = bookmarkHit;
  Dim h =  bookmarkRowHeight(scribbleDoc->document->pageForElement(bookmarkHit));
  // this is pretty hacky, but redrawing 3 bookmarks instead of 1 isn't the end of the world

  // ... no, sorry, having dirty rect boundry pass through middle of bookmarks above/below causes problems
  //  if bookmark "label" has been edited
  // also, parameterize initial highlight count (or at least account for timer value)

  hitDirtyRect = Rect::ltrb(-5, pos.y - h, 1024, pos.y + h);
  dirtyRectDim.rectUnion(hitDirtyRect);
  reqRepaint();
}

// rendering

void BookmarkView::pageSizeChanged()
{
  repaintAll();
  getContentDim(getViewWidth(), getViewHeight());
  // ensure that position is still valid
  ScribbleView::pageSizeChanged();
  scribbleDoc->uiChanged(UIState::PageSizeChange);  // for zoom label
}

// needed for zoom label
void BookmarkView::doRefresh()
{
  reqRepaint();
  scribbleDoc->doRefresh();
}

void BookmarkView::getContentDim(Dim viewwidth, Dim viewheight)
{
  bookmarksHeight = 0;
  Dim bookmarksWidth = 0;
  for(Page* page : scribbleDoc->document->pages) {
    if(page->numBookmarks < 0) continue;  // number of bookmarks unknown
    bookmarksHeight += page->numBookmarks * bookmarkRowHeight(page);
    bookmarksWidth = std::max(bookmarksWidth, page->maxBookmarkWidth);
  }

  Dim contentheight = bookmarksHeight;
  Dim contentwidth = std::min(MAX_BOOKMARK_WIDTH, bookmarksWidth + 2*BOOKMARK_MARGIN);
  maxOriginX = 0; //0.5 * (viewwidth - contentwidth);
  maxOriginY = 0; //0.5 * (viewheight - contentheight);
  minOriginX = MIN(maxOriginX, -contentwidth*mScale - maxOriginX + viewwidth);
  minOriginY = MIN(maxOriginY, -contentheight*mScale - maxOriginY + viewheight);
}

Element* BookmarkView::findBookmark(Document* doc, Dim bookmarky, int* pagenumout)
{
  if(bookmarky > 0 && bookmarky < bookmarksHeight) {
    int pagenum = 0;
    for(auto ii = doc->pages.begin(); ii != doc->pages.end(); ++ii, ++pagenum) {
      Page* page = *ii;
      if(page->numBookmarks < 0) continue;  // this should never happen
      Dim yheight = page->numBookmarks * bookmarkRowHeight(page);
      if(bookmarky < yheight) {
        page->ensureLoaded();
        if(pagenumout)
          *pagenumout = pagenum;
        page->bookmarks.sort(page->cmpRuled());
        auto jj = page->bookmarks.begin();
        std::advance(jj, int(bookmarky / bookmarkRowHeight(page)));
        return *jj;
      }
      bookmarky -= yheight;
    }
  }
  return NULL;
}

// Given that bookmark ordering can be changed by moving strokes, we need the ability to sort bookmark list
//  (alternative would be rebuilding list by scanning all strokes on all pages).  For now, we just resort
//  list everytime it's drawn.  In the future, we could resort only if strokes have been moved.
// How to jump to a bookmark?  Just search through all bookmarks until ypos corresponding to click is found

void BookmarkView::drawPageBookmarks(Painter* painter, Page* page, Dim ypos, Dim xmin, int nbkmks)
{
  page->ensureLoaded();
  std::unique_ptr<Selection> selection(new Selection(page, Selection::STROKEDRAW_NORMAL));
  selection->selMode = Selection::SELMODE_PASSIVE;
  RuledSelector* selector = new RuledSelector(selection.get());
  // ensure bookmarks are sorted (see note above)
  Dim rowh = bookmarkRowHeight(page);
  page->maxBookmarkWidth = 0;
  int dx = 0, dy = 0;
  for(Element* b : page->bookmarks) {
    // for margin content mode (xmin != MAX_DIM), include strokes extending past margin (which this would not
    //  be included in bookmarks directly), e.g. highlighter
    Dim xl = xmin == MAX_DIM ? b->bbox().left : std::min(xmin, 0.0);
    if(page->yruling() > 0) {
      int ruleline = page->getLine(b);
      // select bookmark symbol and everything to the right of it on the line
      selector->selectRuled(xl, ruleline, MAX_DIM, ruleline);
      dy = -int(ruleline * page->yruling() - ypos);
    }
    else {
      Dim ymin = b->bbox().center().y - Page::BLANK_Y_RULING / 2;
      selector->selectRuled(RuledRange(xl, ymin,
        MAX_DIM, ymin + Page::BLANK_Y_RULING, page->props.xRuling, Page::BLANK_Y_RULING));
      dy = -int(ymin - ypos);
    }
    // note dx integer to translate strokes by integer number of pixels; dx != MAX_DIM for margin content mode
    dx = xmin == MAX_DIM ? -int(b->bbox().left) + BOOKMARK_MARGIN : -int(xmin) + BOOKMARK_MARGIN;
    // darken BG of every other row
    if(nbkmks % 2 != 0)
      painter->fillRect(Rect::ltwh(0, ypos, MAX_BOOKMARK_WIDTH, rowh), Color(0, 0, 0, 24));
    // blue BG for selected bookmark
    if(b == bookmarkHit)
      painter->fillRect(Rect::ltwh(0, ypos, MAX_BOOKMARK_WIDTH, rowh), Color(0, 0, 255, 52));

    page->maxBookmarkWidth = std::max(page->maxBookmarkWidth, selection->getBBox().width());
    painter->translate(dx, dy);
    Element::FORCE_NORMAL_DRAW = true;
    selection->draw(painter);
    Element::FORCE_NORMAL_DRAW = false;
    painter->translate(-dx, -dy);
    selection->clear();
    ypos += rowh;
    ++nbkmks;
  }
}

void BookmarkView::drawBookmarks(Painter* painter, Document* doc, const Rect& dirty)
{
  if(!dirty.isValid()) return;
  // draw background first, then strokes so that strokes can bleed over between lines (at page boundaries)
  Dim ypos = 0;
  Color bg = Color::WHITE;
  Dim xmin = MAX_DIM;
  int mode = cfg->Int("bookmarkMode");
  for(Page* page : doc->pages) {
    if(page->loadStatus == Page::NOT_LOADED && page->numBookmarks == 0) continue;
    page->ensureLoaded();
    page->bookmarks.clear();
    if(mode == MARGIN_CONTENT) {
      // note that this will only select strokes entirely contained in margin (as desired)
      std::unique_ptr<Selection> marginsel(new Selection(page, Selection::STROKEDRAW_NORMAL));
      marginsel->selMode = Selection::SELMODE_PASSIVE;
      RectSelector* margintor = new RectSelector(marginsel.get());
      Dim margin = page->marginLeft() > 0 ? page->marginLeft() : std::min(100.0, 0.1*page->width());
      margintor->selectRect(0, 0, margin, page->height());
      if(marginsel->count() == 0)
        continue;
      // we could do better for unruled page, but this should be OK expect some cases of consecutive lines
      marginsel->sortRuled();
      auto end = std::unique(marginsel->strokes.begin(), marginsel->strokes.end(),
          [page](Element* a, Element* b){ return page->getLine(a) == page->getLine(b); });
      page->bookmarks.assign(marginsel->strokes.begin(), end);
      for(Element* b : page->bookmarks)
        xmin = std::min(xmin, b->bbox().left);
    }
    else {
      // every time we select (e.g. for drawing each bookmark line), we iterate over all strokes anyway
      for(Element* s : page->children()) {
        if(s->isBookmark())
          page->bookmarks.push_back(s);
      }
      page->bookmarks.sort(page->cmpRuled());
    }
    Dim yheight = page->bookmarks.size() * bookmarkRowHeight(page);
    if(yheight > 0) {
      bg = page->props.color;  // use page color for background color
      if(ypos + yheight > dirty.top && ypos < dirty.bottom)
        painter->fillRect(Rect::ltrb(dirty.left, ypos, dirty.right, std::min(dirty.bottom, ypos + yheight + 1)), bg);
      else if(ypos > dirty.bottom)
        break;
    }
    ypos += yheight;
    page->numBookmarks = int(page->bookmarks.size());
  }
  // fill the rest of the dirty rect based on the last page color
  if(ypos < dirty.bottom)
    painter->fillRect(Rect::ltrb(dirty.left, std::max(dirty.top, ypos), dirty.right, dirty.bottom), bg);

  ypos = 0;
  int nbkmks = 0;
  for(Page* page : doc->pages) {
    if(page->numBookmarks <= 0) continue;
    Dim yheight = page->numBookmarks * bookmarkRowHeight(page);
    if(ypos + yheight > dirty.top && ypos < dirty.bottom)
      drawPageBookmarks(painter, page, ypos, xmin, nbkmks);
    else if(ypos > dirty.bottom)
      break;
    ypos += yheight;
    nbkmks += page->numBookmarks;
  }
  // update scroll limits
  getContentDim(getViewWidth(), getViewHeight());
}

void BookmarkView::drawImage(Painter* imgpaint, const Rect& dirty)
{
  drawBookmarks(imgpaint, scribbleDoc->document, dirty);
}
