#ifndef SHAPERECOGNIZER_H
#define SHAPERECOGNIZER_H

#include <vector>
#include "strokebuilder.h"

class ShapeRecognizer {
public:
    enum Shape { 
      None,
      Circle,
      Rectangle, 
      Arrow
    };

    static bool isCircle(const std::vector<Point>& points, Point& centroid, double& avgRadius);
    static bool isLine(const std::vector<Point>& points, Point& start, Point& end);
    static bool isRectangle(const std::vector<Point>& points, std::vector<Point>& outCorners);
    static bool isArrow(const std::vector<Point> &points, Point& start, Point& end);
};

#endif
