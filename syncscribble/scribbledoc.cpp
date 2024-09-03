#include <sstream>
#include "scribbledoc.h"
#include "scribblemode.h"
#include "scribblesync.h"
#include "strokebuilder.h"
#include "scribbleapp.h"


ScribbleDoc::ScribbleDoc(ScribbleApp* parent, ScribbleConfig* _cfg, ScribbleMode* _mode)
{
  app = parent;
  globalCfg = _cfg;
  scribbleMode = _mode;
  cfg = new ScribbleConfig(globalCfg);
}

ScribbleDoc::~ScribbleDoc()
{
  closeDocument();
}

void ScribbleDoc::addArea(ScribbleArea* area)
{
  area->scribbleDoc = this;
  area->app = app;
  views.push_back(area);
  if(!activeArea)
    activeArea = area;
  nViews = views.size();
  area->loadConfig(cfg);
  if(document) {
    DocPosition docpos = activeArea ? activeArea->getPos() : DocPosition(0, 0, 0);
    area->gotoPos(docpos.pagenum, docpos.pos, false);  // this ensures currPage is set
    area->pageSizeChanged();
  }
}

void ScribbleDoc::removeArea(ScribbleArea* area)
{
  area->reset();
  views.erase(std::remove(views.begin(), views.end(), area), views.end());
  nViews = views.size();
  if(activeArea == area)
    activeArea = views.empty() ? NULL : views[nViews-1];
  area->scribbleDoc = NULL;
}

void ScribbleDoc::loadConfig(bool refresh)
{
  for(unsigned int ii = 0; ii < views.size(); ii++)
    views[ii]->loadConfig(cfg);

  if(refresh) {
    pageSizeChanged();  // most forceful way to refresh
    doRefresh();
  }
}

void ScribbleDoc::updateDocConfig(Document::saveflags_t flags)
{
  DocPosition docpos = (flags & Document::SAVE_COPY) ? DocPosition(0, 0, 0) : activeArea->getPos();
  cfg->set("pageNum", docpos.pagenum);
  cfg->set("xOffset", docpos.pos.x);
  cfg->set("yOffset", docpos.pos.y);
  cfg->set("docFormatVersion", Document::docFormatVersion);
  cfg->saveConfig(document->resetConfigNode());
}

bool ScribbleDoc::checkAndClearErrors(bool forceload)
{
  if(forceload)
    document->ensurePagesLoaded();
  return document->checkAndClearErrors();
}

void ScribbleDoc::openSharedDoc(const char* server, const pugi::xml_node& xml, bool master)
{
  if(!master)
    newDocument();
  if(scribbleSync)
    delete scribbleSync;
  scribbleSync = new ScribbleSync(this);
  scribbleSync->userMessage = [this](std::string msg, int level){ app->showNotify(msg, level); };
  scribbleSync->connectSync(server, xml, master);
  if(!scribbleSync->enableTX)
    ghostPage.reset();
}

void ScribbleDoc::closeDocument()
{
  // disconnectSync starts an event loop via waitForDisconnect, so we must disconnect before calling
  //  ScribbleArea::reset(), since ScribbleArea handling events when in an invalid state can cause crash
  exitPageSelMode();
  if(scribbleSync)
    delete scribbleSync;
  for(unsigned int ii = 0; ii < views.size(); ii++)
    views[ii]->reset();
  delete document;
  delete cfg;
  ghostPage.reset();
  scribbleSync = NULL;
  document = NULL;
  history = NULL;  // history is owned by document
  cfg = NULL;
}

void ScribbleDoc::newDocument()
{
  closeDocument();
  document = new Document();
  history = document->history;
  cfg = new ScribbleConfig(globalCfg);
  //if(props) setDefaultDims(cfg, props);
  loadConfig(false);
  document->insertPage(generatePage(0), 0);
  for(ScribbleArea* view : views) {
    view->setPageNum(0);  // updateContentDim() access currPage if VIEWMODE_SINGLE
    view->updateContentDim();
    view->gotoPos(0, Point(-10, -10), false);
  }
  pageSizeChanged();  // pageCountChanged() is only for changes due to editing
  // scribbleMode == NULL indicates we are a read-only doc (e.g. clippings), so no ghost page
  if(scribbleMode)
    ghostPage.reset(generatePage(INT_MAX));
  //fileName = "";
  fileLastMod = 0;
  uiChanged(UIState::SetDoc);
  document->bookmarksDirty = false;
  app->repaintBookmarks(true);
  doRefresh();
}

