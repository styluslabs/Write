#pragma once

#include "basics.h"
#include "ugui/svggui.h"

class TextBox;
class Button;
class ScribbleView;

class ScribbleWidget : public Widget
{
public:
  ScribbleWidget(ScribbleView* sv);
  void setScroller(Widget* s, Widget* scrollind);

  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;
  Rect dirtyRect() const override;

  void startTimer(Dim periodMs);
  void setScrollPosition(Dim pos, Dim vfrac);
  void showScroller();

  ScribbleView* scribbleView;
  Widget* scroller = NULL;
  Widget* scrollIndicator = NULL;
  Timer* scrollerFadeTimer = NULL;
  Dim prevScrollerY = 0;

  TextBox* pageNumLabel = NULL;
  TextBox* fileNameLabel = NULL;
  TextBox* timeRangeLabel = NULL;
  TextBox* zoomLabel = NULL;
  Widget* focusIndicator = NULL;
  Button* nextPage = NULL;
  Button* prevPage = NULL;

  // needed to access private members of ScribbleView
  static ScribbleWidget* create(Widget* container, ScribbleView* area);
};

class Selection;
class ScribbleArea;

class OverlayWidget : public Widget
{
public:
  OverlayWidget(Widget* under);
  void drawSelection(Selection* sel, Point gpos = Point(), Point dr = Point(), Dim s = 1);
  bool canDrop(Point gpos) const { return getScribbleArea(gpos) != NULL; }
  bool dropSelection(Selection* clip, Point gpos, Point dr, bool replaceids = false);

  Rect bounds(SvgPainter* svgp) const override;
  void draw(SvgPainter* svgp) const override;
  Rect dirtyRect() const override { return currSelBounds.united(renderedSelBounds); }

private:
  ScribbleArea* getScribbleArea(Point gpos) const;

  Widget* underWidget;
  Selection* selection;
  Point globalPos;
  Point offset;
  Dim scale;
  Rect m_bounds;
  Rect m_dirty;
  Rect currSelBounds;
  mutable Rect renderedSelBounds;
};
