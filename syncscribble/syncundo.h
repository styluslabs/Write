#ifndef SYNCUNDO_H
#define SYNCUNDO_H

#include "ulib/fileutil.h"
#include "page.h"

#define UNDO_ITEM_METHODS \
  void undo() override; \
  void redo() override; \
  void serialize(IOStream& strm) override; \
  UndoHistoryItem* inverse() override;

// for add/remove items that may need to delete objects
#define ADDRM_UNDO_ITEM_METHODS  UNDO_ITEM_METHODS  void discard(bool undone) override;


// Little point to combining the TargetLayerItem classes into a single class and using switch/case
//  since undo, redo, discard still need to be virtual.  (I've flip-flopped on this a few times)
// commit() was added so that dirtyCount isn't changed unless and until undo item is actually added
//  to history

class UndoHistoryItem {
public:
  virtual ~UndoHistoryItem() {}

  virtual void commit() {}
  virtual void undo() {}
  virtual void redo() {}
  virtual void discard(bool undone) {}
  virtual void serialize(IOStream& strm) {}
  virtual UndoHistoryItem* inverse() { return NULL; }

  static constexpr unsigned int HEADER                 = 0x00000001;
  static constexpr unsigned int STROKE_ITEM            = 0x00000002;
  static constexpr unsigned int STROKE_ADDED_ITEM      = 0x00010002;
  static constexpr unsigned int STROKE_DELETED_ITEM    = 0x00020002;
  static constexpr unsigned int STROKE_TRANSFORM_ITEM  = 0x00030002;
  static constexpr unsigned int STROKE_TRANSLATE_ITEM  = 0x00040002;
  static constexpr unsigned int STROKE_CHANGE_ITEM     = 0x00050002;
  static constexpr unsigned int STROKES_ITEM           = 0x00000004;
  static constexpr unsigned int PAGE_ITEM              = 0x00000008;
  static constexpr unsigned int PAGE_CHANGED_ITEM      = 0x00010008;
  static constexpr unsigned int DOCUMENT_ITEM          = 0x00000010;
  static constexpr unsigned int PAGE_ADDED_ITEM        = 0x00010010;
  static constexpr unsigned int PAGE_DELETED_ITEM      = 0x00020010;
  static constexpr unsigned int DISABLED_ITEM          = 0x00008000;
  virtual unsigned int type() const { return 0; }

  static constexpr int SAVED_STATE_DISCARDED = INT_MAX/2;

  bool isA(unsigned int t) const { return (type() & t) == t; }
};

class UndoGroupHeader : public UndoHistoryItem {
public:
  int pageNum;
  UndoGroupHeader(int pagenum) : pageNum(pagenum), uuid(0) {}
  // in the future, this could include name of action to be undone
  unsigned int type() const override { return HEADER; }

  UUID_t uuid;
};

class DisabledUndoItem : public UndoHistoryItem {
public:
  unsigned int type() const override { return DISABLED_ITEM; }
};

// Stroke level undo items

class StrokeUndoItem : public UndoHistoryItem {
  friend class UndoHistory;
protected:
  Element* s;
  Page* page;
public:
  void commit() override;
  void undo() override;
  void redo() override;
  void discard(bool undone) override;
  unsigned int type() const override { return STROKE_ITEM; }
  StrokeUndoItem(Element* s_, Page* p_);
  Element* getStroke() const { return s; }
  Page* getPage() const { return page; }
};

// next is the stroke immediately preceding this one in the stroke list (i.e. in the z-order)
class StrokeAddedItem : public StrokeUndoItem {
protected:
  Element* next;
public:
  StrokeAddedItem(Element* s_, Page* p_, Element* _next) : StrokeUndoItem(s_, p_), next(_next) {}
  ADDRM_UNDO_ITEM_METHODS
  unsigned int type() const override { return STROKE_ADDED_ITEM; }
};

class StrokeDeletedItem : public StrokeUndoItem {
protected:
  Element* next;
public:
  StrokeDeletedItem(Element* s_, Page* p_, Element* _next) : StrokeUndoItem(s_, p_), next(_next) {}
  ADDRM_UNDO_ITEM_METHODS
  unsigned int type() const override { return STROKE_DELETED_ITEM; }
};