bool ScribbleDoc::deleteDocument(const char* filename)
{
  Document tempdoc;
  // OK to delay load when deleting doc
  return tempdoc.load(new FileStream(filename), true) == Document::LOAD_OK && tempdoc.deleteFiles();
}

Image ScribbleDoc::extractThumbnail(const char* filename)
{
  //static const size_t MAX_BUFF_SIZE = (1 << 20);

  StringRef buff;
  FileStream istrm(filename, "rb");  //std::ifstream istrm(filename, std::ios_base::in | std::ios::binary);
  MemStream infstrm;
  //size_t toread = 16384, bytesread = 0;

  FSPath fileinfo(filename);
  if(fileinfo.extension() == "svgz" || fileinfo.extension() == "gz") {
    minigz_io_t zistrm(istrm);
    auto blockInfo = bgz_get_index(zistrm);
    std::stringstream inf_block;
    if(blockInfo.empty() || !bgz_read_block(zistrm, &blockInfo.back() - 1, minigz_io_t(infstrm)))
      return Image(0,0);
    buff = StringRef(infstrm.data(), infstrm.size());  //buff.len = zstrm.readp((void**)&buff.str, SIZE_MAX);
  }
  else
    buff.len = istrm.readp((void**)&buff.str, 1 << 18);  // 256KB

  int idx = buff.find("id=\"thumbnail\"");
  if(idx < 0)
    idx = buff.find("id='thumbnail'");
  if(idx >= 0) {
    idx = buff.find("base64,", idx);
    if(idx >= 0) {
      int end = buff.findFirstOf("'\"", idx);
      if(end >= 0) {
        idx += 7;
        auto dec64 = base64_decode(&buff[idx], end - idx);
        Image img = Image::decodeBuffer(dec64.data(), dec64.size());
        return img;
      }
    }
  }
  return Image(0,0);
}

Document::loadresult_t ScribbleDoc::openDocument(const char* filename, bool delayload)
{
  FileStream* strm = new FileStream(filename, "rb+");
  if(!strm->is_open())
    strm->open("rb");
  return openDocument(strm, delayload);
}

Document::loadresult_t ScribbleDoc::openDocument(IOStream* strm, bool delayload)
{
  Document* newdoc = new Document();
  auto res = newdoc->load(strm, delayload);
  if(res == Document::LOAD_FATAL || (res == Document::LOAD_EMPTYDOC
      && !containsWord("svg svgz html htm", FSPath(strm->name()).extension().c_str()))) {
    delete newdoc;
    return Document::LOAD_FATAL;
  }
  // replace this->document with newdoc
  closeDocument();
  document = newdoc;
  history = document->history;
  cfg = new ScribbleConfig(globalCfg);
  cfg->loadConfig(document->getConfigNode());
  loadConfig(false);
  if(res == Document::LOAD_EMPTYDOC)
    document->insertPage(generatePage(0), 0);
  res = cfg->Int("docFormatVersion", 0) > Document::docFormatVersion ? Document::LOAD_NEWERVERSION : res;
  int pagenum = cfg->Int("pageNum", 0);
  Point pos(cfg->Float("xOffset", -10.0f), cfg->Float("yOffset", -10.0f));
  // we do this before calling pageSizeChanged() to prevent unnecessary loading of page 0
  for(ScribbleArea* view : views) {
    view->setPageNum(pagenum);  // updateContentDim() access currPage if VIEWMODE_SINGLE
    view->updateContentDim();
    view->gotoPos(pagenum, pos, false);
  }
  pageSizeChanged();  // pageCountChanged() is only for changes due to editing
  // no ghost page if any load errors to suggest user not modify document
  if((res == Document::LOAD_OK || res == Document::LOAD_EMPTYDOC) && scribbleMode)
    ghostPage.reset(generatePage(INT_MAX));
  uiChanged(UIState::SetDoc);
  document->bookmarksDirty = false;
  app->repaintBookmarks(true);
  doRefresh();
#if !PLATFORM_IOS
  fileLastMod = getFileMTime(fileName());
#endif
  return res;
}

