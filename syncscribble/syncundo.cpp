#include "syncundo.h"
#include "document.h"
#include "basics.h"

StrokeUndoItem::StrokeUndoItem(Element* s_, Page* p_) : s(s_), page(p_) {}

void StrokeUndoItem::commit()
{
  page->dirtyCount++;
}

void StrokeUndoItem::undo()
{
  page->dirtyCount--;
}

void StrokeUndoItem::redo()
{
  page->dirtyCount++;
}

// if user saves doc, does undo, then makes a change discarding the history which includes saved state, it is
//  impossible to return to saved state, so page is irrevocably dirtied so we set dirtyCount to extreme value
void StrokeUndoItem::discard(bool undone)
{
  if(undone && page->dirtyCount < 0)  // if !undone, page may not exist
    page->dirtyCount = SAVED_STATE_DISCARDED;
}

// StrokeAddedItem
void StrokeAddedItem::undo()
{
  StrokeUndoItem::undo();
  page->onRemoveStroke(s);
  page->contentNode->removeChild(s->node);
}

void StrokeAddedItem::redo()
{
  page->contentNode->addChild(s->node, next ? next->node : NULL);
  page->onAddStroke(s);
  StrokeUndoItem::redo();
}

void StrokeAddedItem::discard(bool undone)
{
  StrokeUndoItem::discard(undone);
  if(undone)
    s->deleteNode();
}

void StrokeDeletedItem::undo()
{
  page->contentNode->addChild(s->node, next ? next->node : NULL);
  page->onAddStroke(s);
  StrokeUndoItem::undo();
}

void StrokeDeletedItem::redo()
{
  StrokeUndoItem::redo();
  page->onRemoveStroke(s);
  page->contentNode->removeChild(s->node);
}

void StrokeDeletedItem::discard(bool undone)
{
  StrokeUndoItem::discard(undone);
  if(!undone)
    s->deleteNode();
}

// StrokeChangedItem - record change of stroke properties (color, width)
void StrokeChangedItem::swapProps()
{
  StrokeProperties temp = s->getProperties();
  s->setProperties(props);
  props = temp;
}

void StrokeChangedItem::undo()
{
  swapProps();
  StrokeUndoItem::undo();
}

void StrokeChangedItem::redo()
{
  swapProps();
  StrokeUndoItem::redo();
}

// StrokeTransformItem is passed Stroke prior to commit; used to efficiently save undo info from reflow
//StrokeTranslateItem::StrokeTranslateItem(Element* s_)
//    : StrokeUndoItem(s_), xoffset(s_->xOffset()), yoffset(s_->yOffset()) {}

StrokeTranslateItem::StrokeTranslateItem(Element* s_, Page* p_, Dim xoffset_, Dim yoffset_)
    : StrokeUndoItem(s_, p_), xoffset(xoffset_), yoffset(yoffset_) {}

void StrokeTranslateItem::undo()
{
  s->applyTransform(Transform2D::translating(-xoffset, -yoffset));
  s->commitTransform();
  StrokeUndoItem::undo();
}

void StrokeTranslateItem::redo()
{
  s->applyTransform(Transform2D::translating(xoffset, yoffset));
  s->commitTransform();
  StrokeUndoItem::redo();
}

// StrokeTransformItem

void StrokeTransformItem::undo()
{
  s->applyTransform(transform.inverse());
  s->commitTransform();
  StrokeUndoItem::undo();
}

void StrokeTransformItem::redo()
{
  s->applyTransform(transform);
  s->commitTransform();
  StrokeUndoItem::redo();
}

// Page level items

PageChangedItem::PageChangedItem(Page* p_) : p(p_), props(p_->getProperties()) {}

void PageChangedItem::discard(bool undone) {}

void PageChangedItem::commit()
{
  p->dirtyCount++;
}

void PageChangedItem::swapProps()
{
  // save current properties
  PageProperties temp = p->getProperties();
  // replace current properties with new properties
  p->setProperties(&props);
  // now store replaced properties (except ruleLayer, if it is unchanged)
  props = temp;
}

void PageChangedItem::undo()
{
  swapProps();
  p->dirtyCount--;
}

void PageChangedItem::redo()
{
  swapProps();
  p->dirtyCount++;
}

