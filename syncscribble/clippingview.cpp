#include "clippingview.h"

#include "page.h"
#include "scribbledoc.h"
#include "scribbleapp.h"
#include "scribblewidget.h"


void ClippingView::loadConfig(ScribbleConfig* _cfg)
{
  _cfg->set("drawCursor", 1);
  _cfg->set("horzBorder", 6);
  _cfg->set("viewMode", 1);  // vertical scrolling
  ScribbleArea::loadConfig(_cfg);
  // override some config settings
  //scribbleInput->singleTouchMode = INPUTMODE_PAN;
  scribbleInput->multiTouchMode = INPUTMODE_PAN;  // prevent any zooming
  centerPages = true;
  pageSpacing = 15;
}

void ClippingView::doPressEvent(const InputEvent& event)
{
  Point rawpos = Point(event.points[0].x, event.points[0].y);
  globalPos = screenToGlobal(Point(rawpos.x, rawpos.y));

  Point pos = screenToDim(rawpos);
  clipNum = dimToPageNum(pos);
  if(clipNum < 0)
    return;
  setPageNum(clipNum);
  if(!currPage->rect().contains(dimToPageDim(pos)))
    return;
  if(event.modemod == MODEMOD_PENBTN) {
    doLongPressAction(globalPos);  // this is currently a no-op
    return;
  }
  selectAll(Selection::STROKEDRAW_NORMAL);
  if(!currSelection)
    return;
  // translate selection so that grab point is at (0,0)
  //currSelection->setOffset(-dimToPageDim(pos));
  //Point dr = rawpos - dimToScreen(pageDimToDim(currPage->scaleFactor*currSelection->getBBox().center()));
  selOffset = -dimToPageDim(pos)/currPage->scaleFactor;
  currMode = MODE_MOVESELFREE;  // so that autoscroll works when dragging near edge
  initPointerTime = event.t;
  currPanLength = 0;
  prevRawPos = rawpos;
  if(currSelection->selector)
    currSelection->selector->drawHandles = false;

  delTarget->setVisible(true);
}

void ClippingView::doMoveEvent(const InputEvent& event)
{
  if(!currSelection)
    return;

  Point rawpos = Point(event.points[0].x, event.points[0].y);
  globalPos = screenToGlobal(rawpos);
  Point dr = rawpos - prevRawPos;
  currPanLength += sqrt(dr.x*dr.x + dr.y*dr.y);
  Dim scale = currPage->scaleFactor*mScale;
  //Dim scale = screenRect.contains(rawpos) ? currPage->scaleFactor*mScale : 0;
  app->overlayWidget->drawSelection(currSelection, globalPos, selOffset, scale);  // - selCenterOffset, scale);
  prevRawPos = rawpos;
}

void ClippingView::doReleaseEvent(const InputEvent& event)
{
  if(!currSelection)
    return;

  // translate coordinates to main ScribbleArea and figure out if we need to paste the clipping
  app->overlayWidget->drawSelection(NULL);

  if(delTarget->node->bounds().contains(globalPos))
    deleteClipping();
  else if(!screenRect.contains(prevRawPos)) {
    // replaceids = true
    bool accepted = app->overlayWidget->dropSelection(currSelection, globalPos, selOffset, true);
    if(accepted && cfg->Bool("autoHideClippings"))
      app->hideClippings();
  }
  else {
    // reorder pages
    int dropnum = dimToPageNum(screenToDim(prevRawPos));
    if(dropnum != clipNum)
      scribbleDoc->movePage(clipNum, dropnum);
  }
  clearSelection();
  currMode = MODE_NONE;
  delTarget->setVisible(false);
}

// not actually used since Esc key isn't passed to ClippingView!
void ClippingView::doCancelAction(bool refresh)
{
  clearSelection();
  app->overlayWidget->drawSelection(NULL);
  currMode = MODE_NONE;
}

bool ClippingView::doClickAction(Point pos)
{
  pos = screenToDim(pos);

  return true; // always accept, since no double click action
}

void ClippingView::pageSizeChanged()
{
  // set scale factor for each page
  Dim s = (getViewWidth() - 2*cfg->Float("horzBorder"))/mScale;
  for(int ii = 0; ii < numPages(); ii++)
    page(ii)->scaleFactor = MIN(Dim(1.0), s/page(ii)->props.width);
  ScribbleArea::pageSizeChanged();
}

// we take Selection instead of Clipboard so we can access source page
bool ClippingView::selectionDropped(Selection* selection, Point pos, Point offset, bool replaceids)
{
  // replaceids is ignored - no reason to change ids of dropped content, since ids are replaced every time
  //  clipping is pasted
  Clipboard clip;
  selection->toSorted(&clip);
  if(clip.count() == 0) return false;  // shouldn't happen, but be certain since empty page can't be deleted
  Rect selbbox = selection->getBBox();
  Dim padx = std::max(3, int(selbbox.width()/20));
  Dim pady = std::max(3, int(selbbox.height()/20));
  selbbox.pad(padx, pady);  // page should be a bit bigger than clipping
  // insert after page under drop position
  Point droppos = screenToDim(globalToScreen(pos));
  int dropnum = droppos.y < 0 ? 0 : (dimToPageNum(droppos) + 1);
  // use source page background
  Color pagecolor = selection->page->props.color;
  Page* page = new Page(PageProperties(selbbox.width(), selbbox.height(), 0, 0, 0, pagecolor));
  scribbleDoc->startAction(dropnum);
  // ScribbleDoc::insertPage moves view so new page is at top ... but we don't want any change in view
  Point prevpos = screenToDim(Point(0, 0));
  scribbleDoc->insertPage(page, dropnum);  // this calls clearSelection() (shouldn't be needed though)
  setCornerPos(prevpos);
  currSelection = new Selection(page);
  clip.paste(currSelection, true);  // move = true
  Point dr = Point(padx, pady) - currSelection->getBBox().origin();
  currSelection->stealthTransform(Transform2D::translating(dr));
  scribbleDoc->endAction();
  clearSelection();
  uiChanged(UIState::InsertPage);
  scribbleDoc->doRefresh();
  return false;  // do not delete selection from source
}

void ClippingView::deleteClipping()
{
  // ScribbleDoc::deletePage jumps view
  Point prevpos = screenToDim(Point(0, 0));
  scribbleDoc->deletePage(clipNum);
  setCornerPos(prevpos);
  scribbleDoc->doRefresh();
}