bool ScribbleDoc::saveDocument(const char* filename, Document::saveflags_t flags)
{
  return saveDocument(filename ? new FileStream(filename, "wb+") : NULL, flags);
}

bool ScribbleDoc::saveDocument(IOStream* strm, Document::saveflags_t flags)
{
  if(!strm)
    flags |= Document::SAVE_BGZ_PARTIAL;  // whole file will be written if this is not set

  doCancelAction();
  // when saving a copy for sharing, set position to start of document
  updateDocConfig(flags);
  flags |= cfg->Int("compressLevel", 2) << 24;  // ignored for uncompressed formats

  bool ok = false;
  if(cfg->Bool("saveThumbnail")) {
#if PLATFORM_IOS
    IOStream* thumbstrm = strm ? strm : document->blockStream.get();
    if(thumbstrm->type() & IOStream::UIDOCSTREAM) {
      Image iosthumb(1024, 1024);
      activeArea->drawThumbnail(&iosthumb);
      iosSetDocThumbnail(static_cast<UIDocStream*>(thumbstrm)->uiDocument,
        iosthumb.bytes(), iosthumb.width, iosthumb.height);
    }
#endif
    Image thumbnail(240, 400, Image::PNG);
    activeArea->drawThumbnail(&thumbnail);
    auto buff = base64_encode(thumbnail.encode(Image::PNG));
    ok = document->save(strm, (char*)buff.data(), flags);
  }
  else
    ok = document->save(strm, NULL, flags);
  if(ok) {
#if !PLATFORM_IOS
    fileLastMod = getFileMTime(fileName());
#endif
    app->refreshUI(this, (1 << UIState::SaveDoc));
  }
  else
    delete strm;

  return ok;
}

// insert pages from another document into the current document (immediately following current page)
Document::loadresult_t ScribbleDoc::insertDocument(IOStream* strm)
{
  Document* otherdoc = new Document();
  Document::loadresult_t res = otherdoc->load(strm, false);
  if(res == Document::LOAD_OK) {
    int where = activeArea->currPageNum + 1;
    startAction((where - 1) | UndoHistory::MULTIPAGE);
    while(otherdoc->numPages() > 0) {
      Page* page = otherdoc->deletePage(otherdoc->numPages() - 1);
      document->insertPage(page, where);
    }
    endAction();
    updateGhostPage();
    pageCountChanged(where);
    activeArea->gotoPos(where + 1, Point(-10, -10), false);
    uiChanged(UIState::InsertDoc);
    doRefresh();
  }
  delete otherdoc;
  return res;
}

void ScribbleDoc::resetDocPrefs()
{
  delete cfg;
  cfg = new ScribbleConfig(globalCfg);   //cfg->set("bookmarkSerialNum", n);
}

void ScribbleDoc::doUndoRedo(bool redo)
{
  clearSelection();
  // clear page's dirty rect so we can jump to what was dirtied by undo
  dirtyPage(activeArea->currPageNum);
  int prevpages = document->numPages();
  // handle case of pages added and removed w/ no change in page count (currently just deletion of sole page)
  int docdirty = document->dirtyCount;
  int pagenum = redo ? history->redo() : history->undo();
  undoRedoUpdate(pagenum, document->numPages() == prevpages && document->dirtyCount != docdirty ? 0 : prevpages);
}