class StrokeChangedItem : public StrokeUndoItem {
private:
  StrokeProperties props;
  void swapProps();
public:
  StrokeChangedItem(Element* s_, Page* p_) : StrokeUndoItem(s_, p_), props(s_->getProperties()) {}
  StrokeChangedItem(Element* s_, Page* p_, const StrokeProperties& props_)
      : StrokeUndoItem(s_, p_), props(props_) {}
  UNDO_ITEM_METHODS
  unsigned int type() const override { return STROKE_CHANGE_ITEM; }
};

class StrokeTranslateItem : public StrokeUndoItem {
private:
  Dim xoffset;
  Dim yoffset;
public:
  //StrokeTranslateItem(Element* s_);
  StrokeTranslateItem(Element* s_, Page* p_, Dim xoffset_, Dim yoffset_);
  UNDO_ITEM_METHODS
  unsigned int type() const override { return STROKE_TRANSLATE_ITEM; }
};

class StrokeTransformItem : public StrokeUndoItem {
private:
  ScribbleTransform transform;
public:
  StrokeTransformItem(Element* s_, Page* p_, const ScribbleTransform& tf) : StrokeUndoItem(s_, p_), transform(tf) {}
  UNDO_ITEM_METHODS
  unsigned int type() const override { return STROKE_TRANSFORM_ITEM; }
};

// Page level undo items

class PageChangedItem : public UndoHistoryItem {
public:
  Page* p;
  PageProperties props;
  void swapProps();
//public:
  PageChangedItem(Page* p_); //, bool saveruling = false);
  PageChangedItem(Page* p_, const PageProperties& props_) : p(p_), props(props_) {}
  void commit() override;
  ADDRM_UNDO_ITEM_METHODS
  unsigned int type() const override { return PAGE_CHANGED_ITEM; }
};

// Document level undo items

class DocumentUndoItem : public UndoHistoryItem {
public:
  Page* p;
  int pagenum;
  Document* document;
//public:
  void commit() override;
  void undo() override;
  void redo() override;
  void discard(bool undone) override;
  DocumentUndoItem(Page* p_, int pagenum_, Document* document_);
  unsigned int type() const override { return DOCUMENT_ITEM; }
};

class PageAddedItem : public DocumentUndoItem {
public:
  PageAddedItem(Page* p_, int pagenum_, Document* document_)
      : DocumentUndoItem(p_, pagenum_, document_) {}
  ADDRM_UNDO_ITEM_METHODS
  unsigned int type() const override { return PAGE_ADDED_ITEM; }
};

class PageDeletedItem : public DocumentUndoItem {
public:
  PageDeletedItem(Page* p_, int pagenum_, Document* document_)
      : DocumentUndoItem(p_, pagenum_, document_) {}
  ADDRM_UNDO_ITEM_METHODS
  unsigned int type() const override { return PAGE_DELETED_ITEM; }
};

// undo list can easily get very big ... if we just limit to fixed number of items, we would have
//  to set that number unnecessarily low ... maybe instead we could limit size ... we'd need layers
//  and strokes to keep track of their sizes


// would be nice to have some limited branching of undo history, but we'd need a simple interface
// ... full-blown tree view is way too complicated (http://e-texteditor.com/blog/2006/making-undo-usable)
class UndoHistory {
  friend class ScribbleSync;
  friend class ScribbleArea;  // used for recent stroke select ... we should instead provide methods

public:
  UndoHistory();
  ~UndoHistory();

  void startAction(int pagenum);
  void endAction();
  void addItem(UndoHistoryItem* undoItem);
  // single step through history
  // fancier version would take arg for number of items to undo or redo.  We'd need a method to
  //  return types for last N undo items to display in MS Word style undo dropdown
  int undo();
  int redo();
  bool canUndo() const;
  bool canRedo() const;
  bool undoable() const;
  size_t histPos() const { return pos; }
  static UUID_t newUuid();

  enum { MULTIPAGE = 0x40000000 };  // flag to OR with pagenum to indicate multiple pages are dirtied

private:
  std::vector<UndoHistoryItem*> hist;
  size_t pos;
  unsigned int inAction;
  int actionPageNum;
  void clearUndone(bool undone = true);
};

#endif // UNDO_H
