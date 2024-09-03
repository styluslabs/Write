#pragma once

#include "ulib/fileutil.h"
#include "ulib/miniz_gzip.h"
#include "page.h"
#include "syncundo.h"

struct DocPosition {
  int pagenum;
  Point pos;

  DocPosition(int _pagenum = -1, Dim x = 0, Dim y = 0) : pagenum(_pagenum), pos(x, y) {}
  DocPosition(int _pagenum, Point _pos) : pagenum(_pagenum), pos(_pos) {}
};

struct DocViewBox {
  int pagenum;
  Rect box;
  Dim zoom;

  DocViewBox(int pp = -1, const Rect& r = Rect(), Dim z = 1) : pagenum(pp), box(r), zoom(z) {}
  bool operator==(const DocViewBox& b) { return pagenum == b.pagenum && box == b.box; }
  bool operator!=(const DocViewBox& b) { return !operator==(b); }
  bool isValid() { return pagenum >= 0 && box.isValid(); }
};

class Document {
public:
  std::vector<Page*> pages;
  UndoHistory* history;
  int dirtyCount = 0;  // managed just as Page.dirtyCount
  int autoSaveSerialNum = 0;
  bool bookmarksDirty = false;
  pugi::xml_document xmldoc;

  std::unique_ptr<IOStream> blockStream;
  std::vector<bgz_block_info_t> blockInfo;

  enum loadresult_t {LOAD_OK=0, LOAD_FATAL=-1, LOAD_NONFATAL=-2, LOAD_EMPTYDOC=-3, LOAD_NEWERVERSION=-4, LOAD_NONWRITE=-5};
  // document format version
  static const int docFormatVersion = 2;
  // save flags
  typedef unsigned int saveflags_t;
  static constexpr saveflags_t SAVE_NORMAL = 0x0, SAVE_FORCE = 0x1, SAVE_MULTIFILE = 0x2, SAVE_COPY = 0x4,
      /*SAVE_AUTO_BACKUP = 0x8,*/ SAVE_BGZ_PARTIAL = 0x10;
  static size_t memoryLimit;

  Document();
  ~Document();
  int insertPage(Page* p, int where = -1);
  Page* deletePage(int where);  // , bool delstrokes = false);
  bool ensurePagesLoaded();
  bool checkAndClearErrors();
  pugi::xml_node resetConfigNode(pugi::xml_node newcfg = pugi::xml_node());
  pugi::xml_node getConfigNode();

  SvgNode* findNamedNode(const char* idstr, int* pagenumout = NULL) const;
  Page* pageForElement(const Element* s) const;
  int pageNumForElement(const Element* s) const;

  bool save(IOStream* outstrm, const char* thumb, saveflags_t flags = SAVE_NORMAL);
  loadresult_t load(IOStream* instrm, bool delayload = false);
  loadresult_t load(const pugi::xml_document& doc, const char* path= "", bool delayload = false, bool ok = true);
  bool deleteFiles();
  bool isModified() const;
  bool isEmptyFile() const;
  int numPages() const { return int(pages.size()); }

  bool saveBgz(IOStream* outstrm, const char* thumb, saveflags_t flags);
  bool loadBgzPage(Page* page);
  Document::loadresult_t loadBgzDoc(IOStream* instrm);
  const char* fileName() const { return blockStream ? blockStream->name() : ""; }
  void checkMemoryUsage(int currpage);
};
