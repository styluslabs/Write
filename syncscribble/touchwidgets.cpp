#include "touchwidgets.h"
//#include "scribbledoc.h"
//#include "scribbleapp.h"
#include "strokebuilder.h"
#include "ugui/textedit.h"
#include "usvg/svgparser.h"
#include "usvg/svgpainter.h"


static void drawCheckerboard(Painter* p, Dim w, Dim h, int nrows, Color fill)
{
  Dim checkh = h/nrows;
  Dim checkw = checkh;
  int ncols = int(w/checkw);
  if(ncols < w/checkw) ++ncols;  // ceil
  for(int row = 0; row < nrows; ++row) {
    for(int col = row % 2; col < ncols; col += 2)
      p->fillRect(Rect::ltwh(checkw*col, checkh*row, checkw, checkh), fill);
  }
}

Color PenPreview::bgColor(Color::WHITE);

PenPreview::PenPreview() : Widget(new SvgCustomNode), mBounds(Rect::wh(200, 70)) //, penNum(n)  //int n
{
  // TODO: this is now used in three places - deduplicate!
  onApplyLayout = [this](const Rect& src, const Rect& dest){
    mBounds = dest.toSize();
    if(src != dest) {
      m_layoutTransform.translate(dest.left - src.left, dest.top - src.top);
      node->invalidate(true);
    }
    return true;
  };
}

Rect PenPreview::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(mBounds);
}

// we'll assume pen can't be changed when preview is displayed, and thus never call redraw()
void PenPreview::draw(SvgPainter* svgp) const
{
  const ScribblePen* pen = &mPen;
  Painter* p = svgp->p;
  int w = mBounds.width() - 4;
  int h = mBounds.height() - 4;
  p->translate(2, 2);
  p->clipRect(Rect::wh(w, h));
  // use color of current page as background
  p->fillRect(Rect::wh(w, h), bgColor);  //doc->getCurrPageColor());
  drawCheckerboard(p, w, h, 4, 0x18000000);
  // draw the pen stroke
  bool isLine = pen->hasFlag(ScribblePen::SNAP_TO_GRID) || pen->hasFlag(ScribblePen::LINE_DRAWING);
  Path2D path;
  path.moveTo(0.1f*w, 0.5f*h);
  isLine ? path.lineTo(Point(0.9f*w, 0.5f*h)) : path.cubicTo(0.3f*w, 0.2f*h, 0.7f*w, 0.8f*h, 0.9f*w, 0.5f*h);
  Path2D flat = path.toFlat();
  std::unique_ptr<StrokeBuilder> sb(StrokeBuilder::create(*pen));
  Timestamp t = 0;
  for(const Point& pt : flat.points) {
    Dim a = std::abs(2*pt.x - w)/w;  // 0 at edges of widget (>0 at ends of path), 1 in middle
    sb->addInputPoint(StrokePoint(pt.x, pt.y, 1.0 - a*a, 0, 0, t));
    t += 100 - 60*a;
  }
  SvgPainter(p).drawNode(sb->getElement()->node);
}

// undo dial
double ButtonDragDial::posToAngle(const Point& pos)
{
  Rect bbox = toolBtn->node->bounds();
  Point center = bbox.center() + Point(0, 0.6*5*bbox.height());
  Point p = pos - center;
  //Point p = pos - node->transformedBounds().center();

  return atan2(p.y, p.x);  //-atan2(p.x, p.y);
}

void ButtonDragDial::updateDial(Dim angle, bool active, int count)
{
  indAngle = angle;
  indActive = active;
  indCount += count;
  redraw();
}