// this is split out as a separate fn because it is also used by SyncScribble (for which viewdirty = false)
void ScribbleDoc::undoRedoUpdate(int pagenum, int prevpages, bool viewdirty)
{
  bool multipage = pagenum & UndoHistory::MULTIPAGE;
  pagenum = pagenum & ~UndoHistory::MULTIPAGE;
  int npages = document->numPages();
  if(prevpages != npages || pagenum < 0 || pagenum >= npages) {
    // try to restore each area's position
    pageCountChanged(pagenum, prevpages);
    document->bookmarksDirty = true;
    if(viewdirty && pagenum >= 0) {
      if(pagenum < npages) {
        // page removed - show top of next page ... do this for page added too
        if(prevpages > npages || !activeArea->isPageVisible(pagenum))
          activeArea->viewRect(pagenum, Rect::wh(document->pages[pagenum]->width(), 0));
      }
      else
        activeArea->gotoPage(npages);  // last page removed - show bottom of prev page
    }
    return;
  }
  Page* page = document->pages[pagenum];
  Rect pagedirty = dirtyPage(pagenum);  // update dirty region for all views  //page->getDirty();
  if(!pagedirty.isValid())
    pagedirty = page->rect();
  else if(pagedirty.contains(page->rect())) {
    pageSizeChanged();
    document->bookmarksDirty = true;
  }
  for(size_t ii = 0; ii < nViews; ++ii) {
    if(multipage)
      views[ii]->repaintAll();
    if(viewdirty && views[ii]->currPageNum == pagenum && views[ii]->isVisible(views[ii]->pageDimToDim(pagedirty)))
      viewdirty = false;
  }
  if(viewdirty)
    activeArea->viewRect(pagenum, pagedirty);  // ensure dirty rect is visible
}

// what we want to do: view a specified pos in activeArea; don't change other views
// - problems: we'd have to save other views before making change
void ScribbleDoc::pageCountChanged(int pagenum, int prevpages)
{
  for(unsigned int ii = 0; ii < nViews; ii++)
    views[ii]->pageCountChanged(pagenum, prevpages);
}

// insert a new page before page "where"; we make the new page the current page
Page* ScribbleDoc::generatePage(int where) const
{
  Page* newPage = NULL;
  Page* refPage = document->numPages() > 0 ?
      document->pages[std::max(0, std::min(where, document->numPages()-1))] : NULL;
  // if page that will preceed page being inserted has custom ruling, duplicate it
  if(refPage && refPage->isCustomRuling && !refPage->ruleNode->hasClass("write-no-dup"))
    newPage = new Page(refPage->getProperties(), refPage->ruleNode);
  else {
    PageProperties props(cfg->Float("pageWidth"), cfg->Float("pageHeight"),
        cfg->Float("xRuling"), cfg->Float("yRuling"), cfg->Float("marginLeft"),
        Color::fromRgb(cfg->Int("pageColor")), Color::fromArgb(cfg->Int("ruleColor")));
    newPage = new Page(props);
  }
  if(globalCfg->Bool("sRGB"))
    newPage->svgDoc->setAttribute("color-interpolation", "linearRGB");  // SVG spec says default is "sRGB"
  return newPage;
}

void ScribbleDoc::updateGhostPage()
{
  if(ghostPage)
    ghostPage.reset(generatePage(INT_MAX));
}

void ScribbleDoc::newPage(int where)
{
  // if appending page, give `where` appropriate value for undo/redo
  if(where < 0 || where > document->numPages())
    where = document->numPages();
  startAction(where);
  // for double tap to add page, looks like first tap will make last page current, so currPageNum is safe
  insertPage(generatePage(activeArea->currPageNum), where);
  endAction();
}

void ScribbleDoc::insertPage(Page* page, int where)
{
  clearSelection();
  where = document->insertPage(page, where);
  if(where >= document->numPages() - 1)
    updateGhostPage();
  // view the new page
  pageCountChanged(where, document->numPages() - 1);
  activeArea->gotoPage(where);
  uiChanged(UIState::InsertPage);
}

void ScribbleDoc::deletePage(int where)
{
  if(document->numPages() < 2)
    return;
  clearSelection();
  document->pages[where]->isSelected = true;
  deletePages();
}

