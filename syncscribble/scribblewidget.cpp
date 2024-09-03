#include "scribblewidget.h"
#include "scribblearea.h"
#include "selection.h"

#include "ugui/widgets.h"  // TODO: move loadSVGFragment somewhere else
//extern const char* scribbleScrollerSVG;

const char* scrollHandleSVG = R"#(
<g class="scribble-scroller" box-anchor="right top">
  <rect fill="none" box-anchor="vfill" width="30" height="36"/>
  <rect fill="#000" fill-opacity="0.4" box-anchor="vfill" x="22" width="4" height="36"/>
</g>
)#";

static const char* scrollIndSVG = R"#(<g class="scroll-indicator" box-anchor="right top">
  <rect fill="#888" box-anchor="vfill" width="4" height="20" rx="2" ry="2"/>
</g>)#";

ScribbleWidget* ScribbleWidget::create(Widget* container, ScribbleView* area)
{
  // We need to run layout for scribble container contents (scroller, statusbar, etc.) separately since we
  //  need to make adjustments if size changes but can't touch anything in the middle of layout
  // unfortunately, we need to add two levels of <g> (outer to separate layout, inner for content); can't
  //  use container directly because it contains split layout sizer
  static const char* scribbleWrapperSVG = R"#(
    <g class="scribble-inner-container" box-anchor="fill">
      <g class="scribble-content" box-anchor="fill" layout="box">
      </g>
    </g>
  )#";

  Widget* inner = new Widget(loadSVGFragment(scribbleWrapperSVG));
  container->addWidget(inner);
  Widget* contents = inner->selectFirst(".scribble-content");
  ScribbleWidget* areaWidget = new ScribbleWidget(area);
  Widget* scrollIndicator = new Widget(loadSVGFragment(scrollIndSVG));
  Widget* scrollContainer = new Widget(loadSVGFragment(scrollHandleSVG));
  areaWidget->setScroller(scrollContainer->selectFirst(".scribble-scroller"), scrollIndicator->selectFirst(".scroll-indicator"));
  areaWidget->node->setAttribute("box-anchor", "fill");
  contents->addWidget(areaWidget);
  contents->addWidget(scrollIndicator);
  contents->addWidget(scrollContainer);
  scrollContainer->setLayoutIsolate(true);
  // isolate layout so page number change when scrolling doesn't trigger full relayout; can't set on
  //  statusbar since scroll handle will be dirty too
  contents->setLayoutIsolate(true);

  // this is needed for findLayoutDirtyRoot to descend into container
  inner->onPrepareLayout = [container](){
    return container->node->bounds();
  };

  inner->onApplyLayout = [area, areaWidget, contents](const Rect& src, const Rect& dest){
    area->screenOrigin = dest.origin();
    if(dest.width() <= 0 || dest.height() <= 0) {
      contents->setVisible(false);
      return true;
    }
    contents->setVisible(true);
    if(src != dest) {
      // can't rely on setLayoutTransform because we still must invalidate if size changes but dx,dy = 0
      areaWidget->m_layoutTransform = Transform2D::translating(dest.origin());
      areaWidget->node->invalidate(true);
    }
    if(area->screenRect != dest.toSize())
      area->doResizeEvent(dest);

    areaWidget->window()->gui()->layoutWidget(contents, dest);
    return true;
  };

  // already handled by container's onApplyLayout
  areaWidget->onApplyLayout = [](const Rect& src, const Rect& dest){ return true; };

  return areaWidget;
}