ButtonDragDial::ButtonDragDial(Button* tb) : AbsPosWidget(new SvgCustomNode), toolBtn(tb), mBounds(Rect::wh(100, 100)),
    stepAngle(2*M_PI/32), indAngle(0), indActive(false), indCount(0), prevAngle(0), altMode(false)
{
  setVisible(false);  // initially invisible
  node->setAttribute("position", "absolute");
  toolBtn->addWidget(this);

  // We'll try replacing button's main event handler ... code is a little weird because `this` is dial widget,
  //  while toolBtn is actual button
  // if this does work out, need to add an official Widget::clearHandlers()
  toolBtn->sdlHandlers.clear();
  toolBtn->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN || isLongPressOrRightClick(event)) {
      // let's try long press to switch to alt mode
      toolBtn->node->setXmlClass(addWord(removeWord(toolBtn->node->xmlClass(), "hovered"), "pressed").c_str());
      altMode = event->tfinger.fingerId != SDL_BUTTON_LMASK;
      dialMoved = false;
      indCount = 0;
      prevAngle = posToAngle(Point(event->tfinger.x, event->tfinger.y));
      updateDial(prevAngle, true, 0);
      setVisible(true);
      gui->setPressed(toolBtn);
    }
    else if(event->type == SDL_FINGERMOTION && isVisible()) {
      Dim deltaAngle = posToAngle(Point(event->tfinger.x, event->tfinger.y)) - prevAngle;
      if(deltaAngle > M_PI)
        deltaAngle -= 2*M_PI;
      else if(deltaAngle < -M_PI)
        deltaAngle += 2*M_PI;
      int delta = deltaAngle/stepAngle;
      int initdelta = delta;
      if(delta != 0) {
        dialMoved = true;
        prevAngle = fmod(prevAngle + delta*stepAngle, 2*M_PI);
        delta = altMode ? onAltStep(delta) : onStep(delta);
        updateDial(prevAngle, delta == 0, initdelta - delta);
      }
    }
    else if(event->type == SDL_FINGERUP || event->type == SvgGui::OUTSIDE_PRESSED) {
      toolBtn->node->removeClass("pressed");
      setVisible(false);
      if(!altMode && !dialMoved && event->type == SDL_FINGERUP)
        onStep(-1);
      else if(altMode && dialMoved)
        onAltStep(0);
    }
    else if(event->type == SvgGui::ENTER && !gui->pressedWidget)
      toolBtn->node->addClass("hovered");
    else if(event->type == SvgGui::LEAVE && !gui->pressedWidget)
      toolBtn->node->removeClass("hovered");
    else
      return false;
    return true;
  });
}

Rect ButtonDragDial::bounds(SvgPainter* svgp) const
{
  //return svgp->p->getTransform().mapRect(mBounds);
  Rect btnbbox = toolBtn->node->bounds();
  Dim h = 5*btnbbox.height();
  return svgp->p->getTransform().mapRect(Rect::wh(h,h));
  //return svgp->p->getTransform().mapRect(Rect::centerwh(btnbbox.center() + Point(0, 0.55*h), h, h));
}

// we cheat here by ignoring layout transform and drawing relative to toolbutton's bbox
void ButtonDragDial::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  Rect bbox = node->bounds();
  Dim w = bbox.width(), h = bbox.height();
  p->translate(w/2, h/2);
  p->scale(w/83.3, h/83.3); // previously value of 100 w/ 6x size was unnecessarily large (now 83.3 w/ 5x)

  // to ease keeping appearance consistent vs. varying DPI, we just draw onto a 100 x 100 canvas scaled to
  //  fill the frame
  int a = 80;
  Dim angle = indAngle; // - M_PI/2;
  Dim sweep = fmod(2*M_PI + indCount*stepAngle, 2*M_PI);
  if(sweep < 0)
    sweep += 2*M_PI;
  if(indCount == 0)
    sweep = 2*M_PI;  // show complete filled circle for initial state

  if(sweep != 0) {
    p->setStrokeBrush(Color::NONE);
    p->setFillBrush(altMode ? Color(255, 128, 128, 128) : Color(128, 128, 255, 128));
    Path2D path;
    path.moveTo(0, 0);
    path.lineTo(0.5*a*cos(angle), 0.5*a*sin(angle));
    path.addArc(0, 0, 0.5*a, 0.5*a, angle, -sweep);
    path.closeSubpath();  //lineTo(0, 0);
    p->drawPath(path);
  }

  p->setFillBrush(Color::NONE);
  p->setStroke(Color(128, 128, 128), 1.5, Painter::RoundCap, Painter::RoundJoin);
  for(Dim ang = 0; ang < 2*M_PI; ang += stepAngle)
    p->drawLine(Point(39*sin(ang - indAngle), 39*cos(ang - indAngle)),
        Point(33*sin(ang - indAngle), 33*cos(ang - indAngle)));

  // for debugging
  //fprintf(stderr, "angle: %f  sweep: %f  angle+sweep: %f", angle*180/M_PI, sweep*180/M_PI, (angle+sweep)*180/M_PI);
  //p->fillRect(Rect::centerwh(Point(0.5*a*cos(angle), 0.5*a*sin(angle)), 5, 5), Color::GREEN);
  //p->fillRect(Rect::centerwh(Point(0.5*a*cos(angle-sweep), 0.5*a*sin(angle-sweep)), 5, 5), Color::RED);
}

