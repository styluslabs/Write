#include "element.h"
#include "basics.h"
#include "selection.h"
#include "usvg/svgparser.h"  // for toReal
#include "strokebuilder.h"  // for rebuilding stroke when scaling width


bool Element::ERASE_IMAGES = true;
bool Element::DEBUG_DRAW_COM = false;
bool Element::SVG_NO_TIMESTAMP = false;  // for ScribbleTest
bool Element::FORCE_NORMAL_DRAW = false;  // for bookmarks and thumbnail
const char* Element::STROKE_PEN_CLASS = "write-stroke-pen";
const char* Element::FLAT_PEN_CLASS = "write-flat-pen";
const char* Element::ROUND_PEN_CLASS = "write-round-pen";
const char* Element::CHISEL_PEN_CLASS = "write-chisel-pen";

Element::Element(SvgNode* n)
    : SvgNodeExtension(n), m_selection(NULL), m_timestamp(0), m_com(NaN, NaN)
{
  if(isMultiStroke()) {
    if(node->hasClass("hyperref")) {
      auto links = containerNode()->select("a");
      if(!links.empty() && links.back() != n) {
        SvgNode* a = links.back();
        node->setAttr(SvgAttr("xlink:href",
            a->getStringAttr("xlink:href", ""), SvgAttr::XMLSrc | SvgAttr::NoSerialize));
        containerNode()->removeChild(a);
        delete a;
      }
    }
    for(SvgNode* child : containerNode()->children()) {
      ASSERT(!child->hasExt() && "multi-stroke element must be created before child elements");
      new Element(child);
    }
  }
  updateFromNode();
}

Range<ElementIter> Element::children() const
{
  SvgContainerNode* g = containerNode();
  if(g)
    return Range<ElementIter>(ElementIter(g->children().begin()), ElementIter(g->children().end()));
  SCRIBBLE_LOG("children() called on non-structure node!");
  return Range<ElementIter>(ElementIter(), ElementIter());
}

void Element::addChild(Element* child)
{
  ASSERT(containerNode());
  containerNode()->addChild(child->node);
}

Element* Element::cloneNode() const
{
  SvgNode* newNode = node->clone();  // this clones extension (Element)
  Element* s = static_cast<Element*>(newNode->ext());
  s->setSelected(NULL); // clear any selection ... may only be used by Selection::toSorted() - do there instead?
  return s;
}

void Element::deleteNode()
{
  ASSERT(!node->parent() && "Cannot delete Element with parent!");
  delete node;  // deletes this (Element) since SvgNodeExtension is owned by SvgNode
}

StrokeProperties Element::getProperties() const
{
  if(isMultiStroke())
    return StrokeProperties::get(children().begin(), children().end());

  Color color = Color::INVALID_COLOR;
  Dim width = -1;
  // currently fill color will supercede stroke color
  color = node->getColorAttr("fill").mulAlphaF(node->getFloatAttr("fill-opacity", 1));
  if(color.alpha() == 0)
    color = node->getColorAttr("stroke").mulAlphaF(node->getFloatAttr("stroke-opacity", 1));
  Dim avgScale = node->hasTransform() ? node->getTransform().avgScale() : 1;
  width = node->getFloatAttr("stroke-width", -1) * avgScale;
  return StrokeProperties(color, width);
}

void setSvgFillColor(SvgNode* node, Color color)
{
  if(color.alpha() == 0)
    node->setAttr<color_t>("fill", Color::NONE);
  else {
    node->setAttr<color_t>("fill", color.opaque().color);
    if(color.alphaF() != node->getFloatAttr("fill-opacity", 1))
      node->setAttr<float>("fill-opacity", color.alphaF());
  }
}

void setSvgStrokeColor(SvgNode* node, Color color)
{
  if(color.alpha() == 0)
    node->setAttr<color_t>("stroke", Color::NONE);
  else {
    node->setAttr<color_t>("stroke", color.opaque().color);
    if(color.alphaF() != node->getFloatAttr("stroke-opacity", 1))
      node->setAttr<float>("stroke-opacity", color.alphaF());
  }
}