// ScribbleView adapter - consider a merge/reshuffle between this, ScribbleView, and ScribbleInput
ScribbleWidget::ScribbleWidget(ScribbleView* sv) : Widget(new SvgCustomNode), scribbleView(sv)
{
  // this is needed only for timers; since SvgGui/Application stuff, including timer stuff, should
  //  technically be static, try to find a better soln!
  sv->widget = this;

  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::ENTER || event->type == SvgGui::LEAVE) {
      scribbleView->doMotionEvent(InputEvent(),
          event->type == SvgGui::ENTER ? INPUTEVENT_ENTER : INPUTEVENT_LEAVE);
      scribbleView->ScribbleView::doRefresh();  // skip update of other views and UI
      return true;
    }
    else if(event->type == SvgGui::OUTSIDE_PRESSED) {
      // send (mouse button up outside when pressed widget) as simple button up event
      // this was initially to support overlay widget
      SDL_Event* btnevent = static_cast<SDL_Event*>(event->user.data1);
      if(!scribbleView->scribbleInput->sdlEvent(gui, btnevent))
        return false;
      scribbleView->doRefresh();
      return true;
    }
    else if(event->type == SvgGui::FOCUS_GAINED || event->type == SvgGui::FOCUS_LOST) {
      // send focus in/out events as enter/leave to handle window focus gained/lost
      scribbleView->doMotionEvent(InputEvent(),
          event->type == SvgGui::FOCUS_GAINED ? INPUTEVENT_ENTER : INPUTEVENT_LEAVE);
      scribbleView->ScribbleView::doRefresh();  // skip update of other views and UI
      return true;
    }
    else if(event->type == SvgGui::TIMER) {
      return scribbleView->doTimerEvent(0);
    }
    else if(event->type == SDL_MOUSEWHEEL) {
      // since mouse wheel zooming was mainly added to support pinch zoom on Wacom tablets, we previously
      //  didn't do it if pinch zoom was disabled for touch
      //if(scribbleView->scribbleInput->multiTouchMode == INPUTMODE_ZOOM) {
      uint32_t mods = (PLATFORM_WIN || PLATFORM_LINUX) ? (event->wheel.direction >> 16) : SDL_GetModState();
      if(mods & KMOD_CTRL) {
        Dim speed = scribbleView->cfg->Float("wheelZoomSpeed")/120.0;
        Point p = window()->gui()->prevFingerPos - scribbleView->screenOrigin;
        scribbleView->zoomBy(std::pow(1.25, speed*event->wheel.y), p.x, p.y);
        scribbleView->doRefresh();
      }
      else {
        Dim speed = scribbleView->cfg->Float("wheelScrollSpeed");
        if(mods & KMOD_SHIFT)
          scribbleView->scrollBy(speed*event->wheel.y, -speed*event->wheel.x);
        else
          scribbleView->scrollBy(-speed*event->wheel.x, speed*event->wheel.y);
      }
      return true;
    }
    else if(event->type == SDL_KEYDOWN) {
      // we will only get key event if we are *pressed*, since we are not focusable; if that changes, we
      //  need to check scribbling != NOT_SCRIBBLING so that Esc gets passed up for quick exit in debug mode
      if(event->key.keysym.sym == SDLK_ESCAPE) {
        scribbleView->scribbleInput->cancelAction();
        scribbleView->doRefresh();
        return true;
      }
      ScribbleInput::pressedKey = event->key.keysym.sym;
    }
    else if(event->type == SDL_KEYUP)
      ScribbleInput::pressedKey = 0;
    else if(event->type == SvgGui::LONG_PRESS) {
      // we accept both touchId == SvgGui::SVG_GUI_LONGPRESSID and touchId == SvgGui::SVG_GUI_LONGPRESSALTID
      // long press is a separate event which does not interfere with normal touch input processing
      //  SVG_GUI_LONGPRESSALTID is for different widget under touch point than when initially pressed, which
      //  for ScribbleView means overlay widget
      scribbleView->doLongPressAction(Point(event->tfinger.x, event->tfinger.y));  //gui->currInputPoint);
      return true;  // SvgGui ignores this currently
    }
    else {
      auto prev = scribbleView->scribbleInput->scribbling;
      if(scribbleView->scribbleInput->sdlEvent(gui, event)) {
        // SDL_FINGERDOWN case added to make it easier to recover from messed up state
        if((prev == ScribbleInput::NOT_SCRIBBLING || event->type == SDL_FINGERDOWN) &&
            scribbleView->scribbleInput->scribbling != ScribbleInput::NOT_SCRIBBLING)
          gui->setPressed(this);
        scribbleView->doRefresh();
        return true;
      }
    }
    return false;
  });
}

