#ifndef SCRIBBLEDOC_H
#define SCRIBBLEDOC_H

#include "scribblearea.h"

class ScribbleApp;
class ScribbleMode;
class ScribbleSync;
class StrokeBuilder;

class ScribbleDoc
{
#ifdef SCRIBBLE_TEST
  friend class ScribbleTest;
#endif
  friend class UndoHistory; // for syncing; hopefully only temporary
  friend class LinkDialog;
  friend class ScribbleArea;
public:
  ScribbleDoc(ScribbleApp* parent, ScribbleConfig* _cfg, ScribbleMode* _mode);
  ~ScribbleDoc();

  void addArea(ScribbleArea* area);
  void removeArea(ScribbleArea* area);
  void loadConfig(bool refresh);
  //void doTimerEvent(Timestamp t);
  void newDocument();
  Document::loadresult_t openDocument(const char* filename, bool delayload = true);
  Document::loadresult_t openDocument(IOStream* strm, bool delayload = true);
  bool saveDocument(const char* filename, Document::saveflags_t flags = Document::SAVE_NORMAL);
  bool saveDocument(IOStream* strm = NULL, Document::saveflags_t flags = Document::SAVE_NORMAL);
  void resetDocPrefs();
  void openSharedDoc(const char* server, const pugi::xml_node& xml, bool master);
  bool checkAndClearErrors(bool forceload = false);

  bool isModified() const { return document->isModified(); }
  bool canRedo() const;
  bool canUndo() const;

  void doCommand(int itemid);
  bool doesClipStrokes(const PageProperties* props, bool applytoall);
  bool setPageProperties(const PageProperties* props, bool applytoall, bool docdefault, bool global, bool undoable = true);
  void openURL(const char* url);
  void bookmarkHit(int pagenum, Element* bookmark);

  // a lot of stuff needs to be moved up from ScribbleDoc to application level
  int getScribbleMode(int modemod);
  int getActiveMode() const;

  Color getCurrPageColor() const;
  void scribbleDone();
  void pageSizeChanged();
  void pageCountChanged(int pagenum, int prevpages = -1);
  void startAction(int pagenum);
  void endAction();
  void clearSelection();
  void deleteSelection();
  Rect dirtyPage(int pagenum);
  void updateCurrStroke(Rect dirty);

  // some support fns for whiteboarding
  void strokesUpdated(const std::vector<Element*>& strokes);
  void invalidateStroke(Element* s) { for(ScribbleArea* view : views) view->invalidateStroke(s); }
  void invalidatePage(Page* p) { for(ScribbleArea* view : views) view->invalidatePage(p); }

  static bool deleteDocument(const char* filename);
  static Image extractThumbnail(const char* filename);
  Document::loadresult_t insertDocument(IOStream* strm);
  const char* fileName() const { return document->fileName(); }

//private:
  Page* generatePage(int where) const;
  void updateGhostPage();
  void newPage(int where = INT_MAX);
  void insertPage(Page* page, int where = INT_MAX);
  void deletePages();
  void deletePage(int where);
  void pastePages(Clipboard* clipboard, int where);
  void movePage(int oldpagenum, int newpagenum);
  enum SelectPagesFlags { PAGESEL_INV=INT_MAX-1, PAGESEL_ALL=INT_MAX };
  void selectPages(int pagenum);
  void exitPageSelMode();

  void uiChanged(int reason);
  void repaintAll();
  void doRefresh();

  void setDefaultDims(ScribbleConfig* c, const PageProperties* props);
  void doUndoRedo(bool redo = false);
  void undoRedoUpdate(int pagenum, int prevpages, bool viewdirty = true);
  void closeDocument();
  void doCancelAction();
  void updateDocConfig(Document::saveflags_t flags);

  ScribbleApp* app;
  ScribbleMode* scribbleMode;
  ScribbleConfig* globalCfg;
  ScribbleConfig* cfg;
  ScribbleArea* activeArea = NULL;
  ScribbleSync* scribbleSync = NULL;
  unsigned int nViews = 0;
  int uiDirty = 0;
  int strokeCounter;
  int numSelPages = 0;
  int prevSelPageNum = 0;

  std::vector<ScribbleArea*> views;
  Document* document = NULL;
  UndoHistory* history = NULL;
  std::unique_ptr<Page> ghostPage;
  Timestamp fileLastMod = 0;
  bool autoSaveReq = false;

  StrokeBuilder* strokeBuilder = NULL;
};

#endif