// we want to descend into any <g>s, not just multi-strokes
static bool applyProperties(const StrokeProperties& props, SvgNode* node)
{
  if(node->asContainerNode()) {
    for(SvgNode* child : node->asContainerNode()->children())
      applyProperties(props, child);
    //return true; ... let's also set style on container
  }

  if(props.color.alpha() > 0) {
    // do not set if current color is none or not set
    if(node->getColorAttr("fill", Color::NONE) != Color::NONE)
      setSvgFillColor(node, props.color);
    if(node->getColorAttr("stroke", Color::NONE) != Color::NONE)
      setSvgStrokeColor(node, props.color);
  }
  if(props.fillColor.isValid())
    setSvgFillColor(node, props.fillColor);
  if(props.strokeColor.isValid())
    setSvgStrokeColor(node, props.strokeColor);
  Dim sw = node->getFloatAttr("stroke-width", 0);
  if(props.width > 0 && sw > 0) {
    Dim avgScale = node->hasTransform() ? node->getTransform().avgScale() : 1;
    Dim scale = props.width/(sw * avgScale);
    if(scale != 1) {
      if(node->hasExt())
        static_cast<Element*>(node->ext())->scaleWidth(scale, scale);
      else
        node->setAttr<float>("stroke-width", sw * scale);
    }
  }
  // TODO: return false if no change to element's properties
  return true;
}

bool Element::setProperties(const StrokeProperties& props)
{
  // applyProperties will only set color if fill or stroke is already present, so make sure one is!
  if(props.color.alpha() > 0 && !node->getAttr("fill") && !isMultiStroke())
    setSvgFillColor(node, Color::BLACK);
  return applyProperties(props, node);
}

static bool erasePenPoints(std::vector<PenPoint>& in, const std::vector<Point>& clip)
{
  //SCRIBBLE_LOG("\n\nStart erase\n");
  //std::vector<PenPoint> in = subject;
  std::vector<PenPoint> out;
  // iterate over edges of clip polygon
  for(size_t cp1 = clip.size() - 1, cp2 = 0; cp2 < clip.size(); cp1 = cp2++) {
    out.reserve(in.size());
    // iterate over edges of subject polygon
    out.push_back(in[0]);
    for(size_t ii = 1; ii < in.size(); ++ii) {
      if(!in[ii].moveTo()) {
        // would this make a difference: Rect::corners(cp1, cp2).intersects(Rect::corners(ii-1, ii))?
        Point x = segmentIntersection(clip[cp1], clip[cp2], in[ii-1].p, in[ii].p);
        if(!x.isNaN()) {
          auto dir = cross(clip[cp2] - clip[cp1], in[ii].p - in[ii-1].p) > 0 ? PenPoint::Entering : PenPoint::Leaving;
          Dim t = (x - in[ii-1].p).dist() / (in[ii].p - in[ii-1].p).dist();
          Point dr = (t*in[ii].dr.dist() + (1-t)*in[ii-1].dr.dist()) * normal(in[ii].p - in[ii-1].p);
          out.emplace_back(x, dr, (Path2D::LineTo | dir));
        }
      }
      out.push_back(in[ii]);
    }
    out.swap(in);
    out.clear();
  }
  // no intersections doesn't mean not touched (e.g. subpath entirely inside erase path, possibly with other
  //  subpaths entirely outside)
  bool touched = false;
  out.reserve(in.size());
  for(size_t ii = 0; ii < in.size();) {
    // determine initial state of erase by finding first intersection; if no intersection, erase iff all
    //  points inside clip
    bool erase = pointInPolygon(clip, in[ii].p);
    for(size_t jj = ii+1; jj < in.size() && !in[jj].moveTo(); ++jj) {
      if(in[jj].intersection()) {
        erase = in[jj].intersection() == PenPoint::Leaving;
        break;
      }
      erase = erase && pointInPolygon(clip, in[jj].p);
    }

    do {
      if(in[ii].intersection()) {
        // inside -> outside: moveTo; outside -> inside: lineTo; note that we clear intersect flag
        out.emplace_back(in[ii].p, in[ii].dr, erase ? Path2D::MoveTo : Path2D::LineTo);
        erase = in[ii].intersection() == PenPoint::Entering;  //!erase;
      }
      else if(!erase)
        out.push_back(in[ii]);
      touched = touched || erase;
    } while(++ii < in.size() && !in[ii].moveTo());
  }
  out.swap(in);
  return touched;
}

