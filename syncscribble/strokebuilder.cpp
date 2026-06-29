#include "strokebuilder.h"
#include "scribbleapp.h"
#include "shaperecognizer.h"

// removePoints() doesn't really work except for removing all points
// what about post-processing filters?  StrokeBuilder saves list all the points it gets and feeds them to
//  chain of post-processing filters in its finalize

// TODO: we shouldn't recalc filter coeffs for every stroke; instead, filters should be persistant (inited
//  in ScribbleArea::prefsChanged()) and reset themselves in finalize()

StrokeBuilder::~StrokeBuilder()
{
  while(firstProcessor != this) {
    InputProcessor* proc = firstProcessor->next;
    delete firstProcessor;
    firstProcessor = proc;
  }
  if(element)  // for, e.g, PenPreview, which doesn't finish()
    element->deleteNode();
}

// this gives up ownership of Element!
Element* StrokeBuilder::finish()
{
  firstProcessor->finalize();
  Element* elem = element;
  element = NULL;
  return elem;
}

// we take ownership of the processor
void StrokeBuilder::addFilter(InputProcessor* p)
{
  p->next = firstProcessor;
  firstProcessor = p;
}

// needed for Page::migrateLegacyDoc()
Point StrokeBuilder::calcCom(SvgNode* node, Path2D* path)
{
  if(path->empty()) return Point(NaN, NaN);
  // stroke->getBBox() does not include stroke width for stroked paths but does (unavoidably) for filled
  //  paths, so let's just always include stroke width
  Rect bbox = node->bounds();  //stroke->getBBox()
  Dim comy = 0;
  // presently, the only thing to do is calculate the COM of the stroke
  // we'll lose precision if there are many points ... probably not a big deal
  for(int ii = 0; ii < path->size(); ++ii)
    comy += path->point(ii).y;
  // for y, give 1/3 weight each to com, first point, and bbox top
  // just setting com = point[0] is almost perfect, except for capital letters
  // this will probably end up being something user configures according to his or her writing style
  // The biggest improvement to setting COM is the grouping of strokes as they're written (in ScribbleArea)
  return Point(bbox.center().x, (comy/path->size() + path->point(0).y + bbox.top)/3.0);
}

void StrokeBuilder::finalize()
{
  if(!stroke->empty())
    element->setCom(calcCom(element->node, stroke));
}

StrokeBuilder* StrokeBuilder::create(const ScribblePen& pen)
{
  if(pen.hasVarWidth() || pen.hasFlag(ScribblePen::TIP_CHISEL))
    return new FilledStrokeBuilder(pen);
  else
    return new StrokedStrokeBuilder(pen);
}

// builder for stroked (non-pressure sensitive) stroke

StrokedStrokeBuilder::StrokedStrokeBuilder(const ScribblePen& pen) : width(pen.width)
{
  SvgPath* svgPath = new SvgPath();
  stroke = svgPath->path();
  svgPath->setAttr<color_t>("fill", Color::NONE);
  setSvgStrokeColor(svgPath, pen.color);
  svgPath->setAttr<float>("stroke-width", pen.width);
  // only round cap and join give decent results with freehand input
  // should we consider setting stroke-linecap/-linejoin on content <g> instead of individual strokes?
  auto cap = pen.hasFlag(ScribblePen::TIP_FLAT) ? Painter::FlatCap : Painter::RoundCap;
  auto join = pen.hasFlag(ScribblePen::TIP_FLAT) ? Painter::MiterJoin : Painter::RoundJoin;
  svgPath->setAttr<int>("stroke-linecap", cap);
  svgPath->setAttr<int>("stroke-linejoin", join);
  if(pen.dash > 0) {
    std::string dashes(pen.gap > 0 ? fstring("%f %f", pen.dash, pen.gap) : fstring("%f", pen.dash));
    svgPath->setAttribute("stroke-dasharray", dashes.c_str());  // dasharray is special, so let parser handle
  }
  svgPath->addClass(Element::STROKE_PEN_CLASS);
  element = new Element(svgPath);
}

void StrokedStrokeBuilder::addPoint(const StrokePoint& pt)
{
  if(stroke->empty())
    stroke->moveTo(pt.x, pt.y);
  else if(stroke->size() == 2 && stroke->point(0) == stroke->point(1))
    stroke->resize(1);
  stroke->lineTo(pt.x, pt.y);
  dirty.rectUnion(stroke->rpoint(2));
  dirty.rectUnion(stroke->rpoint(1));
  element->node->invalidate(false);
}

