#pragma once

#include "ugui/widgets.h"
#include "bookmarkview.h"
#include "touchwidgets.h"

class ScribbleDoc;
class BookmarkSelect;
class TextEdit;

class LinkDialog : public Dialog
{
public:
  LinkDialog(ScribbleDoc* sd);
  void bookmarkSelected(Element* b);
  void accept();

private:
  ColorEditBox* penColorPicker;
  ScribbleDoc* scribbleDoc;
  TextEdit* hrefEdit;
  std::unique_ptr<BookmarkSelect> bookmarkSelect;
};

class BookmarkSelect : public BookmarkView
{
public:
  BookmarkSelect(LinkDialog* parent, ScribbleConfig* _cfg, ScribbleDoc* target);
  ~BookmarkSelect();

protected:
  bool doClickAction(Point pos) override;
  void doDblClickAction(Point pos) override;

  LinkDialog* linkDialog;
};