// rebuild path from PenPoints
void Element::fromPenPoints(const std::vector<PenPoint>& pts)
{
  Path2D& path = *static_cast<SvgPath*>(node)->path();
  path.clear();
  // caller is expected to handle dirty rect/invalidating bounds
  //node->invalidate(false);

  // we use reserve for getEraseSubPaths, for which path initially has zero capacity
  if(node->hasClass(FLAT_PEN_CLASS)) {
    path.reserve(2*pts.size());
    for(size_t ii = 0; ii < pts.size();) {
      size_t ii0 = ii;
      path.moveTo(pts[ii].p + pts[ii].dr/2);
      for(++ii; ii < pts.size() && !pts[ii].moveTo(); ++ii)
        path.lineTo(pts[ii].p + pts[ii].dr/2);
      for(size_t jj = ii; jj-- > ii0;)
        path.lineTo(pts[jj].p - pts[jj].dr/2);
    }

    //int m = 0;
    //int n = path->isSimple() ? path->size() : 1;
    //for(; n <= path->size(); ++n) {
    //  if(n == path->size() || path->command(n) == PainterPath::MoveTo) {
    //    for(int ii = m, jj = n - 1; ii < jj; ++ii, --jj) {
    //      // recalculate points - compensate for overall scaling to keep stroke width constant
    //      Point center = 0.5*(points[ii] + points[jj]);
    //      Point dr = points[ii] - points[jj];
    //      Point dp(0.5*sx_int*dr.x, 0.5*sy_int*dr.y);
    //      points[ii] = center + dp;
    //      points[jj] = center - dp;
    //    }
    //    m = n;
    //  }
    //}
  }
  else if(node->hasClass(ROUND_PEN_CLASS)) {
    path.reserve(8*pts.size());
    for(size_t ii = 1; ii < pts.size(); ++ii) {
      if(!pts[ii].moveTo())
        FilledStrokeBuilder::addRoundSubpath(path, pts[ii-1].p, std::min(Dim(200), pts[ii-1].dr.dist()),
            pts[ii].p, std::min(Dim(200), pts[ii].dr.dist()));
    }
  }
  else if(node->hasClass(CHISEL_PEN_CLASS)) {
    path.reserve(7*pts.size());
    for(size_t ii = 1; ii < pts.size(); ++ii) {
      if(!pts[ii].moveTo())
        FilledStrokeBuilder::addChiselSubpath(path, pts[ii-1].p, std::min(Dim(200), pts[ii-1].dr.dist()),
            pts[ii].p, std::min(Dim(200), pts[ii].dr.dist()));
    }
  }
  else if(node->hasClass(STROKE_PEN_CLASS)) { // if(hasStroke)
    path.reserve(pts.size());
    for(size_t ii = 0; ii < pts.size(); ++ii)
      path.addPoint(pts[ii].p, Path2D::PathCommand(pts[ii].cmd));
  }
}

// storing penPoints in Element might help avoid any numerical issues caused by repeated scaling operations
std::vector<PenPoint> Element::toPenPoints()
{
  const Path2D& path = *static_cast<SvgPath*>(node)->path();
  std::vector<PenPoint> pts;

  if(node->hasClass(FLAT_PEN_CLASS) || node->hasClass("write-fstroke")) {
    pts.reserve(path.size()/2);
    for(int ii = 0, jj = path.size() - 1; ii < jj; ++ii, --jj) {
      Point p = (path.point(ii) + path.point(jj))/2;
      Point dr = path.point(ii) - path.point(jj);
      pts.emplace_back(p, dr, ii == 0 ? Path2D::MoveTo : Path2D::LineTo);
    }
  }
  else if(node->hasClass(ROUND_PEN_CLASS)) {
    pts.reserve(path.size()/8 + 1);  // very conservative
    int ii = 0;
    while(1) {
      while(ii < path.size() && path.command(ii) != Path2D::MoveTo) ++ii;
      if(ii >= path.size()) break;
      Point pt1a = path.point(ii);
      while(ii < path.size() && path.command(ii) != Path2D::LineTo) ++ii;  // skip to end of curve
      if(ii >= path.size()) break;
      Point pt1 = 0.5*(pt1a + path.point(ii-1));  // endpoint of curve
      Point dr1 = path.point(ii-1) - pt1a;
      Point pt2a = path.point(ii);
      ++ii;
      while(ii < path.size() && path.command(ii) != Path2D::LineTo) ++ii;  // skip to end of curve
      if(ii >= path.size()) break;
      Point pt2 = 0.5*(pt2a + path.point(ii-1));  // endpoint of curve
      Point dr2 = path.point(ii-1) - pt2a;
      if(pts.empty())
        pts.emplace_back(pt1, dr1, Path2D::MoveTo);
      pts.emplace_back(pt2, dr2, Path2D::LineTo);
    }
  }
  else if(node->hasClass(CHISEL_PEN_CLASS)) {
    pts.reserve(path.size()/7 + 1);
    // for the round pen, we avoid assumptions about how exactly the arc is constructed, but for chisel pen
    //  we just read points directly
    Point p = (path.point(0) + path.point(2))/2;
    Point dr(0, (path.point(2) - path.point(0)).y);
    pts.emplace_back(p, dr, Path2D::MoveTo);
    for(int ii = 0; ii < path.size(); ii += 7) {
      p = (path.point(ii+3) + path.point(ii+5))/2;
      dr.y = (path.point(ii+5) - path.point(ii+3)).y;
      pts.emplace_back(p, dr, Path2D::LineTo);
    }
  }
  else if(node->hasClass(STROKE_PEN_CLASS)) {
    pts.reserve(path.size());
    for(int ii = 0; ii < path.size(); ++ii)
      pts.emplace_back(path.point(ii), Point(0,0), path.command(ii)); // == PainterPath::MoveTo);
  }
  return pts;
}

