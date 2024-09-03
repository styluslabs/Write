#pragma once

#include <vector>
#include <string>
#include <functional>
#include "ulib/fileutil.h"
#include "element.h"

class PageProperties {
public:
  Dim width;
  Dim height;
  Color color;  // paper color
  Dim xRuling;
  Dim yRuling;
  Dim marginLeft;
  Color ruleColor;

  PageProperties(Dim w=0, Dim h=0, Dim xr=0, Dim yr=0, Dim ml=0, Color c=Color::WHITE, Color rc=Color::BLUE);
};

// 1 page = 1 <svg> node
class Document;

class Page {
public:
  PageProperties props;
  std::unique_ptr<SvgDocument> svgDoc;
  SvgContainerNode* contentNode = NULL;
  SvgContainerNode* ruleNode = NULL;
  bool isCustomRuling = false;
  Timestamp minTimestamp = MAX_TIMESTAMP;
  Timestamp maxTimestamp = 0;
  Document* document = NULL;  // parent document
  std::list<Element*> bookmarks;
  Dim maxBookmarkWidth = 0;
  int numBookmarks = -1;
  enum loadstatus_t {LOAD_SVG_ERROR=-1, NOT_LOADED=0, LOAD_OK=1} loadStatus = NOT_LOADED;
  // dirtyCount is managed by undo system; page needs to be written out if != 0
  int dirtyCount = 0;
  //int autoSavedDirtyCount = NOT_AUTO_SAVED;  // dirtyCount value of last autosave
  int blockIdx = -1;
  std::string fileName;
  std::string autoSaveFileName;

  Dim yRuleOffset = 0;
  // this is a hack for drawing clippings, but in the new SVG model, pages can have an arbitrary 2D
  //  transform, so this is really just a special case of that
  Dim scaleFactor = 1;
  bool isSelected = false;

  Page(Dim w=0, Dim h=0, int idx = -1);
  Page(const PageProperties& _props, const SvgContainerNode* ruling = NULL);
  void initDoc();
  PageProperties getProperties();
  bool setProperties(const PageProperties* props);
  //void changeSpacing(Dim newxruling, Dim newyruling, Dim newmarginLeft);
  void onPageSizeChange();
  void generateRuleLayer(Color pageColor, Dim w, Dim h);
  int getPageNum() const;
  int getLine(Dim y) const;
  int getLine(const Element* s) const;
  std::function<bool(const Element* a, const Element* b)> cmpRuled();
  Dim getYforLine(int line) const;
  Dim height() const;
  Dim width() const;
  Dim yruling(bool usedefault = false) const;
  Dim xruling() const { return props.xRuling; }
  Dim marginLeft() const { return props.marginLeft; }
  Rect rect() const { return Rect::ltwh(0, 0, width(), height()); }
  Color color() const { return props.color; }
  void draw(Painter* painter, const Rect& dirty, bool rulelines = true);

  Rect getDirty() const { return SvgPainter::calcDirtyRect(svgDoc.get()); }
  void clearDirty() { SvgPainter::clearDirty(svgDoc.get()); }
  int strokeCount() const { return contentNode ? contentNode->children().size() : 0; }
  Rect getBBox() const { return contentNode->bounds(); }
  void recalcTimeRange(bool force = false);

  Range<ElementIter> children() const;
  void addStroke(Element* s, Element* next = NULL);
  void removeStroke(Element* s);
  void onAddStroke(Element* s);
  void onRemoveStroke(Element* s);
  const char* getHyperRef(Point pos) const;
  SvgNode* findNamedNode(const char* idstr) const;

  bool saveSVG(IOStream& file, Dim x = 0, Dim y = 0);
  bool saveSVGFile(const char* filename);
  bool loadSVG(SvgDocument* doc);
  bool loadSVGFile(const char* filename = NULL, bool delayload = false);
  bool ensureLoaded(bool checkmem = true);
  void migrateLegacySVG();
  void contentToRuling();
  void setSelected(bool sel);
  void unload();

  static Dim BLANK_Y_RULING;
  static const color_t DEFAULT_RULE_COLOR = Color::BLUE;
  //static const int NOT_AUTO_SAVED = INT_MAX;
  static bool enableDropShadow;
};
