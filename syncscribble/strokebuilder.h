#ifndef STROKEBUILDER_H
#define STROKEBUILDER_H

#include "page.h"
#include "scribblepen.h"

// abstractions for Stroke construction with support for filtering

struct StrokePoint : public Point
{
  Dim pr;
  Dim tiltX;
  Dim tiltY;
  Timestamp t;
  //Dim velocity;
  StrokePoint(Dim _x = 0, Dim _y = 0, Dim _p = 1.0, Dim _tiltX = 0, Dim _tiltY = 0, Timestamp _t = 0)
      : Point(_x, _y), pr(_p), tiltX(_tiltX), tiltY(_tiltY), t(_t) {}
};

class InputProcessor
{
  friend class StrokeBuilder;
public:
  InputProcessor() : next(NULL) {}
  virtual ~InputProcessor() {}

  virtual void addPoint(const StrokePoint& pt) = 0;  //{ next->addPoint(x, y, pr); }
  virtual void removePoints(int n) = 0;  //{ next->removePoints(n); }
  virtual void finalize() = 0;  //{ return next->finalize(); }

  InputProcessor* next;
};

class StrokeBuilder : public InputProcessor
{
public:
  StrokeBuilder() : firstProcessor(this) {}
  ~StrokeBuilder() override;
  void addFilter(InputProcessor* p);
  void addInputPoint(const StrokePoint& pt) { firstProcessor->addPoint(pt); }
  Element* getElement() { return element; }
  Path2D* getPath() { return stroke; }
  virtual Rect getDirty() { return element->bbox(); }
  Element* finish();  // caller assumes ownership of returned Element

  static StrokeBuilder* create(const ScribblePen& pen);
  static Point calcCom(SvgNode* node, Path2D* path);

protected:
  Element* element;
  Path2D* stroke;
  void finalize() override;

private:
  InputProcessor* firstProcessor;
};

class StrokedStrokeBuilder : public StrokeBuilder
{
public:
  StrokedStrokeBuilder(const ScribblePen& pen);
  Rect getDirty() override;

protected:
  void addPoint(const StrokePoint& pt) override;
  void removePoints(int n) override;

private:
  Dim width;
  Rect dirty;
};

class FilledStrokeBuilder : public StrokeBuilder {
public:
  FilledStrokeBuilder(const ScribblePen& _pen);
  Rect getDirty() override;

  static void addRoundSubpath(Path2D& path, Point pt1, Dim w1, Point pt2, Dim w2);
  static void addChiselSubpath(Path2D& path, Point pt1, Dim w1, Point pt2, Dim w2);
  static constexpr Dim CHISEL_ASPECT_RATIO = 4;

protected:
  void addPoint(const StrokePoint& pt) override;
  void removePoints(int n) override;

private:
  void assembleFlatStroke();

  ScribblePen pen;
  enum Style { Flat, Round, Chisel } style;
  Rect dirty;
  Path2D path1;
  Path2D path2;
  std::vector<Point> points;
  std::vector<Dim> widths;

  // for WIDTH_VEL
  Dim vel = 0;
  Dim totalDist = 0;
  Timestamp prevt = 0;
  Point prevVelPt;
};

// filters

class SimplifyFilter : public InputProcessor
{
public:
  SimplifyFilter(Dim dist = 0.1, Dim pr = 0.1) : threshDist(dist), threshPr(pr) {}

protected:
  void addPoint(const StrokePoint& pt) override;
  void removePoints(int n) override;
  void finalize() override;

private:
  Dim threshDist;
  Dim threshPr;
  std::vector<StrokePoint> pts;
  //int origPts = 0;  // for testing
  //int simpPts = 0;
};

class LowPassIIR : public InputProcessor
{
public:
  LowPassIIR(Dim tau) : prevPt(NaN, NaN), invTau(1/tau) {}
  void addPoint(const StrokePoint& pt) override;
  void removePoints(int n) override;
  void finalize() override;  // { next->finalize(); }

  StrokePoint prevPt;
  StrokePoint filtPt;
  Dim invTau;
};

class SymmetricFIR : public InputProcessor
{
public:
  SymmetricFIR(int ncoeffs);

protected:
  void addPoint(const StrokePoint& pt) override;
  void removePoints(int n) override;
  void finalize() override;

private:
  void applyFilter(Dim in[], Dim out[], int N);

  std::vector<Dim> X;
  std::vector<Dim> Y;
  std::vector<Dim> P;
  //std::vector<StrokePoint> points;
  std::vector<Dim> coeffs;

  static Dim identity[];
  static Dim gaussian5[];
  static Dim gaussian11[];
  static Dim savitzky_golay5[];
  static Dim savitzky_golay11[];
};

#endif