void StrokedStrokeBuilder::removePoints(int n)
{
  int newsize = (n < 0 || n >= stroke->size()) ? 0 : stroke->size() - n;
  // this complicates line drawing, so disable for now
  //if(n != 0 && stroke->size() == 2 && stroke->point(0) == stroke->point(1)) newsize = 0;
  for(int ii = std::max(0, newsize - 1); ii < stroke->size(); ++ii)
    dirty.rectUnion(stroke->point(ii));
  stroke->resize(newsize);
  element->node->invalidate(false);
}

Rect StrokedStrokeBuilder::getDirty()
{
  Rect r = dirty.pad(width/2);
  dirty = Rect();
  return r;
}

// should we have a builder for filled shape? what fill rule? previous code removed 2 May 2019
// - dirtied area is triangle formed by last two points + first point

/*
Builder for FilledStroke

To support variable pressure (and other pen tips such as chisel tip for highlighter), we use "filled strokes",
i.e., we calculate the filled path directly.  This seems to work perfectly for constant width (pressure) and
segments longer than width - but usually neither of these hold for actual usage.  The results are still
acceptable for sufficiently small pen widths (< ~4 Dim), and require only 2x more path points than a simple
stroke.

For wider pen widths, we use a separate subpath for each segment; this greatly simplifies the calculation
and ensures that moving the pen is purely additive, i.e., adding points to the stroke never uncovers a
previously covered pixel.  It also has slightly better rendering performance w/ my nanovg-2 backend.
... unfortunately, we get AA artifacts with nanovg-2 due to overlapping subpaths

In general, both these cases will produce self-intersecting paths (intersection of separate subpaths counts
as path intersection) and we are relying on non-zero winding fill (as opposed to even-odd fill) to effectively
cover the "defects" - but of course the pen's path could always cross itself, so we need the behavior anyway
and there seems to be minimal benefit to doing an exact path union calculation just to remove intersections
from our fill calculation.  Qt's qstroker.cpp appears to use the same trick (vs. exact calculation of stroke)

Tried a few different fns mapping pressure to width, I think the current one gives good results, esp. with
 pressureparam = 2 (1/pressureparam is displayed to user so that higher value means greater sensitivity).
 I think most applications use a linear mapping (equiv to pressureparam = 1).  Quill definitely does
 Nice example: D:\temp\Scribble\samples\IMAG0199.jpg and IMAG0200.jpg
Pressure should be normalized to range of [0, 1]

Square end caps (flat pen): originally decided against these, but with adjustment preventing large changes in
 width over short distances, caps do seem to help supress artifacts from abrupt change in direction near ends
 of stroke.
*/

FilledStrokeBuilder::FilledStrokeBuilder(const ScribblePen& _pen) : pen(_pen)
{
  SvgPath* svgPath = new SvgPath();
  stroke = svgPath->path();
  setSvgFillColor(svgPath, _pen.color);
  svgPath->setAttr<float>("stroke-width", pen.width);
  element = new Element(svgPath);
  style = _pen.hasFlag(ScribblePen::TIP_CHISEL) ? Chisel : (_pen.hasFlag(ScribblePen::TIP_ROUND) ? Round : Flat);
  if(style == Chisel)
    svgPath->addClass(Element::CHISEL_PEN_CLASS);
  else if(style == Round)
    svgPath->addClass(Element::ROUND_PEN_CLASS);
  else
    svgPath->addClass(Element::FLAT_PEN_CLASS);
}

static void addArc(Path2D& path, Dim w, const Point& pc, const Point& p0, const Point& p1, bool ccw)
{
  if(p0 == p1)
    return;
  Dim sweep = calcAngle(p0, pc, p1);
  if(ccw && sweep > 0)
    sweep -= 2*M_PI;
  else if(!ccw && sweep < 0)
    sweep += 2*M_PI;
  if(std::abs(sweep) > degToRad(180.01))
    PLATFORM_LOG("Sweep angle: %f", sweep);
  // if arc distance is short (e.g. less than half Dim), just draw a line
  if(std::abs(sweep)*w/2 < 0.5) {
    path.lineTo(p1);
    return;
  }
  Dim startangle = calcAngle(Point(pc.x + w, pc.y), pc, p0);
  path.addArc(pc.x, pc.y, w/2, w/2, startangle, sweep);
}

void FilledStrokeBuilder::addRoundSubpath(Path2D& path, Point pt1, Dim w1, Point pt2, Dim w2)
{
  Point d12 = pt1 == pt2 ? Point(1, 0) : pt2 - pt1;
  Point n12 = normal(d12);

  path.moveTo(pt1 - n12*w1/2);
  addArc(path, w1, pt1, pt1 - n12*w1/2, pt1 + n12*w1/2, true);
  path.lineTo(pt2 + n12*w2/2);
  addArc(path, w2, pt2, pt2 + n12*w2/2, pt2 - n12*w2/2, true);
  path.closeSubpath();
}

