#ifndef SELECTION_H
#define SELECTION_H

#include "page.h"

// Initially, I tried separate RectSelection, RuledSelection, etc
//  objects, but didn't like it
// To support multiple selections, we may need to have a single
//  selection object to which we add rectangles, lasso paths, etc.
class Selector;
class Clipboard;

class Selection {
public:
  Rect bbox;
  Page* page;
  ScribbleTransform transform;
  Element* sourceNode;
  std::list<Element*> strokes;
  // SELMODE_PASSIVE does not take ownership of strokes
  enum {SELMODE_REPLACE, SELMODE_UNION, SELMODE_PASSIVE, SELMODE_NONE} selMode = SELMODE_REPLACE;
  Selector* selector = NULL;
  Timestamp minTimestamp = MAX_TIMESTAMP;
  Timestamp maxTimestamp = 0;
  enum StrokeDrawType {STROKEDRAW_NORMAL, STROKEDRAW_SEL, STROKEDRAW_NONE};

  Selection(Page* source, StrokeDrawType drawtype = STROKEDRAW_SEL);
  ~Selection();  // destructor calls clear() to deselect all strokes

  template <class Predicate>
  Element* getFirstElement(Predicate pred)
  {
    // previously, we descended into groups to find a matching stroke
    auto it = find_if(strokes.begin(), strokes.end(), pred);
    return it != strokes.end() ? *it : NULL;
  }

  void addStroke(Element* s, Element* next = NULL);
  bool removeStroke(Element* s);

  void clear();
  void deleteStrokes();
  void selectAll();
  void invertSelection();
  void selectbyProps(Element* elproto, bool useColor, bool useWidth);
  int sortRuled();
  void insertSpace(Dim dx, int dline);
  void reflowStrokes(Dim dx, int dline, Dim minWordSep);
  int count() const { return int(strokes.size()); }
  void recalcTimeRange();
  void invalidateBBox() { bbox = Rect(); }
  // the default, STROKEDRAW_NONE uses the current draw type (since none doesn't make sense)
  void draw(Painter* painter, StrokeDrawType drawtype = STROKEDRAW_NONE);  //const Rect& dirty = Rect());

  void translate(Dim dx, Dim dy);
  void setOffset(Dim x, Dim y);
  void setOffset(Point p) { setOffset(p.x, p.y); }
  Point getOffset() const { return Point(transform.xoffset(), transform.yoffset()); }
  void rotate(Dim radians, Point origin);
  void scale(Dim sx, Dim sy, Point origin, bool scalewidths = false);
  void applyTransform(const ScribbleTransform& tf);
  void resetTransform();
  void stealthTransform(const ScribbleTransform& tf);
  void setStrokeProperties(const StrokeProperties& props);
  StrokeProperties getStrokeProperties() const;
  bool containsGroup();
  void ungroup();
  void toSorted(Clipboard* dest) const;

  //void growDirtyRect(const Rect& r);
  Rect getBBox();
  Rect getBGBBox();
  void setZoom(Dim zoom);
  void shrink();
  void commitTransform();
  // by default, nothing to draw (e.g. PathSelection)
  void drawBG(Painter* painter);
  void setDrawType(StrokeDrawType newtype);
  StrokeDrawType drawType() const { return m_drawType; }
  bool xchgBGDirty(bool b);

//private:
  void doSelect();

  StrokeDrawType m_drawType;
};

// clipboard owns its strokes, unlike Selection
class Clipboard
{
public:
  std::unique_ptr<SvgDocument> content;

  Clipboard() : content(new SvgDocument) {}
  Clipboard(SvgDocument* doc) : content(doc) {}
  int count() const { return content->children().size(); }
  void addStroke(Element* s) { content->addChild(s->node); }
  void paste(Selection* sel, bool move);
  void replaceIds() { content->replaceIds(); }
  void saveSVG(std::ostream& file);
};

// Selection object manages the Selector - it will delete selector in its destructor

class Selector
{
public:
  Selection* selection;

  Selector(Selection* _sel);
  virtual ~Selector();

  virtual bool selectHit(Element* s) = 0;
  virtual Rect getBGBBox() { return Rect(); }
  virtual void shrink() {}
  virtual void drawBG(Painter* painter) {}
  virtual void setZoom(Dim zoom) {}
  virtual void transform(const Transform2D& tf) {}

  virtual Point scaleHandleHit(Point pos, bool touch) { return Point(NaN, NaN); }
  virtual Point rotHandleHit(Point pos, bool touch) { return Point(NaN, NaN); }
  virtual Point cropHandleHit(Point pos, bool touch) { return Point(NaN, NaN); }