void ScribbleDoc::deletePages()
{
  int where = -1;
  int prevpages = document->numPages();
  // find first visible page that will not be deleted
  Page* prevpospage = NULL;
  Point prevpos;
  for(int ii = 0; ii < document->numPages(); ++ii) {
    Page* p = document->pages[ii];
    if(!p->isSelected && activeArea->isPageVisible(ii)) {
      prevpospage = p;
      prevpos = activeArea->screenToDim(Point(0,0)) - activeArea->getPageOrigin(ii);
      break;
    }
  }

  for(int ii = 0; ii < document->numPages();) {
    if(document->pages[ii]->isSelected) {
      if(where < 0)
        startAction(ii | UndoHistory::MULTIPAGE);
      where = ii;
      document->pages[ii]->isSelected = false;
      document->deletePage(ii);
    }
    else
      ++ii;
  }
  if(where < 0)
    return;
  if(document->numPages() < 1)
    document->insertPage(generatePage(0), 0);
  endAction();
  if(where >= document->numPages())
    updateGhostPage();
  document->bookmarksDirty = true;  // deleted page may have contained bookmarks
  pageCountChanged(where, prevpages);  // this will set curr page to "where"
  // restore view position
  // what if no deleted pages were visible initially? ... difficult to determine if this is the case
  if(prevpospage)
    activeArea->gotoPos(prevpospage->getPageNum(), prevpos, false);

  uiChanged(UIState::DeletePage);
}

void ScribbleDoc::pastePages(Clipboard* clipboard, int where)
{
  clearSelection();  // clear stroke selection/page selection
  int prevpages = document->numPages();

  // getPos() uses the current page, so make sure it's the first visible page!
  activeArea->setPageNum(activeArea->dimToPageNum(activeArea->screenToDim(Point(0,0))));
  DocPosition prevpos = activeArea->getPos();
  Page* prevpospage = document->pages[prevpos.pagenum];
  int numpaste = clipboard->content->children().size();

  startAction((where - 1) | UndoHistory::MULTIPAGE);
  for(SvgNode* n : clipboard->content->children()) {
    Page* p = new Page;
    if(n->type() == SvgNode::DOC)
      p->loadSVG(static_cast<SvgDocument*>(n->clone()));
    document->insertPage(p, where++);
  }
  endAction();
  updateGhostPage();
  document->bookmarksDirty = true;
  pageCountChanged(where - numpaste, prevpages);

  activeArea->gotoPos(prevpospage->getPageNum(), prevpos.pos, false);
  // pasted pages being visible takes precedence
  if(!activeArea->isPageVisible(where - 1) && !activeArea->isPageVisible(where - numpaste))
    activeArea->gotoPage(where - numpaste);

  uiChanged(UIState::InsertPage);
}

// pagenum < 0 to select range from previous page to -pagenum-1; 0 - npages-1 to toggle single page;
//  SelectPagesFlags for select all, invert selection
void ScribbleDoc::selectPages(int pagenum)  //, Point toolpos)
{
  int first = pagenum, last = pagenum;
  bool forcesel = pagenum < 0 || pagenum == PAGESEL_ALL;
  if(pagenum < 0) {
    pagenum = std::min(-pagenum-1, document->numPages() - 1);
    first = std::min(pagenum, prevSelPageNum);
    last = std::max(pagenum, prevSelPageNum);
  }
  else if(pagenum > document->numPages() - 1) {
    first = 0;
    last = document->numPages() - 1;
  }
  for(int ii = first; ii <= last; ++ii) {
    Page* p = document->pages[ii];
    numSelPages += !p->isSelected ? 1 : (forcesel ? 0 : -1);
    p->setSelected(forcesel ? true : !p->isSelected);
  }
  if(pagenum < document->numPages())
    prevSelPageNum = pagenum;
  if(first != last)
    repaintAll();
  // popup tools not shown for stroke selection sel all/inv sel either (tools avail on overflow menu anyway)
  //if(numSelPages > 0 && cfg->Bool("popupToolbar"))  //&& !toolpos.isNaN()
  //  app->showSelToolbar(activeArea->screenToGlobal(toolpos.isNaN() ? activeArea->screenRect.center() : toolpos));
}