void FilledStrokeBuilder::addChiselSubpath(Path2D& path, Point pt1, Dim w1, Point pt2, Dim w2)
{
  // w is actually vertical extent and h is horizontal extent
  Dim h1 = w1/CHISEL_ASPECT_RATIO, h2 = w2/CHISEL_ASPECT_RATIO;
  Dim sgnx = pt2.x > pt1.x ? 1 : -1;
  Dim sgny = pt2.y < pt1.y ? 1 : -1;
  path.moveTo(pt1.x - sgny*h1/2, pt1.y - sgnx*w1/2);
  path.lineTo(pt1.x - sgnx*h1/2, pt1.y + sgny*w1/2);
  path.lineTo(pt1.x + sgny*h1/2, pt1.y + sgnx*w1/2);
  path.lineTo(pt2.x + sgny*h2/2, pt2.y + sgnx*w2/2);
  path.lineTo(pt2.x + sgnx*h2/2, pt2.y - sgny*w2/2);
  path.lineTo(pt2.x - sgny*h2/2, pt2.y - sgnx*w2/2);
  path.closeSubpath();
}

// we could consider spliting FilledStrokeBuilder into separate classes for Flat, Round, and Chisel pens, but
//  there is considerable commonaility between Round and Chisel cases, so we'd maybe need a common base class
//  for those, and the initial setup code is shared by all ... so let's wait and see if we add a 4th style
void FilledStrokeBuilder::addPoint(const StrokePoint& pt)
{
  Point pt2 = Point(pt.x, pt.y);
  Point pt1 = points.empty() ? pt2 : points.back();
  Point pt0 = points.size() > 1 ? points[points.size() - 2] : pt1;

  static Dim holdThreshold = 0.6; // Should be dynamic so that while using a pen or a finger, the threshold is larger
  if (points.empty()) {
    holdStartTime = 0;
  } else if(holdStartTime == 0 || pt2.dist(pt1) > holdThreshold) {
    holdStartTime = pt.t;
  }
  //printf("p.t: %ld, dist: %f, empty: %d, time: %ld\n", pt.t, pt2.dist(pt1), points.empty(), holdStartTime);

  // width calculation - initially we had separate class for each, but only velocity has any complexity
  Dim wscale = 1.0;
  int nscales = 0;
  if(pen.hasFlag(ScribblePen::WIDTH_SPEED) && pen.spdMax != 0) {
    // prevVelPt isn't affected by removal of points ... seems to work w/ smoothing OK though
    static constexpr Dim invTau = 1/20.0;
    const Dim minV = 0.1*pen.spdMax, maxV = pen.spdMax;  // or *0.2? or fixed min speed?
    totalDist += pt.dist(prevVelPt);
    Dim dt = (pt.t - prevt);
    if((totalDist > 0 && dt > 5) || vel == 0) {
      if(prevt == 0) {}
      else if(vel == 0)
        vel = totalDist/std::max(1.0, dt);  // second point
      else
        vel += (1 - std::exp(-dt*invTau))*(totalDist/dt - vel);  // IIR low pass
      //PLATFORM_LOG("vel: %f; filtered: %f\n", totalDist/dt, vel);
      prevt = pt.t;
      totalDist = 0;
    }
    prevVelPt = Point(pt.x, pt.y);  // note that prevt is not necessarily previous pt.t!
    wscale *= vel == 0 ? 0.5 : std::min(std::max(((maxV > 0 ? maxV : -minV) - vel)/(maxV - minV), 0.0), 1.0);
    ++nscales;
  }
  if(pen.hasFlag(ScribblePen::WIDTH_DIR)) {
    Dim rad = pen.dirAngle * M_PI/180.0;
    // use point sufficient distance away to calculate angle
    Point p = pt2;
    Dim dtot = 0;
    for(auto it = points.rbegin(); it != points.rend() && dtot < 2; ++it) {
      dtot += it->dist(p);
      p = *it;
    }
    wscale *= std::abs(dot((pt2 - p).normalize(), Point(std::cos(rad), std::sin(rad))));  //0.5*(1.0 + dot())
    ++nscales;
  }
  if(pen.hasFlag(ScribblePen::WIDTH_PR)) {  //pen.param1 != 0) {
    //Dim factr = pen.param2 == 0 ? 1.0 : std::pow(1-pen.param2, -pen.param1);  // param2 is min pressure
    wscale *= pen.prParam > 0 ? (1 - std::pow(1 - std::min(pt.pr, 1.0), pen.prParam)) :  // 1-factr*pow(...)
        std::pow(1 - std::min(pt.pr, 1.0), -pen.prParam);
    ++nscales;
  }
  if(nscales > 1)
    wscale = std::pow(wscale, 1.0/nscales);  // geometric mean seems to work well

  Dim w2 = pen.width*(1 - pen.wRatio*(1 - std::min(std::max(wscale, 0.0), 1.0)));
  // discard initial point for width fns that need two points to calculate
  if(widths.size() == 1 && (pen.hasFlag(ScribblePen::WIDTH_DIR) || pen.hasFlag(ScribblePen::WIDTH_SPEED)))
    widths.back() = w2;
  Dim w1 = widths.empty() ? w2 : widths.back();
  // dist from previous pt plus current radius should be >= radius at previous point
  if(!widths.empty())
    w2 = std::max(w2, w1 - 2*pt2.dist(pt1));  // std::min(w1, 2*w1 - 2*pt2.dist(pt1))
  points.push_back(pt2);
  widths.push_back(w2);

  // nothing to do if same as prev point(this can happen with, e.g., line drawing mode), but note that we
  //  still save point since caller may subsequently call removePoint (again, e.g., line drawing mode).
  if(points.size() > 1 && pt2 == pt1) {
    if(style != Flat)
      stroke->moveTo(pt2);
    return;
  }

  element->node->invalidate(false);

  if(style == Round) {
    if(points.size() == 2)
      stroke->clear();
    addRoundSubpath(*stroke, pt1, w1, pt2, w2);
    dirty.rectUnion(Rect::centerwh(pt1, w1, w1));
    dirty.rectUnion(Rect::centerwh(pt2, w2, w2));
    return;
  }

  if(style == Chisel) {
    if(points.size() == 2)
      stroke->clear();
    addChiselSubpath(*stroke, pt1, w1, pt2, w2);
    dirty.rectUnion(Rect::centerwh(pt1, w1, w1));
    dirty.rectUnion(Rect::centerwh(pt2, w2, w2));
    return;
  }

  // style == Flat (Miter)
  if(points.size() == 1) {
    // create initial point in case no further points are added (discarded otherwise)
    stroke->moveTo(pt.x - w2/2, pt.y - w2/2);
    stroke->lineTo(pt.x - w2/2, pt.y + w2/2);
    stroke->lineTo(pt.x + w2/2, pt.y + w2/2);
    stroke->lineTo(pt.x + w2/2, pt.y - w2/2);  // note unclosed like longer flat paths
    dirty.rectUnion(stroke->controlPointRect());
    return;
  }

  Point d01 = pt1 - pt0;
  Point d12 = pt2 - pt1;
  // calculate normals for two segments
  Point n01 = normal(d01);
  Point n12 = normal(d12);
  Dim hw = w1/2;  // half-width
  //Point extrude = hw*(n01 + n12).normalize();  // old, incorrect calculation
  Dim extrude_denom = 1 + dot(n01, n12);
  Point extrude = extrude_denom < 1E-6 ? hw*n01 : hw*(n01 + n12)/extrude_denom;

  if(path1.size() == 0) {
    // shift point to give something like a square end cap
    hw = std::max(hw, w2/2 - pt2.dist(pt1));  // ensure minimum cap size
    pt1 = pt1 - hw*d12.normalize();
    Point a = pt1 + hw*n12;  //extrude;
    Point b = pt1 - hw*n12;  //extrude;
    path1.moveTo(a);
    path2.moveTo(b);
    // would like to do path1.moveTo(b); path1.lineTo(a), but this would break recovery of original input pts
    dirty.rectUnion(a).rectUnion(b);  //Intersect(Rect::corners(a, b));
    dirty.rectUnion(stroke->controlPointRect());  // include square drawn for first point
  }
  else if(extrude_denom >= 1) {  // >= 90 deg bend - miter join
    Point a = pt1 + extrude;
    Point b = pt1 - extrude;
    path1.lineTo(a);
    path2.lineTo(b);
    dirty.rectUnion(a).rectUnion(b);  //Intersect(Rect::corners(a, b));
  }
  else {  // <90 bend - connect flat endpoints for segments - no miter (not the same as miter limit)
    //Dim over12 = pt1.dist(pt2 - hw*n12) - extrude.dist();
    //Dim over01 = pt1.dist(pt0 - hw*n01) - extrude.dist();
    Dim sign = cross(d01, d12) < 0 ? -1 : 1;
    Path2D& outer = sign < 0 ? path1 : path2;
    Path2D& inner = sign < 0 ? path2 : path1;
    inner.lineTo(pt1 + sign*hw*n01);  // inner01
    inner.lineTo(pt1 + sign*hw*n12);  // inner12
    outer.lineTo(pt1 - sign*hw*n01);  // outer01
    outer.lineTo(pt1 - sign*hw*n12);  // outer12
    dirty.rectUnion(Rect::centerwh(pt1, w1, w1));
  }
  assembleFlatStroke();
}