void ScribbleWidget::setScroller(Widget* scrollhandle, Widget* scrollind)
{
  scroller = scrollhandle;
  scrollIndicator = scrollind;
  // a generic drag handler doesn't really work because, mouse can keep moving after reaching edge of area
  scroller->addHandler([this](SvgGui* gui, SDL_Event* event) {
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      //initScrollerY = event->button.y - scroller->node->transformedBounds().top;
      prevScrollerY = event->tfinger.y;
      gui->setPressed(scroller);
      return true;
    }
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == scroller) {
      Rect scrollerBBox = scrollIndicator->node->bounds();  //scroller->node->bounds();
      // this failed approach requires scroller position be updated immediately in doPan (instead of
      //  in paintEvent) ... but that causes scroller to get squished down due to something going wrong on
      //  initial layout
      //Dim dy = initScrollerY - (event->motion.y - scrollerBBox.top);
      //scribbleView->scrollFrac(0, dy/(scribbleView->getViewHeight() - scrollerBBox.height()));
      Dim h = scribbleView->getViewHeight() - scrollerBBox.height();
      Dim y0 = scribbleView->getScrollFrac().y;
      scribbleView->scrollFrac(0, (prevScrollerY - event->tfinger.y)/h);  //event->motion.y
      prevScrollerY += h*(y0 - scribbleView->getScrollFrac().y);
      return true;
    }
    return false;
  });
}

Rect ScribbleWidget::bounds(SvgPainter* svgp) const
{
  return m_layoutTransform.mapRect(scribbleView->screenRect); //Rect(scribbleView->screenRect).translate(scribbleView->screenOrigin);
}

Rect ScribbleWidget::dirtyRect() const
{
  const Rect& dirty = scribbleView->dirtyRectScreen;
  return dirty.isValid() ? m_layoutTransform.mapRect(dirty) : Rect();
}

void ScribbleWidget::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  p->clipRect(scribbleView->screenRect);
  p->setsRGBAdjAlpha(false);  // this is enabled for GUI; disable for ScribbleView
  scribbleView->doPaintEvent(p, m_layoutTransform.inverse().mapRect(svgp->dirtyRect));
}

void ScribbleWidget::startTimer(Dim periodMs)
{
  window()->gui()->setTimer(periodMs, this);
}

// passed scroll position as a fraction in [0,1]
void ScribbleWidget::setScrollPosition(Dim pos, Dim vfrac)
{
  if(pos < 0 || pos > 1) {
    scroller->setVisible(false);
    scrollIndicator->setVisible(false);
    return;
  }

  scrollIndicator->setVisible(true);
  Dim viewh = scribbleView->getViewHeight();
  Dim indh = std::max(real(60), viewh * vfrac);
  Dim indy = (viewh - indh)*pos;
  if(scrollIndicator->margins().top != indy)
    scrollIndicator->setMargins(indy, 4, 0, 0);

  SvgRect* rectNode = static_cast<SvgRect*>(scrollIndicator->containerNode()->selectFirst("rect"));
  rectNode->setRect(Rect::wh(4, indh), 2, 2);

  // scroll handle needs to be centered wrt scroll indicator
  scroller->setVisible(true);
  Dim scrollh = scroller->node->bounds().height();
  Dim scrolly = indy + (indh - scrollh)/2;  //(viewh - scrollh)*pos;
  if(scroller->margins().top != scrolly)
    scroller->setMargins(scrolly, 0, 0, 0);

  //if(scroller->m_layoutTransform.yoffset() != scrolly)
  //  scroller->setLayoutTransform(Transform2D().translate(0, scrolly));

  //real w = scroller->node->bounds().width();
  //real yc = bbox.top + (bbox.height() - h)*scrollY/scrollLimits.bottom + h/2;
  //scroller->setLayoutBounds(Rect::centerwh(Point(scribbleView->getViewWidth() - w/2 - 1, scrolly), w, scrollh));

}