// used only for drag and drop of clippings pages
void ScribbleDoc::movePage(int oldpagenum, int newpagenum)
{
  if(oldpagenum < 0 || oldpagenum >= document->numPages() || oldpagenum == newpagenum)
    return;
  clearSelection();  // otherwise Element.m_selection won't be cleared on copy!
  Page* newpage = new Page;
  // this replaces previous method of serializing and deserializing
  newpage->loadSVG(document->pages[oldpagenum]->svgDoc->clone());
  startAction(oldpagenum | UndoHistory::MULTIPAGE);
  document->deletePage(oldpagenum);
  document->insertPage(newpage, newpagenum);
  endAction();
  updateGhostPage();
  document->bookmarksDirty = true;
  pageCountChanged(MIN(newpagenum, oldpagenum));
  uiChanged(UIState::MovePage);
}

void ScribbleDoc::deleteSelection()
{
  if(numSelPages > 0) {
    deletePages();
    numSelPages = 0;
    prevSelPageNum = 0;
  }
  else
    activeArea->deleteSelection();
}

void ScribbleDoc::clearSelection()
{
  if(numSelPages > 0) {
    for(Page* p : document->pages)
      p->setSelected(false);
    numSelPages = 0;
    prevSelPageNum = 0;
  }
  else {
    for(unsigned int ii = 0; ii < views.size(); ii++) {
      if(views[ii]->currSelection)
        views[ii]->clearSelection();
    }
  }
}

// can't include in clearSelection() since we also call that for, e.g., undo and pasting pages
void ScribbleDoc::exitPageSelMode()
{
  if(scribbleMode && scribbleMode->getMode() == MODE_PAGESEL) {
    clearSelection();
    scribbleMode->setMode(MODE_STROKE);
  }
}

void ScribbleDoc::doCancelAction()
{
  activeArea->doCancelAction();
}

// set dimensions for future pages
void ScribbleDoc::setDefaultDims(ScribbleConfig* c, const PageProperties* props)
{
  if(props->width > 0)
    c->set("pageWidth", props->width);
  if(props->height > 0)
    c->set("pageHeight", props->height);
  c->set("xRuling", props->xRuling);
  c->set("yRuling", props->yRuling);
  c->set("marginLeft", props->marginLeft);
  c->set("pageColor", int(props->color.argb()));
  c->set("ruleColor", int(props->ruleColor.argb()));
  //if(props->ruleLayer) {
  //  c->setRuling("default", new RuleLayer(*props->ruleLayer));
  //}
}

// should move this (these?) somewhere else; also, get rid of clip check in setPageProperties
static bool doesClipPageStrokes(const PageProperties* props, Page* page)
{
  Rect r = page->getBBox();
  return (props->width != 0 && r.right > props->width && r.right < page->props.width)
      || (props->height != 0 && r.bottom > props->height && r.bottom < page->props.height);
}

bool ScribbleDoc::doesClipStrokes(const PageProperties* props, bool applytoall)
{
  if(!applytoall)
    return doesClipPageStrokes(props, activeArea->currPage);
  for(Page* page : document->pages) {
    if((numSelPages == 0 || page->isSelected) && doesClipPageStrokes(props, page))
      return true;
  }
  return false;
}

// TODO: enum RulingFlags {RULING_ALL=1, RULING_DFLT=2, RULING_GLOBAL=4, RULING_NOUNDO=8};
bool ScribbleDoc::setPageProperties(const PageProperties* props, bool applytoall, bool docdefault, bool global, bool undoable)
{
  if(undoable)
    startAction(activeArea->currPageNum | (applytoall ? UndoHistory::MULTIPAGE : 0));
  bool clipped = false;
  if(applytoall) {
    for(Page* page : document->pages) {
      if(numSelPages == 0 || page->isSelected)
        clipped = page->setProperties(props) || clipped;
    }
  }
  else
    clipped = activeArea->currPage->setProperties(props);
  if(undoable)
    endAction();
  pageSizeChanged();
  // set doc and/or global defaults
  if(docdefault)
    setDefaultDims(cfg, props);
  if(global)
    setDefaultDims(globalCfg, props);
  //if(docdefault || global || (activeArea->currPageNum == document->numPages() - 1 && activeArea->currPage->isCustomRuling))
  updateGhostPage();
  uiChanged(UIState::SetPageProps);
  doRefresh();
  return clipped;
}