void FilledStrokeBuilder::assembleFlatStroke()
{
  if(points.size() < 2)
    return;
  Point pt0 = points[points.size() - 2];
  Point pt1 = points.back();
  Dim hw = widths.back()/2;
  *stroke = path1;
  Point dr = (pt1 - pt0).normalize();
  // extend to create something like a square end cap - note that no change to Element::fromPenPoints needed!
  pt1 = pt1 + dr*hw;
  Point a = Point(pt1.x - dr.y*hw, pt1.y + dr.x*hw);
  Point b = Point(pt1.x + dr.y*hw, pt1.y - dr.x*hw);
  stroke->lineTo(a.x, a.y);
  stroke->lineTo(b.x, b.y);
  stroke->connectPath(path2.toReversed());
  dirty.rectUnion(a).rectUnion(b);  //Intersect(Rect::corners(a, b));
}

void FilledStrokeBuilder::removePoints(int n)
{
  // smoothing is enabled by default on iOS, so to avoid redrawing entire stroke w/ every point, we need to
  //  calculate tight dirty rect
  element->node->invalidate(false);
  if(n < 0 || n >= int(points.size())) {
    dirty.rectUnion(stroke->getBBox());
    points.clear();
    widths.clear();
    path1.clear();
    path2.clear();
    stroke->clear();
  }
  else if(style != Flat) {
    for(; n > 0; --n) {
      points.pop_back();
      widths.pop_back();
      int ii = stroke->size() - 1;
      for(; ii >= 0 && stroke->command(ii) != Path2D::MoveTo; --ii)
        dirty.rectUnion(stroke->point(ii));
      stroke->resize(ii);
    }
  }
  else {
    // include endcap in dirty!
    dirty.rectUnion(stroke->point(path1.size()));
    dirty.rectUnion(stroke->point(path1.size() + 1));
    for(; n > 0; --n) {
      Point p = points.back();
      points.pop_back();
      widths.pop_back();
      if(!points.empty() && points.back() == p)
        continue;
      bool two = path1.size() > 1 && path2.size() > 1 && approxEq(p, (path1.rpoint(2) + path2.rpoint(2))/2, 1E-6);
      dirty.rectUnion(path1.rpoint(1));
      dirty.rectUnion(path2.rpoint(1));
      if(two) {
        dirty.rectUnion(path1.rpoint(2));
        dirty.rectUnion(path2.rpoint(2));
      }
      path1.resize(path1.size() - (two ? 2 : 1));
      path2.resize(path2.size() - (two ? 2 : 1));
    }
    if(!path1.empty() && !path2.empty()) {
      dirty.rectUnion(path1.rpoint(1));
      dirty.rectUnion(path2.rpoint(1));
    }
    assembleFlatStroke();
  }
}