DocumentUndoItem::DocumentUndoItem(Page* p_, int pagenum_, Document* document_)
    : p(p_), pagenum(pagenum_), document(document_) {}

void DocumentUndoItem::commit()
{
  document->dirtyCount++;
}

void DocumentUndoItem::undo()
{
  document->dirtyCount--;
}

void DocumentUndoItem::redo()
{
  document->dirtyCount++;
}

void DocumentUndoItem::discard(bool undone)
{
  if(undone && document->dirtyCount < 0)
    document->dirtyCount = SAVED_STATE_DISCARDED;
}

void PageDeletedItem::undo()
{
  document->insertPage(p, pagenum);
  DocumentUndoItem::undo();
}

void PageDeletedItem::redo()
{
  DocumentUndoItem::redo();
  document->deletePage(pagenum);
}

void PageDeletedItem::discard(bool undone)
{
  DocumentUndoItem::discard(undone);
  if(!undone)
    delete p;
}

void PageAddedItem::undo()
{
  DocumentUndoItem::undo();
  document->deletePage(pagenum);
}

void PageAddedItem::redo()
{
  document->insertPage(p, pagenum);
  DocumentUndoItem::redo();
}

void PageAddedItem::discard(bool undone)
{
  DocumentUndoItem::discard(undone);
  if(undone)
    delete p;
}

// UndoHistory class

UndoHistory::UndoHistory() : pos(0), inAction(0) {}

UndoHistory::~UndoHistory()
{
  // clear the strokes that are actually undone, then the rest
  clearUndone();
  pos = 0;
  clearUndone(false);
}

void UndoHistory::startAction(int pagenum)
{
  if(inAction)
    SCRIBBLE_LOG("UndoHistory::startAction called while already in action!");
  inAction = 1;
  actionPageNum = pagenum;
}

void UndoHistory::endAction()
{
  inAction = 0;
}

void UndoHistory::addItem(UndoHistoryItem* undoItem)
{
  if(inAction) {
    if(inAction == 1) {
      if(pos < hist.size())
        clearUndone();
      hist.push_back(new UndoGroupHeader(actionPageNum));
    }
    undoItem->commit();
    hist.push_back(undoItem);
    pos = hist.size();
    inAction++;
  }
  else {
    undoItem->discard(false);
    delete undoItem;
    // not necessarily a bug, but I want to understand when this can happen
    SCRIBBLE_LOG("Undo item outside of Action\n");
  }
}

void UndoHistory::clearUndone(bool undone)
{
  // must discard newer entries before older ones
  while(hist.size() > pos) {  //unsigned int ii = pos; ii < hist.size(); ii++) {
    hist.back()->discard(undone);
    if(!hist.back()->isA(UndoHistoryItem::DISABLED_ITEM))
      delete hist.back();
    hist.pop_back();
  }
}

int UndoHistory::undo()
{
  while(pos > 0) {
    pos--;
    if(hist[pos]->isA(UndoHistoryItem::HEADER))
      break;
    hist[pos]->undo();
  }
  return ((UndoGroupHeader*) hist[pos])->pageNum;
}

int UndoHistory::redo()
{
  int pagenum = -1;
  // skip header
  if(pos < hist.size())
    pagenum = ((UndoGroupHeader*) hist[pos++])->pageNum;
  while(pos < hist.size() && !hist[pos]->isA(UndoHistoryItem::HEADER))
    hist[pos++]->redo();
  return pagenum;
}

bool UndoHistory::canUndo() const
{
  return !hist.empty() && pos > 0;
}

bool UndoHistory::canRedo() const
{
  return pos < hist.size();
}

// Note that this is provided for optimization purposes - items added to history when inAction == 0
//  are discarded.  Checking this flag allows us to avoid unnecessary creation of such items.
// We expect inAction to be 0 when in the process of undoing or redoing

bool UndoHistory::undoable() const
{
  return (inAction != 0);
}

// serialization and inversion - needed for shared whiteboarding

// for now, we are going to assign uuid to undo item; in the future, server may do this
// RNG is seeded with time in MainWindow ctor - this assumes we're on the same thread!
UUID_t UndoHistory::newUuid()
{
  return (UUID_t(randpp()) << 32) + UUID_t(randpp());
}