void ScribbleDoc::openURL(const char* url)
{
  // address starting with a '.' (so '.' or '..') is assumed to point to a local file
  if(url[0] == '.') {
    FSPath urlfile = FSPath(fileName()).parent().child(url);
    std::string href;
    if(!urlfile.exists()) {
      size_t pnd = urlfile.path.find_last_of('#');
      if(pnd != std::string::npos) {
        href = urlfile.path.substr(pnd);
        urlfile.path.resize(pnd);
      }
    }
    // assume link to local HTML file is a Write document
    // QProcess::startDetached(QApplication::applicationFilePath(), QStringList(filename));
    if(urlfile.exists() && urlfile.extension() == cfg->String("docFileExt")) {
      if(app->openDocument(canonicalPath(urlfile)) && !href.empty())
        app->activeArea()->viewHref(href.c_str());
    }
    else
#if PLATFORM_OSX
      ScribbleApp::openURL(("file:///" + urlEncode(urlfile.path.c_str())).c_str());
#else
      ScribbleApp::openURL(("file:///" + urlfile.path).c_str());
#endif
  }
  else
    ScribbleApp::openURL(url);
}

bool ScribbleDoc::canUndo() const
{
  return history->canUndo() && (!scribbleSync || scribbleSync->canUndo());
}

bool ScribbleDoc::canRedo() const
{
  return history->canRedo();
}

// we could consider moving some (most) of these to Application
void ScribbleDoc::doCommand(int itemid)
{
  doCancelAction();
  switch(itemid) {
  case ID_UNDO:
    if(canUndo()) {
      doUndoRedo();
      if(scribbleSync)
        scribbleSync->sendHist();
    }
    break;
  case ID_REDO:
    if(canRedo()) {
      doUndoRedo(true);
      if(scribbleSync)
        scribbleSync->sendHist();
    }
    break;
  // clipboard related
  case ID_DELSEL:
    deleteSelection();
    break;
  case ID_INVSEL:
    if(scribbleMode->getMode() == MODE_PAGESEL)
      selectPages(PAGESEL_INV);
    else
      activeArea->invertSelection();
    break;
  case ID_SELALL:
    if(scribbleMode->getMode() == MODE_PAGESEL)
      selectPages(PAGESEL_ALL);
    else
      activeArea->selectAll();
    break;
  case ID_COPYSEL:
  case ID_CUTSEL:
  case ID_DUPSEL:
  {
    std::unique_ptr<Clipboard> cb(new Clipboard);
    Selection* sel = activeArea->currSelection;
    int flags = 0;
    if(!sel) {
      for(Page* p : document->pages) {
        if(p->isSelected) {
          SvgNode* p2 = p->svgDoc->clone();
          // make sure "write-page" class is set even for foreign docs - used to distinguish pages when pasting
          if(!p2->hasClass("write-page"))
            p2->addClass("write-page");
          cb->content->addChild(p2);  //addStroke(p->docElement());
        }
      }
    }
    else {
      flags = sel->selector && !sel->selector->drawHandles ? ScribbleArea::PasteNoHandles : 0;
      sel->toSorted(cb.get());
      if(itemid != ID_CUTSEL)
        cb->replaceIds();
    }
    if(!cb->count())  // normally prevented by button disabled in UI, but happened running test with Ctrl pressed
      break;
    if(itemid == ID_DUPSEL)
      activeArea->doPaste(cb.get(), NULL, flags | ScribbleArea::PasteMoveClipboard);
    else
      app->setClipboard(cb.release(), (sel && itemid == ID_COPYSEL) ? activeArea->getCurrPage() : NULL, flags);
    if(itemid == ID_CUTSEL)
      deleteSelection();
    break;
  }
  case ID_PASTE:
    if(app->clipboard)
      activeArea->doPaste(app->clipboard.get(), app->clipboardPage, app->clipboardFlags);
    break;
  case ID_PAGEAFTER:
    newPage(activeArea->currPageNum + 1);
    break;
  case ID_PAGEBEFORE:
    newPage(activeArea->currPageNum);
    break;
  case ID_DELPAGE:
    deletePage(activeArea->currPageNum);
    break;
  default:
    activeArea->doCommand(itemid);
    return;
  }
  uiChanged(UIState::Command);
  doRefresh();
}