Rect FilledStrokeBuilder::getDirty()
{
  Rect r = dirty;
  dirty = Rect();
  return r;
}

bool FilledStrokeBuilder::shapeRecognize(Timestamp t)
{
  int holdTimeout = 0;
  if (points.size() < 5 || holdStartTime == 0 || t - holdStartTime < holdTimeout)
    return false;

  Point centroid;
  double avgRadius;
  Point start, end;
  std::vector<Point> corners;

  if (ShapeRecognizer::isCircle(points, centroid, avgRadius)) {
    removePoints(points.size());
    dirty = Rect();

    double halfWidth = pen.width / 2.0;
    double circumference = 2 * M_PI * avgRadius;
    int numSegments = std::max(20, static_cast<int>(circumference / 5.0));
    
    Path2D outerCircle;
    for (int i = 0; i < numSegments; ++i) {
        double angle = 2 * M_PI * i / numSegments;
        Point point = {
            centroid.x + (avgRadius + halfWidth) * std::cos(angle),
            centroid.y + (avgRadius + halfWidth) * std::sin(angle)
        };
        if (i == 0)
            outerCircle.moveTo(point.x, point.y);
        else
            outerCircle.lineTo(point.x, point.y);
    }
    outerCircle.closeSubpath();

    Path2D innerCircle;
    for (int i = 0; i < numSegments; ++i) {
      double angle = 2 * M_PI * i / numSegments;
      Point point = {
        centroid.x + (avgRadius - halfWidth) * std::cos(angle),
        centroid.y + (avgRadius - halfWidth) * std::sin(angle)
      };
      if (i == 0)
        innerCircle.moveTo(point.x, point.y);
      else
        innerCircle.lineTo(point.x, point.y);
    }
    innerCircle.closeSubpath();

    stroke->clear();
    stroke->connectPath(outerCircle);
    stroke->connectPath(innerCircle.toReversed());
    
    return true;
  }
  if (ShapeRecognizer::isRectangle(points, corners)) {
    removePoints(points.size());

    double halfWidth = pen.width; // So not really halfWidth here

    Point p0 = corners[0];
    Point p1 = corners[1];
    Point p2 = corners[2];
    Point p3 = corners[3];

    double avgLeftX   = (p0.x + p3.x) / 2;
    double avgRightX  = (p1.x + p2.x) / 2;
    double avgTopY    = (p0.y + p1.y) / 2;
    double avgBottomY = (p2.y + p3.y) / 2;

    Point o0 = {avgLeftX, avgTopY};
    Point o1 = {avgRightX, avgTopY};
    Point o2 = {avgRightX, avgBottomY};
    Point o3 = {avgLeftX, avgBottomY};

    Point i0 = { o0.x + halfWidth, o0.y + halfWidth };
    Point i1 = { o1.x - halfWidth, o1.y + halfWidth };
    Point i2 = { o2.x - halfWidth, o2.y - halfWidth };
    Point i3 = { o3.x + halfWidth, o3.y - halfWidth };

    Path2D hollowRect;
    hollowRect.moveTo(o0.x, o0.y);
    hollowRect.lineTo(o1.x, o1.y);
    hollowRect.lineTo(o2.x, o2.y);
    hollowRect.lineTo(o3.x, o3.y);
    hollowRect.closeSubpath();

    Path2D innerRect;
    innerRect.moveTo(i0.x, i0.y);
    innerRect.lineTo(i1.x, i1.y);
    innerRect.lineTo(i2.x, i2.y);
    innerRect.lineTo(i3.x, i3.y);
    innerRect.closeSubpath();

    stroke->clear();
    stroke->connectPath(hollowRect);
    stroke->connectPath(innerRect.toReversed());
    return true;
  }
  if (ShapeRecognizer::isArrow(points, start, end)) {
    removePoints(points.size());
    double halfWidth = pen.width / 2.0;
    double headLength = std::max(10.0, pen.width * 2.5);
    double headWidth = pen.width * 1.5;

    double dx = end.x - start.x;
    double dy = end.y - start.y;
    double shaftLen = std::hypot(dx, dy);
    double ux = dx / shaftLen;
    double uy = dy / shaftLen;

    double px = -uy;
    double py = ux;

    Point a = { start.x + px * halfWidth, start.y + py * halfWidth };
    Point b = { start.x - px * halfWidth, start.y - py * halfWidth };
    Point c = { end.x   - px * halfWidth, end.y   - py * halfWidth };
    Point d = { end.x   + px * halfWidth, end.y   + py * halfWidth };

    Path2D shaft;
    shaft.moveTo(a.x, a.y);
    shaft.lineTo(b.x, b.y);
    shaft.lineTo(c.x, c.y);
    shaft.lineTo(d.x, d.y);
    shaft.closeSubpath();

    const double angleRad1 = 135.0 * M_PI / 180.0;
    double hx = std::cos(angleRad1) * ux - std::sin(angleRad1) * uy;
    double hy = std::sin(angleRad1) * ux + std::cos(angleRad1) * uy;

    const double angleRad2 = -135.0 * M_PI / 180.0;
    double hx2 = std::cos(angleRad2) * ux - std::sin(angleRad2) * uy;
    double hy2 = std::sin(angleRad2) * ux + std::cos(angleRad2) * uy;

    Point tip1 = {end.x + hx * headLength, end.y + hy * headLength};
    Point tip2 = {end.x + hx2 * headLength, end.y + hy2 * headLength};
    
    double pxHead1 = -hy;
    double pyHead1 = hx;
    double pxHead2 = -hy2;
    double pyHead2 = hx2;
    
    Point ha1 = { end.x + pxHead1 * halfWidth, end.y + pyHead1 * halfWidth };
    Point hb1 = { end.x - pxHead1 * halfWidth, end.y - pyHead1 * halfWidth };
    Point hc1 = { tip1.x - pxHead1 * halfWidth, tip1.y - pyHead1 * halfWidth };
    Point hd1 = { tip1.x + pxHead1 * halfWidth, tip1.y + pyHead1 * halfWidth };

    Point ha2 = { end.x + pxHead2 * halfWidth, end.y + pyHead2 * halfWidth };
    Point hb2 = { end.x - pxHead2 * halfWidth, end.y - pyHead2 * halfWidth };
    Point hc2 = { tip2.x - pxHead2 * halfWidth, tip2.y - pyHead2 * halfWidth };
    Point hd2 = { tip2.x + pxHead2 * halfWidth, tip2.y + pyHead2 * halfWidth };

    Path2D headLine1;
    headLine1.moveTo(ha1.x, ha1.y);
    headLine1.lineTo(hb1.x, hb1.y);
    headLine1.lineTo(hc1.x, hc1.y);
    headLine1.lineTo(hd1.x, hd1.y);
    headLine1.closeSubpath();

    Path2D headLine2;
    headLine2.moveTo(ha2.x, ha2.y);
    headLine2.lineTo(hb2.x, hb2.y);
    headLine2.lineTo(hc2.x, hc2.y);
    headLine2.lineTo(hd2.x, hd2.y);
    headLine2.closeSubpath();

    stroke->clear();
    stroke->connectPath(shaft);
    stroke->connectPath(headLine1);
    stroke->connectPath(headLine2);
    
    return true;
  }
  if (ShapeRecognizer::isLine(points, start, end)) {
    removePoints(points.size());
    dirty = Rect();

    double halfWidth = pen.width / 2.0;
    double dx = end.x - start.x;
    double dy = end.y - start.y;
    double length = std::hypot(dx, dy);
    double ux = dx / length;
    double uy = dy / length;
    double px = -uy;
    double py = ux;

    Point a = { start.x + px * halfWidth, start.y + py * halfWidth };
    Point b = { start.x - px * halfWidth, start.y - py * halfWidth };
    Point c = { end.x   - px * halfWidth, end.y   - py * halfWidth };
    Point d = { end.x   + px * halfWidth, end.y   + py * halfWidth };

    Path2D linePath;
    linePath.moveTo(a.x, a.y);
    linePath.lineTo(b.x, b.y);
    linePath.lineTo(c.x, c.y);
    linePath.lineTo(d.x, d.y);
    linePath.closeSubpath();

    stroke->clear();
    stroke->connectPath(linePath);

    return true;
  }
  return false;
}
// filters