  bool drawHandles = true;
  bool bgDirty = false;

protected:
  Color bgStroke;
  Color bgFill;

};

class PathSelector : public Selector
{
public:
  PathSelector(Selection* _sel) : Selector(_sel) {}

  void selectPath(Point p, Dim radius);
  bool selectHit(Element* s) override;
  Rect getBGBBox() override;
  void drawBG(Painter* painter) override;

private:
  Path2D path;

  Point selPos = Point(NaN, NaN);
  Dim selRadius;
};

class RectSelector : public Selector
{
public:
  enum {RECTSEL_BBOX, RECTSEL_COM, RECTSEL_ANY} rectSelMode = RECTSEL_BBOX;
  bool enableCrop = false;

  RectSelector(Selection* _sel, Dim zoom = 1, bool handles = true)
      : Selector(_sel), mZoom(zoom) { drawHandles = handles;  }
  void selectRect(Dim x0, Dim y0, Dim x1, Dim y1);
  bool selectHit(Element* s) override;
  void shrink() override;
  Rect getBGBBox() override;
  void drawBG(Painter* painter) override;
  void setZoom(Dim zoom) override;
  void transform(const Transform2D& tf) override;

  Point scaleHandleHit(Point pos, bool touch) override;
  Point rotHandleHit(Point pos, bool touch) override;
  Point cropHandleHit(Point pos, bool touch) override;

  static Dim HANDLE_PAD;

private:
  Rect selRect;
  Dim mZoom;
  static Dim HANDLE_SIZE;
};

struct RuledRange
{
  Dim x0;
  Dim ymin;
  Dim x1;
  Dim ymax;
  Dim xruling;
  Dim yruling;
  std::vector<Dim> lstops;
  std::vector<Dim> rstops;

  RuledRange(Dim _x0, Dim _ymin, Dim _x1, Dim _ymax, Dim _xruling, Dim _yruling) :
      x0(_x0), ymin(_ymin), x1(_x1), ymax(_ymax), xruling(_xruling), yruling(_yruling) {}

  bool isSingleLine() const;
  bool isValid() const;
  RuledRange& translate(Dim dx, Dim dy);

  Dim lstop(int idx) const;
  Dim rstop(int idx) const;
  int nlines() const { return (ymax - ymin)/yruling; }
};

class RuledSelector : public Selector
{
public:
  enum SelMode {SEL_CONTAINED, SEL_OVERLAP} selMode;
  enum ColMode {COL_NONE=0, COL_NORMAL=1, COL_AGGRESSIVE=2} colMode;
  std::vector<Dim> lstops;
  std::vector<Dim> rstops;

  RuledSelector(Selection* _sel, ColMode cm = COL_NORMAL) : Selector(_sel), selMode(SEL_CONTAINED),
      colMode(cm), selRange(MAX_DIM, MAX_DIM, MIN_DIM, MIN_DIM, 0, 0) {}
  void selectRuled(Dim x0, int line0, Dim x1, int line1);
  void selectRuled(const RuledRange& range);
  // selections for ruled insert space
  void selectRuledAfter(Dim x, int linenum);
  //void selectRuledBelow(int linenum);
  void findStops(Dim x, int linenum);
  bool selectHit(Element* s) override;
  void shrink() override;
  Rect getBGBBox() override;
  void drawBG(Painter* painter) override;

  static Dim MIN_DIV_HEIGHT;

private:
  RuledRange selRange;
};

// Note that a base class (here, Selector) destructor must be virtual if any derived classes so much as
//  contain members which could allocate memory.  In this case, LassoSelector contains Stroke which contains
//  vector<Points>, which may dynamically allocate memory (to grow the vector).  Without a virtual destructor,
//  "delete (Selector*)selector" will call ~Selector() so the destructors for Stroke and hence vector<> never
//  get called, resulting in a memory leak.

class LassoSelector : public Selector
{
public:
  LassoSelector(Selection* sel, Dim simplify = 0);
  //void selectRect(Dim x0, Dim y0, Dim x1, Dim y1);
  void addPoint(Dim x, Dim y);
  bool selectHit(Element* s) override;
  //void shrink();
  Rect getBGBBox() override;
  void drawBG(Painter* painter) override;

private:
  Path2D lasso;
  Rect lassoBBox;

  int nextSimpStart = 0;
  Dim simplifyThresh;
  bool checkCollision = false;
  Point norm1, norm2, norm3;
  Dim txmin, txmax, tymin, tymax;
  Dim s1min, s1max, s2min, s2max, s3min, s3max;
  bool projectRect(const Point& p, const Rect& r, Dim smin, Dim smax);
  bool rectCollide(const Rect& r);
  //bool encloses(const PainterPath& path) const;
};

#endif // SELECTION_H