// we assume caller has already detemined that our bbox intersects eraser bbox
bool Element::freeErase(const Point& prevpos, const Point& pos, Dim radius)
{
  bool touched = false;

  if(isMultiStroke()) {
    for(Element* s : children())
      touched = s->freeErase(prevpos, pos, radius) || touched;
  }
  else if(isPathElement()) {
    Path2D eraser;
    Point dr = pos == prevpos ? Point(1, 0) : (pos - prevpos).normalize();
    Point n = normal(dr);
    eraser.moveTo(prevpos + radius*(-dr + n));
    eraser.lineTo(prevpos + radius*(-dr - n));
    eraser.lineTo(pos + radius*(dr - n));
    eraser.lineTo(pos + radius*(dr + n));
    eraser.closeSubpath();
    eraser.transform(node->getTransform().inverse());
    if(polygonArea(eraser.points) > 0)
      std::reverse(eraser.points.begin(), eraser.points.end());

    if(penPoints.empty())
      penPoints = toPenPoints();
    if(!penPoints.empty())
      touched = erasePenPoints(penPoints, eraser.points);
    if(touched) {
      fromPenPoints(penPoints);
      // for wide stroke, dirty area may be larger than eraser bbox, so we just have to dirty whole stroke
      node->invalidate(false);  //node->invalidateBounds(false);  // we assume caller will take care of dirty rect
    }
  }
  else if(node->type() == SvgNode::IMAGE) {
    SvgImage* imagenode = static_cast<SvgImage*>(node);
    if(!ERASE_IMAGES || !imagenode->m_linkStr.empty())  // can't modify referenced images
      return false;
    //Painter painter(img);
    //painter.setPen(Brush(Color::NONE));
    //painter.setBrush(Brush(Color::BLACK));
    //painter.setCompositionMode(Painter::CompositionMode_Clear);
    //PainterPath eraser;
    //eraser.addEllipse(pos.x, pos.y, radius, radius);
    //painter.beginFrame();
    //painter.drawImage(Rect::wh(img->width, img->height), *img);
    //painter.drawPath(eraser);
    //painter.endFrame();

    Image* image = imagenode->image();
    Rect bounds = imagenode->viewport(); //bounds();
    unsigned int* pixels = image->pixels();

    const int hpixels = image->width;
    const int vpixels = image->height;
    const Dim sx = bounds.width()/hpixels;
    const Dim sy = bounds.height()/vpixels;

    Transform2D tf = node->getTransform() * Transform2D(sx, 0, 0, sy, bounds.left, bounds.top);

    const Dim radius2 = radius*radius;
    // translate pos to a pixel position
    const Rect r = tf.inverse().mapRect(Rect::corners(prevpos, pos).pad(radius));
    const int x0 = std::max(int(r.left + 0.5), 0);
    const int x1 = std::min(int(r.right + 0.5), hpixels-1);
    const int y0 = std::max(int(r.top + 0.5), 0);
    const int y1 = std::min(int(r.bottom + 0.5), vpixels-1);
    for(int y = y0; y <= y1; y++) {
      for(int x = x0; x <= x1; x++)
        // only clear alpha ... clearing RGB too (to black) gives ugly border due to GL_LINEAR interpolation
        if(distToSegment2(prevpos, pos, tf.map(Point(x + 0.5, y + 0.5))) < radius2)  //Point((x + 0.5)/sx + bounds.left, (y + 0.5)/sy + bounds.top)) < radius2)
          pixels[y*hpixels + x] &= ~Color::A;
    }
    touched = true;

    // we assume caller will set dirty rect, so we don't have to redraw whole image
    //node->setDirty(SvgNode::PIXELS_DIRTY);
    image->invalidate();
  }
  return touched;
}