// when accounting for pressure, the decrease in number of points is fairly minor (maybe ~20%), esp. when
//  writing at normal speed (decrease can be larger when writing unnaturally slow) on Windows; perhaps
//  other platforms might have higher input rates and thus see bigger improvement
void SimplifyFilter::addPoint(const StrokePoint& pt)
{
  //simpPts++; origPts++;
  if(!pts.empty() && std::abs(pts.front().pr - pt.pr) > threshPr) {
    if(pts.size() > 2) {
      // this is the case that pts simplifies to just start and end (i.e. on previous run of simplifyRDP())
      next->removePoints(pts.size() - 1);
      next->addPoint(pts.back());
    }
    next->addPoint(pt);
    pts = { pt };
    return;
  }
  pts.push_back(pt);
  // idea here is to process path in chunks - we keep collecting points until at least one point (since last
  //  simplification) exceeds threshold, then simplify the path between 2nd to last point of prev
  //  simplication (not last point!) and current end point
  std::vector<StrokePoint> simp = simplifyRDP(pts, 0, pts.size() - 1, threshDist);
  if(simp.size() > 2) {
    if(pts.size() > 3) {
      next->removePoints(pts.size() - 1);  // we haven't added current point to `next` yet
      for(const StrokePoint& p : simp)
        next->addPoint(p);
      //simpPts += simp.size() - pts.size();
    }
    else
      next->addPoint(pt);
    pts.assign(simp.end() - 2, simp.end());
  }
  else
    next->addPoint(pt);
}