void StrokeAddedItem::serialize(IOStream& strm)
{
  s->uuid = UndoHistory::newUuid();
  strm << fstring("<addstroke strokeuuid='%llu' pagenum='%d' nextstrokeuuid='%llu'>",
     s->uuid, page->getPageNum(), next ? next->uuid : 0);
  XmlStreamWriter xmlwriter;
#if IS_DEBUG
  xmlwriter.defaultFloatPrecision = 6;  // send extra digits to eliminate test diffs for transformed strokes
#endif
  SvgWriter(xmlwriter).serialize(s->node);
  xmlwriter.save(strm);
  strm << "</addstroke>";
}

void StrokeDeletedItem::serialize(IOStream& strm)
{
  strm << fstring("<delstroke strokeuuid='%llu'/>", s->uuid);
}

void StrokeTransformItem::serialize(IOStream& strm)
{
  Dim* m = transform.asArray();
  strm << fstring("<transform strokeuuid='%llu' internalscale='%f %f'",
      s->uuid, transform.internalScale[0], transform.internalScale[1]);
  // maintain legacy 3x3 format for now
  strm << fstring(" matrix='%f %f %f %f %f %f %f %f %f'/>", m[0], m[1], 0.0, m[2], m[3], 0.0, m[4], m[5], 1.0);
}

void StrokeTranslateItem::serialize(IOStream& strm)
{
  strm << fstring("<translate strokeuuid='%llu' x='%f' y='%f'/>", s->uuid, xoffset, yoffset);
}

void StrokeChangedItem::serialize(IOStream& strm)
{
  StrokeProperties currprops = s->getProperties();
  strm << fstring("<strokechanged strokeuuid='%llu' color='%u' width='%f'/>",
      s->uuid, currprops.color.argb(), currprops.width);
}

void PageChangedItem::serialize(IOStream& strm)
{
  // lots of parameters - just use fstring
  strm << fstring("<pagechanged pagenum='%d' width='%.3f' height='%.3f' color='%u' xruling='%.3f'"
      " yruling='%.3f' marginLeft='%.3f' rulecolor='%u'>", p->getPageNum(), p->props.width, p->props.height,
      p->props.color.argb(), p->props.xRuling, p->props.yRuling, p->props.marginLeft, p->props.ruleColor.argb());
  //if(props.ruleLayer)
  //  props.ruleLayer->saveSVG(strm);
  strm << "</pagechanged>";
}

void PageDeletedItem::serialize(IOStream& strm)
{
  strm << fstring("<delpage pagenum='%d'/>", pagenum);
}

void PageAddedItem::serialize(IOStream& strm)
{
  UUID_t firstuuid = p->strokeCount() > 0 ? UndoHistory::newUuid() : 0;
  strm << fstring("<addpage pagenum='%d' keepcontents='1' firstuuid='%llu'>", pagenum, firstuuid);
  for(Element* s : p->children())
    s->uuid = firstuuid++;
  p->saveSVG(strm, 0, 0);  //, true);
  strm << "</addpage>";
}

// to sync local undo, we need to be able to get the inverse of an item, which we call serialize() for
UndoHistoryItem* StrokeAddedItem::inverse()
{
  return new StrokeDeletedItem(s, page, next);
}

UndoHistoryItem* StrokeDeletedItem::inverse()
{
  return new StrokeAddedItem(s, page, next);
}

UndoHistoryItem* StrokeTranslateItem::inverse()
{
  return new StrokeTranslateItem(s, page, -xoffset, -yoffset);
}

UndoHistoryItem* StrokeTransformItem::inverse()
{
  return new StrokeTransformItem(s, page, transform.inverse());
}

UndoHistoryItem* PageAddedItem::inverse()
{
  return new PageDeletedItem(p, pagenum, document);
}

UndoHistoryItem* PageDeletedItem::inverse()
{
  return new PageAddedItem(p, pagenum, document);
}

// pretty ugly hack - we're assuming that serialize() is only called (directly) when item has not been undone,
//  while inverse() is only called when item has been undone.
UndoHistoryItem* StrokeChangedItem::inverse()
{
  return new StrokeChangedItem(*this);
}

UndoHistoryItem* PageChangedItem::inverse()
{
  return new PageChangedItem(*this);
}

