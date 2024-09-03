#pragma once

#include "scribblearea.h"

class Widget;

class ClippingView : public ScribbleArea
{
public:
  void loadConfig(ScribbleConfig* _cfg) override;
  bool selectionDropped(Selection* selection, Point pos, Point offset, bool replaceids = false) override;
  void deleteClipping();

  Widget* delTarget;

protected:
  void doPressEvent(const InputEvent& event) override;
  void doMoveEvent(const InputEvent& event) override;
  void doReleaseEvent(const InputEvent& event) override;
  bool doClickAction(Point pos) override;
  void doCancelAction(bool refresh = true) override;
  void pageSizeChanged() override;

  int clipNum;
  Point globalPos;
  Point selOffset;
};