// returned rect only needed for jumping view for undo
// we could consider instead scanning all pages for dirtiness, instead of only active pages of each area
Rect ScribbleDoc::dirtyPage(int pagenum)
{
  Rect dirty = document->pages[pagenum]->getDirty();
  if(dirty.isValid()) {
    for(size_t ii = 0; ii < nViews; ii++)
      views[ii]->dirtyPage(pagenum, dirty);
    document->pages[pagenum]->clearDirty();
  }
  return dirty;
}

void ScribbleDoc::pageSizeChanged()
{
  for(unsigned int ii = 0; ii < nViews; ii++)
    views[ii]->pageSizeChanged();
}

void ScribbleDoc::repaintAll()
{
  for(unsigned int ii = 0; ii < nViews; ii++)
    views[ii]->repaintAll();
}

void ScribbleDoc::doRefresh()
{
  if(autoSaveReq && getActiveMode() == MODE_NONE) {
    autoSaveReq = false;
    if(fileName()[0])
      saveDocument();
  }
  // won't actually repaint unless dirty
  // shouldn't we just check all (visible) pages? then we wouldn't need explicit repaintAll calls (?)
  for(ScribbleArea* view : views)
    dirtyPage(view->currPageNum);  // first build directRectDim from each potentially dirty page
  for(ScribbleArea* view : views)
    view->reqRepaint();  // then update dirtyRectScreen for each view and set PIXELS_DIRTY if needed

  if(document->bookmarksDirty) {
    app->repaintBookmarks();
    document->bookmarksDirty = false;
  }
  if(uiDirty)
    app->refreshUI(this, uiDirty);
  uiDirty = 0;
}

void ScribbleDoc::uiChanged(int reason)
{
  ASSERT((1 << reason) > 0 && "Too many UIChangeFlags values!");
  uiDirty |= (1 << reason);
}

void ScribbleDoc::bookmarkHit(int pagenum, Element* bookmark)
{
  Page* page = document->pages[pagenum];
  Point cornerpos(bookmark->bbox().left - 5,
      MIN(bookmark->bbox().top - 5, page->getYforLine(page->getLine(bookmark))));

  activeArea->doGotoPos(pagenum, cornerpos, false);
  if(cfg->Bool("autoHideBookmarks"))
    app->hideBookmarks();
}

int ScribbleDoc::getScribbleMode(int modemod)
{
  return scribbleMode ? scribbleMode->getScribbleMode(modemod) : MODE_NONE;
}

int ScribbleDoc::getActiveMode() const
{
  return activeArea->currMode;
}

Color ScribbleDoc::getCurrPageColor() const
{
  return activeArea->getCurrPage()->props.color;
}

// dirty is relative to activeArea's current page!
void ScribbleDoc::updateCurrStroke(Rect dirty)
{
  if(!dirty.isValid()) {}
  else if(nViews > 1) {
    // we just want to dirty screen, but this is the easiest way since `dirty` is relative to activeArea's
    //  page and other views could have different page number
    for(size_t ii = 0; ii < nViews; ii++)
      views[ii]->dirtyPage(activeArea->currPageNum, dirty);
    //activeArea->currPage->growDirtyRect(dirty);
  }
  else
    activeArea->dirtyScreen(dirty);
}

void ScribbleDoc::scribbleDone()
{
  // cursor update will now be handled by refreshUI
  if(scribbleMode)
    scribbleMode->scribbleDone();
  // process any received items that we're held back until local user input release
  if(scribbleSync)
    scribbleSync->processRecvBuff();
}

// handles update to strokes outside of the undo system - currently the only place this happens is COM update
//  by ScribbleArea::groupStrokes
void ScribbleDoc::strokesUpdated(const std::vector<Element*>& strokes)
{
  if(scribbleSync)
    scribbleSync->sendStrokeUpdate(strokes);
}

void ScribbleDoc::startAction(int pagenum)
{
  if(scribbleSync)
    scribbleSync->syncClearUndone();
  history->startAction(pagenum);
}

void ScribbleDoc::endAction()
{
  history->endAction();
  if(scribbleSync)
    scribbleSync->sendHist();
}