std::vector<Element*> Element::getEraseSubPaths()
{
  if(isMultiStroke()) {
    Element* c = cloneNode();
    // delete children of copy
    while(!c->containerNode()->children().empty()) {
      SvgNode* node0 = c->containerNode()->children().front();
      c->containerNode()->removeChild(node0);
      delete node0;
    }

    for(Element* s : children()) {
      for(Element* t : s->getEraseSubPaths())
        c->addChild(t);
    }
    if(c->containerNode()->children().empty()) {
      c->deleteNode();
      return {};
    }
    return {c};
  }

  if(!isPathElement() || penPoints.empty())
    return {};
  // don't copy path when cloning!
  static_cast<SvgPath*>(node)->path()->clear();
  node->invalidate(false);

  std::vector<Element*> pieces;
  std::vector<PenPoint> pts;
  pts.swap(penPoints);
  auto a = pts.begin();
  for(auto b = a; b != pts.end();) {
    if(++b == pts.end() || b->moveTo()) {
      Element* s = cloneNode();
      s->penPoints.assign(a, b);
      a = b;
      s->fromPenPoints(s->penPoints);
      // if we set com to NaN, we have to do removeAttr("__comx/y")
      s->setCom(s->bbox().center());  //Point(NaN, NaN));
      pieces.push_back(s);
    }
  }
  return pieces;
}

bool Element::isHyperRef() const
{
  // && static_cast<SvgG*>(node)->groupType == SvgNode::A
  return node->type() == SvgNode::G && node->hasClass("hyperref");
}

const char* Element::href() const
{
  if(!isHyperRef())
    SCRIBBLE_LOG("Warning: requesting href for invalid node!");
  //SvgNode* a = structureNode()->selectFirst("a");
  // SVG 2 spec states href overrides xlink:href, so maybe we should reverse this
  const char* s = node->getStringAttr("xlink:href");
  if(!s)
    s = node->getStringAttr("href");
  return s;
}

void Element::sethref(const char* s)
{
  if(!isHyperRef())
    SCRIBBLE_LOG("Warning: sethref() called on invalid node!");
  node->setAttr(SvgAttr("xlink:href", s, SvgAttr::XMLSrc | SvgAttr::NoSerialize));
  //node->setAttribute("xlink:href", s);
  //if(s[0] != '#')
  //  node->setAttribute("target", "_top");
}

void Element::setSelected(const Selection* l)
{
  auto olddraw = m_selection ? m_selection->drawType() : Selection::STROKEDRAW_NORMAL;
  auto newdraw = l ? l->drawType() : Selection::STROKEDRAW_NORMAL;
  m_selection = l;
  if(newdraw == Selection::STROKEDRAW_NONE || olddraw == Selection::STROKEDRAW_NONE)
    node->setDisplayMode(newdraw == Selection::STROKEDRAW_NONE ? SvgNode::AbsoluteMode : SvgNode::BlockMode);
  else if(olddraw != newdraw)
    node->setDirty(SvgNode::PIXELS_DIRTY);
}

void Element::scaleWidth(Dim sx_int, Dim sy_int)
{
  if(!isPathElement())
    return;

  node->invalidate(false);
  Dim sw = node->getFloatAttr("stroke-width", 1);
  node->setAttr<float>("stroke-width", sw * std::sqrt(std::abs(sx_int * sy_int)));
  // nothing else to do for stroked element
  if(node->getColorAttr("stroke", Color::NONE) != Color::NONE)
    return;

  // this will be a no-op except for known Write path types
  if(penPoints.empty())
    penPoints = toPenPoints();
  for(PenPoint& p : penPoints) {
    p.dr.x *= sx_int;
    p.dr.y *= sy_int;
  }
  if(!penPoints.empty())
    fromPenPoints(penPoints);
}

