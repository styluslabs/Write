#include "shaperecognizer.h"

bool ShapeRecognizer::isCircle(const std::vector<Point> &points, Point &centroid, double &avgRadius)
{
    double sumX = 0, sumY = 0;
    for (const auto& point : points) {
        sumX += point.x;
        sumY += point.y;
    }

    centroid = { sumX / points.size(), sumY / points.size() };

    std::vector<double> distances;
    for (const auto& point : points) {
        double dx = point.x - centroid.x;
        double dy = point.y - centroid.y;
        distances.push_back(std::sqrt(dx * dx + dy * dy));
    }

    avgRadius = std::accumulate(distances.begin(), distances.end(), 0.0) / distances.size();

    double variance = 0;
    for (double d : distances) {
        variance += (d - avgRadius) * (d - avgRadius);
    }
    variance /= distances.size();

    double stddev = std::sqrt(variance);
    double ratio = stddev / avgRadius;

    double threshold = 0.13;
    printf("Circle: StdDev %.5f, AvgRadius %.5f, Ratio %.5f\n", stddev, avgRadius, ratio);

    return ratio < threshold;
}

bool ShapeRecognizer::isLine(const std::vector<Point>& points, Point& start, Point& end) {
    double sumX = 0, sumY = 0;
    for (const auto& p : points) {
        sumX += p.x;
        sumY += p.y;
    }
    double meanX = sumX / points.size();
    double meanY = sumY / points.size();

    double num = 0, den = 0;
    for (const auto& p : points) {
        num += (p.x - meanX) * (p.y - meanY);
        den += (p.x - meanX) * (p.x - meanX);
    }

    if (den == 0) {
        start = { meanX, points.front().y };
        end = { meanX, points.back().y };
        return true;
    }

    double slope = num / den;
    double intercept = meanY - slope * meanX;

    double sumSquaredDistances = 0;
    for (const auto& p : points) {
        double expectedY = slope * p.x + intercept;
        double distance = std::abs(expectedY - p.y) / std::sqrt(1 + slope * slope);
        sumSquaredDistances += distance * distance;
    }
    double rmse = std::sqrt(sumSquaredDistances / points.size());

    double span = std::hypot(points.back().x - points.front().x, points.back().y - points.front().y);
    double threshold = span * 0.05;
    printf("Line: RMSE %.5f, Span %.5f, Threshold %.5f\n", rmse, span, threshold);
    if (rmse > threshold) return false;

    auto project = [&](const Point& p) -> Point {
        double x = (p.x + slope * (p.y - intercept)) / (1 + slope * slope);
        double y = slope * x + intercept;
        return { x, y };
    };

    start = project(points.front());
    end   = project(points.back());

    return true;
}

bool ShapeRecognizer::isRectangle(const std::vector<Point>& points, std::vector<Point>& outCorners) {
    Point centroid{0, 0};
    for (const auto& p : points) {
        centroid.x += p.x;
        centroid.y += p.y;
    }
    centroid.x /= points.size();
    centroid.y /= points.size();

    std::vector<Point> corners(4);
    double maxDists[4] = {0, 0, 0, 0};

    for (const auto& p : points) {
        double dx = p.x - centroid.x;
        double dy = p.y - centroid.y;
        double dist = dx * dx + dy * dy;

        int quadrant = (dy >= 0) * 2 + (dx >= 0);
        if (dist > maxDists[quadrant]) {
            maxDists[quadrant] = dist;
            corners[quadrant] = p;
        }
    }

    std::sort(corners.begin(), corners.end(), [&centroid](const Point& a, const Point& b) {
        double angleA = std::atan2(a.y - centroid.y, a.x - centroid.x);
        double angleB = std::atan2(b.y - centroid.y, b.x - centroid.x);
        return angleA < angleB;
    });

    double sides[4];
    double angles[4];
    for (int i = 0; i < 4; ++i) {
        Point a = corners[i];
        Point b = corners[(i + 1) % 4];
        Point c = corners[(i + 2) % 4];

        double dx = b.x - a.x;
        double dy = b.y - a.y;
        sides[i] = std::hypot(dx, dy);

        double dx1 = a.x - b.x;
        double dy1 = a.y - b.y;
        double dx2 = c.x - b.x;
        double dy2 = c.y - b.y;

        double dot = dx1 * dx2 + dy1 * dy2;
        double mag1 = std::hypot(dx1, dy1);
        double mag2 = std::hypot(dx2, dy2);
        angles[i] = std::acos(dot / (mag1 * mag2)) * 180.0 / M_PI;
    }

    double sideTol = 0.2 * ((sides[0] + sides[2]) / 2);
    double angleTol = 15.0;

    bool sidesEqual = std::abs(sides[0] - sides[2]) < sideTol &&
                      std::abs(sides[1] - sides[3]) < sideTol;
    bool anglesRight = std::all_of(angles, angles + 4, [=](double angle) {
        return std::abs(angle - 90.0) < angleTol;
    });

    bool closedFigure = points.front().dist(points.back()) < 0.5 * points.front().dist(centroid);

    printf("Rectangle: Closed=%s, SidesEqual=%s, AnglesRight=%s\n", 
        closedFigure ? "true" : "false", sidesEqual ? "true" : "false", anglesRight ? "true" : "false");
    
    outCorners = corners;
    return sidesEqual && anglesRight && closedFigure;
}


bool ShapeRecognizer::isArrow(const std::vector<Point>& points, Point& start, Point& end) {
    size_t splitIndex = points.size() * 2 / 3;
    if (splitIndex >= points.size() - 2) splitIndex = points.size() - 3;

    std::vector<Point> shaft(points.begin(), points.begin() + splitIndex);
    std::vector<Point> head(points.begin() + splitIndex, points.end());

    if (!isLine(shaft, start, end)) return false;

    auto vec = [](const Point& a, const Point& b) {
        return std::pair<double, double>{b.x - a.x, b.y - a.y};
    };

    auto normalize = [](std::pair<double, double> v) {
        double len = std::hypot(v.first, v.second);
        return std::pair<double, double>{v.first / len, v.second / len};
    };

    auto dot = [](std::pair<double, double> u, std::pair<double, double> v) {
        return u.first * v.first + u.second * v.second;
    };

    Point shaftStart = shaft.front(), shaftEnd = shaft.back();
    Point headStart = head.front(), headEnd = head.back();

    auto dir1 = normalize(vec(shaftStart, shaftEnd));
    auto dir2 = normalize(vec(headStart, headEnd));

    double angleCos = dot(dir1, dir2);
    double angleDeg = std::acos(angleCos) * 180.0 / M_PI;

    printf("Arrow: shaft-head angle: %.2f\n", angleDeg);

    return angleDeg > 140.0 && angleDeg < 160.0;
}

