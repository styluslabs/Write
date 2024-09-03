#pragma once

#include "scribbleview.h"
#include "document.h"
#include "selection.h"


struct UIState {
  enum UIChangeFlags { SetDoc = 0, SaveDoc, InsertDoc, InsertPage, DeletePage, MovePage, SetPageProps,
      Command, ReleaseEvent, PenRelease, CancelEvent, SyncEvent, Zoom, Pan, PageSizeChange, PageNumChange,
      SelChange, SetSelProps, Paste, ClipboardChange };

  bool activeSel;
  bool pageSel;
  bool selHasGroup;
  bool pagemodified;
  bool nextView;
  bool prevView;
  bool syncActive;
  bool rxOnly;  // true if SWB active and enableTX false
  int pageNum;
  int totalPages;
  int currPageStrokes;
  int currSelStrokes;
  Dim pageWidth;
  Dim pageHeight;
  Dim zoom;
  Timestamp minTimestamp;
  Timestamp maxTimestamp;
};

class ScribbleDoc;
class ScribbleApp;
struct SDL_Cursor;
struct SDL_Cursor_Deleter;

class ScribbleArea : public ScribbleView
{
#ifdef SCRIBBLE_TEST
  friend class ScribbleTest;
#endif
  friend class LinkDialog;
  friend class ScribbleDoc;
  friend class ClippingView;
public:
  ScribbleArea();
  //~ScribbleArea();

  void loadConfig(ScribbleConfig* _cfg) override;
  Page* getCurrPage() const { return currPage; }
  void createHyperRef(Element* b, const StrokeProperties* props = NULL) { setSelProperties(props, NULL, b); }
  void createHyperRef(const char* target, const StrokeProperties* props = NULL) { setSelProperties(props, target); }
  const char* getHyperRef() const;
  const char* getIdStr() const;
  void setSelProperties(const StrokeProperties* props, const char* target = NULL,
      Element* bkmktarget = NULL, const char* idstr = NULL, bool forcenormal = false);
  void insertImage(Image image); //, bool lossy = false);

  // some of these need to be made private
  DocPosition getPos() const;
  void doGotoPos(int pagenum, Point pos, bool exact = true);
  void doCommand(int itemid);
  bool doTimerEvent(Timestamp t) override;
  void reset();

  bool recentStrokeSelect();
  bool recentStrokeDeselect();
  void recentStrokeSelDone();
  void zoomCenter(Dim newZoom, bool snap = false);

  void setViewBox(const DocViewBox &vb);
  DocViewBox getViewBox() const;

  enum viewmode_t {VIEWMODE_SINGLE=0, VIEWMODE_VERT=1, VIEWMODE_HORZ=2};
  enum PasteFlags {PasteOrigPos=0, PasteCenter=2, PasteCorner=4, PasteOrigin=8, PasteOffsetExisting=0x10,
      PasteRulingShift=0x20, PasteUndoable=0x40, PasteMoveClipboard=0x80, PasteNoHandles=0x100};

  static Image* watermark;  // only for iOS IAP
  // made public to support multi-doc split
  ScribbleDoc* scribbleDoc = NULL;
  ScribbleApp* app = NULL;
  int strokeCounter = 0;
  void updateUIState(UIState* state);
  bool hasSelection() const { return currSelection != NULL; }
  void setStrokeProperties(const StrokeProperties& props, bool undoable = true);
  ScribblePen getPenForSelection() const;
  // accept external selection
  virtual bool selectionDropped(Selection* clip, Point globalPos, Point offset, bool replaceids = false);
  // for file dropped from OS
  bool clipboardDropped(Clipboard* selection, Point globalPos, Point offset);

protected:
  void doPressEvent(const InputEvent& event) override;
  void doMoveEvent(const InputEvent& event) override;
  void doReleaseEvent(const InputEvent& event) override;
  bool doClickAction(Point pos) override;
  void doDblClickAction(Point pos) override;
  void doMotionEvent(const InputEvent& event, inputevent_t eventtype) override;
  void doCancelAction(bool refresh = true) override;

  Page* page(int n) const;
  int numPages() const;
  const ScribblePen* currPen() const;

  void setPageNum(int pagenum);
  void nextPage(bool appendnew = false);
  void prevPage();
  void selectAll(Selection::StrokeDrawType drawtype = Selection::STROKEDRAW_SEL);
  void selectSimilar();
  void invertSelection();
  void deleteSelection();
  void ungroupSelection();
  void doPaste(Clipboard* clipboard, const Page* srcpage, int flags = 0);
  void doPasteAt(Clipboard* clipboard, Point pos, PasteFlags flags);
  void expandDown();
  void expandRight();
  void prevView();
  void nextView();
  void viewPos(int pagenum, Point pos);
  void gotoPos(int pagenum, Point pos, bool savepos = true);
  void gotoPage(int pagenum);
  void viewRect(int pagenum, const Rect& r);
  bool viewHref(const char* href);