void Element::applyTransform(const ScribbleTransform& tf)
{
  if(tf.isIdentity())
    return;
  m_pendingTransform = tf * m_pendingTransform;
  m_applyPending = false;  // will be set to true if none of special cases below apply
  if(!m_com.isNaN())
    m_com = tf.map(m_com);
  node->invalidate(true);
  // we've redefined internal scale to mean additional scale on top of scale from tf
  Dim sx_int = tf.internalScale[0];
  Dim sy_int = tf.internalScale[1];

  if(isMultiStroke()) {
    for(Element* s : children())
      s->applyTransform(tf);
  }
  else if(node->type() == SvgNode::IMAGE && (sx_int == 0 || sy_int == 0 || std::isinf(sx_int) || std::isinf(sy_int))) {
    // internalScale == 0 or inf indicates crop
    SvgImage* svgimg = static_cast<SvgImage*>(node);
    if(!svgimg->getTransform().isIdentity()) {
      svgimg->m_bounds = svgimg->getTransform().mapRect(svgimg->m_bounds);
      svgimg->setTransform(Transform2D());
    }
    // get transform mapping viewport to srcRect
    Dim sx = svgimg->srcRect.width()/svgimg->m_bounds.width();
    Dim sy = svgimg->srcRect.height()/svgimg->m_bounds.height();
    Transform2D srctf = Transform2D::translating(-svgimg->m_bounds.origin()).scale(sx, sy).translate(svgimg->srcRect.origin());
    //Rect testr = srctf.mapRect(svgimg->m_bounds);
    //Transform2D srctf(sx, 0, 0, sy, (1 - sx)*scaleOrigin.x, (1 - sy)*scaleOrigin.y);
    svgimg->m_bounds = tf.mapRect(svgimg->m_bounds);
    svgimg->srcRect = srctf.mapRect(svgimg->m_bounds);
  }
  else if(isPathElement() && !tf.isRotating() && !(approxEq(sx_int, 1, 1E-7) && approxEq(sy_int, 1, 1E-7))) {
    SvgPath* pathnode = static_cast<SvgPath*>(node);
    // we assume rotation is never combined with scaling
    // this is basically a hack that assumes this case can only happen via Selection
    Transform2D pttf = node->getTransform().inverse() * tf.tf() * node->getTransform();
    if(node->hasClass(FLAT_PEN_CLASS) || node->hasClass(ROUND_PEN_CLASS) || node->hasClass(CHISEL_PEN_CLASS)) {
      if(penPoints.empty())
        penPoints = toPenPoints();
      for(PenPoint& p : penPoints) {
        p.p = pttf.map(p.p);
        if(tf.yscale() < 0)  // note x flipped for y scale < 0 (and y for x scale < 0)
          p.dr.x = -p.dr.x;
        if(tf.xscale() < 0)
          p.dr.y = -p.dr.y;
      }
      if(!penPoints.empty())
        fromPenPoints(penPoints);
    }
    else  // this will apply to any stroked path ... limit to STROKE_PEN_CLASS?
      pathnode->path()->transform(pttf);
    if(tf.xscale() != tf.yscale() && pathnode->pathType() == SvgNode::CIRCLE)
      pathnode->m_pathType = SvgNode::PATH;
  }
  else
    m_applyPending = true;
}

void Element::commitTransform()
{
  if(m_pendingTransform.isIdentity())
    return;
  if(isMultiStroke()) {
    for(Element* s : children())
      s->commitTransform();
  }
  else if(m_applyPending)
    node->setTransform(m_pendingTransform.tf() * node->getTransform());
  m_pendingTransform.reset();
  m_applyPending = false;
}

// the normal and preferred way to handle attributes not subject to CSS and that we wish to store directly in
//  ext is to provide createExt to SvgParser, but for legacy reasons we will instead read from string
//  attribute and update string attribute when serializing
void Element::updateFromNode()
{
  m_com.x = toReal(node->getStringAttr("__comx"), m_com.x);
  m_com.y = toReal(node->getStringAttr("__comy"), m_com.y);
  const char* ts = node->getStringAttr("__timestamp");
  if(ts && ts[0])
    m_timestamp = strtoull(ts, NULL, 0);
}