// most modern applications (at least on mobile) won't have any menubars, so complicating Button class to
//  support menubar doesn't seem right
Menubar::Menubar(SvgNode* n) : Toolbar(n)
{
  // same logic as Menu except we use `this` instead of `parent()` - any way to deduplicate?
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::OUTSIDE_PRESSED) {
      // close unless button up over parent (assumed to include opening button)
      Widget* target = static_cast<Widget*>(event->user.data2);
      if(!target || !target->isDescendantOf(this))
        gui->closeMenus();  // close entire menu tree
      return true;
    }
    if(event->type == SvgGui::OUTSIDE_MODAL) {
      Widget* target = static_cast<Widget*>(event->user.data2);
      gui->closeMenus();  // close entire menu tree
      // swallow event (i.e. return true) if click was within menu's parent to prevent reopening
      return target && target->isDescendantOf(this);
    }
    return false;
  });

  isPressedGroupContainer = true;
}

void Menubar::addButton(Button* btn)
{
  // this will run before Button's handler
  btn->addHandler([btn, this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::ENTER) {
      // if our menu is open, enter event can't close it, and we have class=pressed, so don't add hovered
      if(!btn->mMenu || !btn->mMenu->isVisible()) {
        bool isPressed = gui->pressedWidget != NULL;
        // if a menu is open, we won't be sent enter event unless we are in same pressed group container
        bool sameMenuTree = !gui->menuStack.empty();
        gui->closeMenus(btn);  // close sibling menu if any
        // note that we only receive pressed enter event if button went down in our group
        if(btn->mMenu && (isPressed || sameMenuTree)) {
          btn->node->addClass("pressed");
          gui->showMenu(btn->mMenu);
        }
        else
          btn->node->addClass(isPressed ? "pressed" : "hovered");
      }
      return true;
    }
    else if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      // close menu bar menu on 2nd click
      if(btn->mMenu && !gui->menuStack.empty() && btn->mMenu == gui->menuStack.front()) {
        btn->node->removeClass("hovered");
        gui->closeMenus();
        return true;  // I don't think it makes sense to send onPressed when we are clearing pressed state!
      }
    }
    else if(event->type == SDL_FINGERUP && (!btn->mMenu || autoClose)) {
      gui->closeMenus();
      return false;  // continue to button handler
    }
    else if(isLongPressOrRightClick(event) && btn->mMenu) {
      if(!btn->mMenu->isVisible()) {
        btn->node->addClass("pressed");
        gui->showMenu(btn->mMenu);
      }
      gui->pressedWidget = NULL;  //setPressed(btn->mMenu) doesn't work as menubar is pressed group container
      return true;
    }
    return false;
  });

  addWidget(btn);
}

// would be nice to deduplicate this cut and paste from Toolbar::addAction() (w/o using virtual!)
Button* Menubar::addAction(Action* action)
{
  Button* item = createToolbutton(action->icon(), action->title.c_str());
  action->addButton(item);
  addButton(item);
  // handler added in addButton() stops propagation of ENTER event, so tooltip handler must preceed it
  setupTooltip(item, action->tooltip.empty() ? action->title.c_str() : action->tooltip.c_str());
  return item;
}

Menubar* createMenubar() { return new Menubar(widgetNode("#toolbar")); }
Menubar* createVertMenubar() { return new Menubar(widgetNode("#vert-toolbar")); }

// previously, adjFn only made one adjustment and was called in a loop, but having adjFn drive the layout
//  process instead allows for simplier code and potentially more efficiency
void AutoAdjContainer::repeatLayout(const Rect& dest)
{
  //window()->absPosNodes.erase(window()->absPosNodes.begin() + numAbsPos, window()->absPosNodes.end());
  window()->gui()->layoutWidget(contents, dest);
}

// if this is useful, we can move to widgets.cpp as generic recursive layout container
AutoAdjContainer::AutoAdjContainer(SvgNode* n, Widget* _contents) : Widget(n), contents(_contents)
{
  addWidget(contents);

  onApplyLayout = [this](const Rect& src, const Rect& dest){
    // fit contents to container
    //numAbsPos = window()->absPosNodes.size();
    window()->gui()->layoutWidget(contents, dest);
    adjFn(src, dest);
    contentsBBox = contents->node->bounds();
    return true;
  };

  onPrepareLayout = [this](){
    // we will typically only stretch along one direction - need to get content size for other direction
    if(contentsBBox.isValid())
      return contentsBBox.toSize();
    //contents->setLayoutTransform(Transform2D());
    //setLayoutTransform(Transform2D());
    window()->gui()->layoutWidget(contents, Rect::wh(0, 0));
    Rect bbox = contents->node->bounds();
    return bbox.toSize();
  };
}
