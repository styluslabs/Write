#pragma once

#include "basics.h"
#include "usvg/svgnode.h"
#include "usvg/svgwriter.h"
#include "usvg/svgpainter.h"

#define MAX_LINE_NUM INT_MAX

typedef uint64_t UUID_t;

template<typename It>
class Range
{
  It b, e;
public:
  Range(It _b, It _e) : b(_b), e(_e) {}
  It begin() const { return b; }
  It end() const { return e; }
};

class ScribbleTransform : public Transform2D
{
public:
  // scaling for internal dimensions, e.g. stroke width
  Dim internalScale[2];

  ScribbleTransform(const Transform2D& tf = Transform2D(), Dim isx = 1, Dim isy = 1)
      : Transform2D(tf), internalScale{isx, isy} {}
  const Transform2D& tf() const { return *static_cast<const Transform2D*>(this); }

  bool isIdentity() const
      { return Transform2D::isIdentity() && internalScale[0] == 1 && internalScale[1] == 1; }
  ScribbleTransform inverse() const
      { return ScribbleTransform(Transform2D::inverse(), 1.0/internalScale[0], 1.0/internalScale[1]); }
  ScribbleTransform& reset() { *this = ScribbleTransform(); return *this; }

  friend ScribbleTransform operator*(const ScribbleTransform& a, const ScribbleTransform& b)
  {
    return ScribbleTransform(operator*(a.tf(), b.tf()),
      a.internalScale[0] * b.internalScale[0], a.internalScale[1] * b.internalScale[1]);
  }

  friend bool operator==(const ScribbleTransform& a, const ScribbleTransform& b)
  {
    return operator==(a.tf(), b.tf())
      && a.internalScale[0] == b.internalScale[0] && a.internalScale[1] == b.internalScale[1];
  }

  friend bool operator!=(const ScribbleTransform& a, const ScribbleTransform& b) { return !(a == b); }

  friend bool approxEq(const ScribbleTransform& a, const ScribbleTransform& b, Dim eps)
  {
    return approxEq(a.tf(), b.tf(), eps) && std::abs(a.internalScale[0] - b.internalScale[0]) < eps
      && std::abs(a.internalScale[1] - b.internalScale[1]) < eps;
  }
};

template<typename V>
class SvgExtIter {
public:
  typedef SvgExtIter<V> Self;
  typedef std::list<SvgNode*>::iterator NodeIter;
  // required iterator traits
  typedef V value_type;
  typedef ptrdiff_t difference_type;
  typedef V* pointer;
  typedef V& reference;
  typedef std::forward_iterator_tag iterator_category;

  SvgExtIter() {}
  SvgExtIter(const NodeIter& iterator) : nodeIter(iterator) {}
  SvgExtIter(const Self& other) : nodeIter(other.nodeIter) {}

  Self& operator=(const Self& other) { nodeIter = other.nodeIter; return *this;}

  bool operator==(const Self& other) const { return nodeIter == other.nodeIter; }
  bool operator!=(const Self& other) const { return nodeIter != other.nodeIter; }

  V operator*() const { return static_cast<V>((*nodeIter)->ext()); }
  V* operator->() const { return &(static_cast<V>((*nodeIter)->ext())); }

  Self& operator++() { ++nodeIter; return *this; }
  Self operator++(int) { Self result(*this); ++(*this); return result; }

private:
   NodeIter nodeIter;
};

class StrokeProperties {
public:
  // if either or both of fillColor or strokeColor is valid, color should not be
  Color color;
  Dim width;
  Color fillColor = Color::INVALID_COLOR;
  Color strokeColor = Color::INVALID_COLOR;

  StrokeProperties(Color c, Dim w) : color(c), width(w) {}

  // It should be an iterator over Element*
  template<typename It>
  static StrokeProperties get(It b, It e)
  {
    if(b == e)
      return StrokeProperties(Color::INVALID_COLOR, -1);
    StrokeProperties props = (*b)->getProperties();
    for(++b; b != e; ++b) {
      StrokeProperties p2 = (*b)->getProperties();
      if(p2.color.isValid() && p2.color != props.color)
        props.color = Color::INVALID_COLOR;
      if(p2.width > 0 && p2.width != props.width)
        props.width = -1;
      if(!props.color.isValid() && props.width == -1)
        return props;
    }
    return props;
  }
};