void SimplifyFilter::removePoints(int n)
{
  pts.resize(n < 0 || n > int(pts.size()) ? 0 : pts.size() - n);
  next->removePoints(n);
}

void SimplifyFilter::finalize()
{
  //PLATFORM_LOG("Thresh: %f; Orig points: %d; Simp points: %d\n", threshDist, origPts, simpPts);
  next->finalize();
}

// this was written because of concerns about using FIR w/o PointDensityFilter ... but w/ stylus, points are
//  usually denser anyway, and FIR might be fine (but this IIR seems to work well too, and is streaming!)
// tau ~ 2 - 10 /mZoom seems reasonable
void LowPassIIR::addPoint(const StrokePoint& pt)
{
  // this is a bit of a hack to handle low pass followed by simplify - since streaming low pass (necessarily)
  //  lags input (but stroke must extend to current pen position), adding current pos to simplify filter will
  //  likely flush it due to large difference (and indeed we see very little simplification w/o this fix) - so
  //  we add current point directly to StrokeBuilder; an alternative hack would be to have simplify filter not
  //  process current point (i.e. call simplifyRDP() before adding current point to pts), but this seemed easier
  InputProcessor* last = next;
  while(last->next) last = last->next;

  Dim a = prevPt.isNaN() ? 1 : 1 - std::exp(-pt.dist(prevPt)*invTau);
  filtPt += a*(pt - filtPt);
  filtPt.pr += a*(pt.pr - filtPt.pr);
  if(!prevPt.isNaN()) {
    last->removePoints(1);
    next->addPoint(filtPt);
  }
  last->addPoint(pt);
  prevPt = pt;
}