  bool uiDirty;
  void uiChanged(int reason);
  void roundZoom(Dim px, Dim py) override;
  void doPan(Dim dx, Dim dy) override;
  void doRefresh() override;
  void pageSizeChanged() override;
  void pageCountChanged(int pagenum, int prevpages = -1);

  void invalidateStroke(Element* s);
  void invalidatePage(Page* p);
  void setPageDims(Dim width, Dim height, bool pending = false);
  void growPage(Rect bbox, Dim maxdx = -1, Dim maxdy = -1, bool pending = false);
  void reqRepaint();
  void drawStrokeOnImage(Element* stroke);
  bool clearSelection();
  bool clearTempSelection();
  void groupStrokes(Element* b = NULL);
  int selectionHit(Point pos, bool touch);

  void viewSelection();
  void freeErase(Point prevpos, Point pos);
  bool saveCurrPos(int newpagenum, Point newpos);

  Point getPageOrigin(int pagenum) const;
  int dimToPageNum(const Point& pos) const;
  Point dimToPageDim(const Point& p) const;
  Rect dimToPageDim(Rect r) const;
  Point pageDimToDim(const Point& p) const;
  Rect pageDimToDim(Rect r) const;
  bool isPageVisible(int pagenum);
  void dirtyScreen(const Rect &dirty);
  void dirtyPage(int pagenum, Rect dirty);

  void updateContentDim();
  void drawThumbnail(Image* dest);
  void drawWatermark(Painter* painter, Page* page, const Rect& dirty);  // for iOS IAP
  void drawImage(Painter* imgpaint, const Rect& dirty) override;
  void drawScreen(Painter* painter, const Rect& dirty) override;

  int currMode;
  Point prevPos;
  Point initialPos;
  Point rawPos;
  Point prevRawPos;
  Point releasePos = { NAN, NAN };
  int prevLine;
  int initialLine;
  Rect initialPageSize;
  Dim prevXScale;
  Dim prevYScale;
  bool scaleLockRatio;
  Timestamp prevHoverDrawTime;
  Rect hoverRect;
  int cursorMode = 0;  // CURSORMODE_SYSTEM
  int drawCursor = 0;
  Dim lineDrawPressure = 1;
  bool showHelpTips = false;

  // for erase ruled
  int eraseCurrLine;
  Dim eraseXmax;
  Dim eraseXmin;
  // for insert space
  bool insertSpaceX;
  // for resize selection
  Point scaleOrigin;
  Dim bookmarkSnapX;

  viewmode_t viewMode;
  bool centerPages;
  Dim reflowWordSep;
  RuledSelector::ColMode selColMode = RuledSelector::COL_NORMAL;
  Dim pageSpacing;
  Dim currPageXOrigin = 0;
  Dim currPageYOrigin = 0;
  Dim contentHeight = 0;
  Dim contentWidth = 0;

  // we'll only display one page at a time for now (like OneNote)
  int currPageNum = INT_MAX;
  Page* currPage = NULL;
  Selection* tempSelection = NULL;
  Selection* currSelection = NULL;
  // selection for stroke fragments produced by free eraser
  Selection* freeErasePieces = NULL;
  int currSelPageNum = 0;
  Rect selBGRect;

  PathSelector* pathSelector = NULL;
  RuledSelector* ruledSelector = NULL;
  RectSelector* rectSelector = NULL;
  LassoSelector* lassoSelector = NULL;
  RuledSelector* insSpaceEraseSelector = NULL;
  Selection* insSpaceEraseSelection = NULL;
  Element* currStroke = NULL;

  // for groupStrokes
  std::vector<Element*> recentStrokes;
  Dim strokeGroupYCenter = 0;
  typedef std::vector<Element*>::iterator RecentStrokesIter;

  // for back/fwd navigation
  std::vector<DocPosition> posHistory;
  std::vector<DocPosition>::iterator posHistoryPos;

  // experimental feature to select N most recent strokes
  int recentStrokeSelPos = -1;
#if !PLATFORM_MOBILE
  static bool staticInited;
  static std::unique_ptr<SDL_Cursor, SDL_Cursor_Deleter> penCursor;
  static std::unique_ptr<SDL_Cursor, SDL_Cursor_Deleter> panCursor;
  static std::unique_ptr<SDL_Cursor, SDL_Cursor_Deleter> eraseCursor;
#endif
  // some constants
  static const Dim ERASESTROKE_RADIUS;
  static const Dim ERASEFREE_RADIUS;
  static const Dim PATHSELECT_RADIUS;
  static const Dim MIN_LASSO_POINT_DIST;
  static const Dim GROW_STEP;
  static const Dim GROW_TRIGGER;  // in multiples of GROW_STEP or ruling
  static const Dim GROW_EXTRA;  // in multiples of GROW_STEP or ruling
  static const Dim AUTOSCROLL_BORDER;
  static const Dim MIN_CURSOR_RADIUS;
  static const Color BACKGROUND_COLOR;
};