void ScribbleWidget::showScroller()
{
  if(!window() || !window()->gui())
    return;
  Dim opacity = 1;
  // finalopacity = -0.8 = 10 x -0.08 steps, 50 ms each = 500 ms hold before setting visible to false - to
  //  account for user's reaction time
  Dim finalopacity = scribbleView->cfg->Bool("autoHideScroller") ? -0.8 : 0.36;  //0.24;
  scroller->node->setAttr<float>("opacity", opacity);
  scrollIndicator->node->setAttr<float>("opacity", opacity);
  scrollerFadeTimer = window()->gui()->setTimer(2500, this, scrollerFadeTimer, [this, opacity, finalopacity]() mutable {
    opacity -= 0.08;
    if(finalopacity < 0)
      scroller->node->setAttr<float>("opacity", std::max(Dim(0), opacity));
    scrollIndicator->node->setAttr<float>("opacity", std::max(Dim(0), opacity));
    if(finalopacity < 0 && opacity <= finalopacity) {
      scroller->setVisible(false);
      scrollIndicator->setVisible(false);
    }
    return opacity > finalopacity ? 50 : 0;
  });
}

/// OverlayWidget

OverlayWidget::OverlayWidget(Widget* under)
  : Widget(new SvgCustomNode), underWidget(under), selection(NULL), m_bounds(Rect::wh(100, 100))
{
  setVisible(false);
  node->setAttribute("box-anchor", "fill");

  onApplyLayout = [this](const Rect& src, const Rect& dest){
    m_bounds = dest.toSize();
    if(src != dest) {
      m_layoutTransform.translate(dest.left - src.left, dest.top - src.top);
      node->invalidate(true);
    }
    return true;
  };
}

Rect OverlayWidget::bounds(SvgPainter* svgp) const
{
  return m_layoutTransform.mapRect(m_bounds);
}

void OverlayWidget::draw(SvgPainter* svgp) const
{
  if(!selection)
    return;

  Painter* p = svgp->p;
  p->clipRect(m_bounds);  // note that translation from layout transform has already been applied
  p->translate(globalPos - bounds(svgp).origin());
  p->scale(scale, scale);
  p->translate(offset);

  p->setsRGBAdjAlpha(false);  // this is enabled for GUI but disabled for ScribbleView
  selection->draw(p, Selection::STROKEDRAW_SEL);
  //selection->setZoom(scale);
  selection->drawBG(p);
  renderedSelBounds = currSelBounds;
}

ScribbleArea* OverlayWidget::getScribbleArea(Point gpos) const
{
  SvgNode* n = underWidget->containerNode()->nodeAt(gpos);
  bool hit = n && (n->hasClass("scribbleArea") || n->hasClass("clippingView"));
  return hit ? static_cast<ScribbleArea*>(static_cast<ScribbleWidget*>(n->ext())->scribbleView) : NULL;
}

bool OverlayWidget::dropSelection(Selection* clip, Point gpos, Point dr, bool replaceids)
{
  ScribbleArea* targetArea = getScribbleArea(gpos);
  return targetArea ? targetArea->selectionDropped(clip, gpos, dr, replaceids) : false;
}

void OverlayWidget::drawSelection(Selection* sel, Point gpos, Point dr, Dim s)
{
  setVisible(sel != NULL);
  selection = sel;
  if(!sel)
    return;

  // note that we always use original scale for clippingView
  SvgNode* n = underWidget->containerNode()->nodeAt(gpos);
  if(n && n->hasClass("scribbleArea")) {
    ScribbleArea* targetArea = static_cast<ScribbleArea*>(static_cast<ScribbleWidget*>(n->ext())->scribbleView);
    scale = targetArea->getScale();
  }
  else
    scale = s;

  globalPos = gpos;
  offset = dr;
  selection->setZoom(scale);
  Transform2D tf = Transform2D().translate(offset).scale(scale).translate(globalPos);
  currSelBounds = tf.mapRect(selection->getBGBBox());
  redraw();
}