void Element::serializeAttr(SvgWriter* writer)
{
  if(isHyperRef()) {
    const char* s = href();
    SvgG* a = new SvgG(SvgNode::A);
    a->setAttr("xlink:href", s);
    // add target="_top" since html <object>s are considered iframes, so default behavior would be to only
    //  replace <object> content rather than navigate (whole tab/window) to link target
    if(s && s[0] != '#')
      a->setAttr("target", "_top");
    // transforms are propagated to children on multi-stroke, so this should never actually happen
    if(node->hasTransform())
      a->setTransform(node->getTransform().inverse());
    SvgNode* r = new SvgRect(bbox());
    // we should probably just use opacity="0"
    r->setAttr("fill-opacity", 0.0f);
    r->setAttr("stroke-opacity", 0.0f);
    a->addChild(r);
    containerNode()->addChild(a);
    writer->tempNodes.push_back(a);
  }
  if(!m_com.isNaN()) {
    node->setAttr<Dim>("__comx", m_com.x);
    node->setAttr<Dim>("__comy", m_com.y);
  }
  if(m_timestamp > 0 && !SVG_NO_TIMESTAMP)
    node->setAttr("__timestamp",
        fstring("0x%x%08x", uint32_t(m_timestamp >> 32), uint32_t(m_timestamp)).c_str());
}

void Element::applyStyle(SvgPainter* svgp) const
{
  Painter* painter = svgp->p;
  if(m_applyPending && !m_pendingTransform.isIdentity()) {
    // ideally, we would not have to get inverse of existing transform
    Transform2D tf = node->totalTransform();
    if(!tf.isIdentity())
      painter->transform(tf.inverse() * m_pendingTransform.tf() * tf);
    else
      painter->transform(m_pendingTransform);
  }

  // rulelines shouldn't get bigger when we zoom in (one reason - so they don't look ugly at zooms slightly
  //  above 100%) but should get smaller when we zoom out; move this to a dedicated subclass for rule node?
  if(node->hasClass("write-scale-down")) {
    // because of sqrt, avgScale shows up in profiling, so only compute if necessary
    Dim avgScale = painter->getTransform().avgScale();
    if(avgScale < 1)
      svgp->extraState().strokeOpacity *= avgScale*avgScale;
  }

  // selection draw style must be applied to every graphic node individually, so we must ascend to see if any
  //  parent is selected; valid dirtyRect indicates we are drawing, as opposed to calculating bounds
  if(isPathElement() && svgp->dirtyRect.isValid()) {
    const Selection* sel = selection();
    Element* parent = this->parent();
    while(!sel && parent) {
      sel = parent->selection();
      parent = parent->parent();
    }
    if(sel && sel->drawType() == Selection::STROKEDRAW_SEL && !FORCE_NORMAL_DRAW) {
      Dim width = painter->strokeWidth();
      bool hasFill = !painter->fillBrush().isNone();
      bool hasStroke = !painter->strokeBrush().isNone();
      Dim avgScale = painter->getTransform().avgScale();
      if(hasFill && !hasStroke) {
        Color color = painter->fillBrush().color().setAlphaF(svgp->extraState().fillOpacity);
        painter->setStrokeBrush(color);
        painter->setStrokeWidth(width > 0 && width*avgScale < 2 ? width : 2/avgScale);
        painter->setFillBrush(Color::NONE);
        painter->drawPath(*static_cast<SvgPath*>(node)->path());
        painter->setStrokeBrush(Color::NONE);
        painter->setFillBrush(Color::WHITE);
        svgp->extraState().fillOpacity = 1;  // opaque to cover internal structure (subpaths)
      }
      else if(hasStroke && !hasFill) {
        // Stroke once with normal color and width + 2, again with white
        painter->setStrokeWidth(width + 2/avgScale);
        painter->drawPath(*static_cast<SvgPath*>(node)->path());
        painter->setStrokeBrush(Color::WHITE);
        painter->setStrokeWidth(width);
      }
    }
  }

#if IS_DEBUG
  if(DEBUG_DRAW_COM && svgp->dirtyRect.isValid() && !m_com.isNaN()) {
    Dim avgScale = painter->getTransform().avgScale();
    painter->save();
    painter->setStrokeBrush(Color::NONE);
    painter->setFillBrush(Color(255, 128, 0));

    Point com = m_pendingTransform.inverse().map(m_com);
    if(node->hasTransform())
      com = node->getTransform().inverse().map(com);

    painter->drawPath(Path2D().addEllipse(com.x, com.y, 2/avgScale, 2/avgScale));
    painter->restore();
  }
#endif
}