void setSvgFillColor(SvgNode* node, Color color);
void setSvgStrokeColor(SvgNode* node, Color color);

class Selection;
// TODO: try SvgExtIter<Element> (extra deref)
class Element;
typedef SvgExtIter<Element*> ElementIter;

struct PenPoint
{
  using PathCommand = Path2D::PathCommand;
  enum Direction { Entering = 0x1000, Leaving = 0x2000 };
  Point p;
  Point dr;
  unsigned int cmd;

  PenPoint(const Point& _p, const Point& _dr, unsigned int _cmd) : p(_p), dr(_dr), cmd(_cmd) {}
  bool moveTo() const { return (cmd & 0xFF) == PathCommand::MoveTo; }
  Direction intersection() const { return Direction(cmd & (Entering | Leaving)); }
};

class Element : public SvgNodeExtension
{
public:
  Element(SvgNode* n);
  Element* createExt(SvgNode* n) const override { ASSERT(0 && "Elements must be created explicitly");  return new Element(n); }
  Element* clone() const override { return new Element(*this); }

  Element* cloneNode() const;
  void deleteNode();

  const ScribbleTransform& pendingTransform() const { return m_pendingTransform; }
  void resetTransform() { applyTransform(m_pendingTransform.inverse()); m_pendingTransform.reset(); }
  void applyTransform(const ScribbleTransform& tf);
  void commitTransform();
  void scaleWidth(Dim sx_int, Dim sy_int);
  StrokeProperties getProperties() const;
  bool setProperties(const StrokeProperties& props);

  void setSelected(const Selection* l);
  bool isSelected(const Selection* l) const { return m_selection == l; }
  const Selection* selection() const { return m_selection; }

  bool isPathElement() const { return node->type() == SvgNode::PATH; }
  bool isBookmark() const { return node->hasClass("bookmark"); }
  bool isHyperRef() const;
  // all children of multi-stroke have Element exts; may extend to include bookmark groups later
  bool isMultiStroke() const { return isHyperRef(); }
  const char* href() const;
  void sethref(const char* s);

  Element* parent() const { return node->parent() ? static_cast<Element*>(node->parent()->ext()) : NULL; }
  SvgContainerNode* containerNode() const { return node->asContainerNode(); }
  Range<ElementIter> children() const;
  void addChild(Element* child);

  Rect bbox() const { return node->bounds(); }
  Point com() const { return m_com.isNaN() ? bbox().center() : m_com; }
  void setCom(const Point& p) { m_com = p; }
  Timestamp timestamp() const { return m_timestamp; }
  void setTimestamp(Timestamp t) { m_timestamp = t; }

  void setNodeId(const char* id) { node->setXmlId(id); }
  const char* nodeId() const { return node->xmlId(); }

  bool freeErase(const Point& prevpos, const Point& pos, Dim radius);
  std::vector<Element*> getEraseSubPaths();

  void updateFromNode();
  void serializeAttr(SvgWriter* writer) override;
  void applyStyle(SvgPainter* svgp) const override;

  UUID_t uuid;
  Point scratch;

  static bool ERASE_IMAGES;
  static bool DEBUG_DRAW_COM;
  static bool SVG_NO_TIMESTAMP;
  static bool FORCE_NORMAL_DRAW;
  static const char* STROKE_PEN_CLASS;
  static const char* FLAT_PEN_CLASS;
  static const char* ROUND_PEN_CLASS;
  static const char* CHISEL_PEN_CLASS;

protected:
  ~Element() override {}

private:
  std::vector<PenPoint> toPenPoints();
  void fromPenPoints(const std::vector<PenPoint>& pts);

  const Selection* m_selection;
  Timestamp m_timestamp;
  Point m_com;
  ScribbleTransform m_pendingTransform;
  bool m_applyPending = false;

  std::vector<PenPoint> penPoints;
};