void LowPassIIR::removePoints(int n)
{
  next->removePoints(n);
}

void LowPassIIR::finalize()
{
  InputProcessor* last = next;
  while(last->next) last = last->next;

  last->removePoints(1);
  next->addPoint(prevPt);
  next->finalize();
}

// Savitzky-Golay seems to work better than Gaussian - e.g., shrinks loops less for given amount of overall
//  smoothing (subjective).  But ideally, we would pass both filtertype and filterstrength parameters
// Savitzky-Golay has the advantage of a very flat passband, so it preserves overall stroke shape very
//  well (at the cost of reduced stopband attenuation - wiggles aren't suppressed as much as possible)

SymmetricFIR::SymmetricFIR(int ncoeffs)
{
  coeffs.resize(ncoeffs);
  const Dim m = 2*ncoeffs - 1;
  // calculate normalized coefficients for quadratic (N = 2) Savitzky-Golay filter
  // ref: http://en.wikipedia.org/wiki/Savitzky%E2%80%93Golay_filter, eq. for C_{0i}
  for(int ii = 0; ii < ncoeffs; ii++)
    coeffs[ii] = (3*(3*m*m - 7 - 20*ii*ii))/(4*m*(m*m - 4));
}

void SymmetricFIR::addPoint(const StrokePoint& pt)
{
  X.push_back(pt.x);
  Y.push_back(pt.y);
  P.push_back(pt.pr);
  next->addPoint(pt);
}

void SymmetricFIR::removePoints(int n)
{
  int newsize = n < 0 ? 0 : X.size() - n;
  X.resize(newsize);
  Y.resize(newsize);
  P.resize(newsize);
  next->removePoints(n);
}

void SymmetricFIR::finalize()
{
  std::vector<Dim> outX(X.size());
  std::vector<Dim> outY(Y.size());
  std::vector<Dim> outP(P.size());
  applyFilter(&X[0], &outX[0], X.size());
  applyFilter(&Y[0], &outY[0], Y.size());
  applyFilter(&P[0], &outP[0], P.size());

  // remove and replace all downstream points
  next->removePoints(-1);
  for(unsigned int ii = 0; ii < X.size(); ii++)
    next->addPoint(StrokePoint(outX[ii], outY[ii], outP[ii]));
  next->finalize();
}

// symmetric FIR filter; coefficients w/ negative indicies are not stored
// N = length of out = length of in; we must have out != in
void SymmetricFIR::applyFilter(Dim in[], Dim out[], int N)
{
  int ii;
  Dim sum, norm;

  // split [0 .. N-1] into [ 0, .., i_pre-1] + [i_pre, ..., i_post-1], [i_post, ..., N-1 ]
  // such that i_pre = length-1 and i_post = N-length if there is enough space
  int ncoeffs = coeffs.size();
  int i_pre  = std::min(ncoeffs - 1, N/2);
  int i_post = std::max(N - ncoeffs, N/2);

  for(ii = 0; ii < i_pre; ii++) {
    norm = coeffs[0];
    sum = in[ii] * coeffs[0];
    for(int n = 1; n <= ii; n++) {
      sum += (in[ii-n] + in[ii+n]) * coeffs[n];
      norm += 2 * coeffs[n];
    }
    out[ii] = sum / norm;
  }

  for(ii = i_pre; ii < i_post; ii++) {
    sum = in[ii] * coeffs[0];
    for(int n = 1; n < ncoeffs; n++)
      sum += (in[ii-n] + in[ii+n]) * coeffs[n];
    out[ii] = sum;
  }

  for(ii = i_post; ii < N; ii++) {
    norm = coeffs[0];
    sum = in[ii] * coeffs[0];
    for(int n = 1; n < N - ii; n++) {
      sum += (in[ii-n] + in[ii+n]) * coeffs[n];
      norm += 2 * coeffs[n];
    }
    out[ii] = sum / norm;
  }
}
