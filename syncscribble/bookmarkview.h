#ifndef BOOKMARKVIEW_H
#define BOOKMARKVIEW_H

#include "scribbleview.h"

class ScribbleDoc;
//class Element;
#include "page.h"

class BookmarkView : public ScribbleView
{
  friend class ScribbleTest;
public:
  enum Mode {BOOKMARKS=0, MARGIN_CONTENT=1};

  BookmarkView(ScribbleConfig* _cfg, ScribbleDoc* doc);
  void setScribbleDoc(ScribbleDoc* doc);
  void repaintBookmarks();
  Element* bookmarkHit = NULL;

  void loadConfig(ScribbleConfig* _cfg) override;
  void unHighlightHit();

protected:
  void doPressEvent(const InputEvent& event) override;
  void doMoveEvent(const InputEvent& event) override;
  void doReleaseEvent(const InputEvent& event) override;
  bool doClickAction(Point pos) override;
  void doCancelAction(bool refresh = true) override;
  void doMotionEvent(const InputEvent& event, inputevent_t eventtype) override;
  void doRefresh() override;

  void pageSizeChanged() override;
  void drawImage(Painter* imgpaint, const Rect& dirty) override;
  void getContentDim(Dim viewwidth, Dim viewheight);

  void highlightHit(Point pos);
  void drawBookmarks(Painter* painter, Document* doc, const Rect& dirty);
  void drawPageBookmarks(Painter* painter, Page* page, Dim ypos, Dim xmin, int nbkmks);
  Element* findBookmark(Document* doc, Dim bookmarky, int* pagenumout = NULL);

  ScribbleDoc* scribbleDoc = NULL;
  Rect hitDirtyRect;
  Dim bookmarksHeight = 0;
};

#endif // BOOKMARKVIEW_H
