#ifndef TOUCHWIDGETS_H
#define TOUCHWIDGETS_H

#include "ugui/widgets.h"
#include "ugui/colorwidgets.h"
#include "scribblepen.h"

class ScribbleApp;

class PenPreview : public Widget
{
public:
  PenPreview();  //int n = -1);
  void setPen(const ScribblePen& pen) { mPen = pen; redraw(); }
  // assuming this is derived from SvgNodeLayout, we have access to layout transform and so can use it to set
  //  canvas size instead of scaling what we draw
  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;

  Rect mBounds;
  static Color bgColor;
private:
  //int penNum;
  ScribblePen mPen = {Color::INVALID_COLOR, -1};
};

// this is Widget for the actual dial canvas; toolbutton is just a regular toolbutton with special sdlEvent handler
class ButtonDragDial : public AbsPosWidget
{
public:
  ButtonDragDial(Button* tb);
  //~ButtonDragDial();

  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;

  // These should of course be private
  std::function<int(int delta)> onStep;
  std::function<int(int delta)> onAltStep;

private:
  double posToAngle(const Point& pos);
  void updateDial(Dim angle, bool active, int count);

  Button* toolBtn;
  Rect mBounds;
  //int yCenter;

  double stepAngle;
  double indAngle;
  bool indActive;
  int indCount;
  double prevAngle;
  bool dialMoved;
  bool altMode;
};

class Menubar : public Toolbar
{
public:
  Menubar(SvgNode* n);
  void addButton(Button* btn);
  Button* addAction(Action* action);

  bool autoClose = false;
};

Menubar* createMenubar();
Menubar* createVertMenubar();

class AutoAdjContainer : public Widget
{
public:
  AutoAdjContainer(SvgNode* n, Widget* _contents);
  void repeatLayout(const Rect& dest);

  std::function<void(const Rect&, const Rect&)> adjFn;
  Widget* contents;
  Rect contentsBBox;

//private:
//  int numAbsPos = 0;
};

// tooltip for widget with long press/right click action
#define altTooltip(s1, s2) fstring("<text>%s\n<tspan y='14' class='alttext'>%s</tspan></text>", s1, s2).c_str()

#endif
