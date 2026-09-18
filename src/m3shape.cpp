#include "m3shape.h"
#include <QtGlobal>
#include <cmath>

namespace m3 {
namespace {
constexpr double kPi = 3.14159265358979323846;

// A lobed shape: `count` bumps cut `depth` out of the radius, with `point`
// deciding how sharply each one comes to a tip. Material builds these from
// stars with an inner radius and a corner rounding; the depth is what the inner
// radius takes away and the point is what the rounding leaves behind.
double lobed(double angle, double count, double depth, double point) {
  return 1 - depth * std::pow((1 - std::cos(count * angle)) / 2, point);
}

// A regular polygon eased towards its circumcircle, which is what rounding its
// corners does to the silhouette.
double polygonal(double angle, double sides, double rounding) {
  const double step = 2 * kPi / sides;
  const double offset = std::fmod(std::fmod(angle, step) + step, step) - kPi / sides;
  const double edge = std::cos(kPi / sides) / std::cos(offset);
  return edge * (1 - rounding) + rounding;
}

double superellipse(double angle, double wide, double tall, double power) {
  return std::pow(std::pow(std::abs(std::cos(angle) / wide), power) +
                      std::pow(std::abs(std::sin(angle) / tall), power),
                  -1 / power);
}

double elliptical(double angle, double ratio, double tilt) {
  const double a = angle - tilt;
  return ratio / std::sqrt(std::pow(ratio * std::cos(a), 2) + std::pow(std::sin(a), 2));
}

struct Shape {
  const char *name;
  double (*radius)(double);
};

// Material's definitions, read off its shape library:
//   sunny and the cookies are stars, named by their point count, with an inner
//   radius of 0.75 to 0.8 and 15% to 50% corner rounding;
//   soft burst and burst repeat a lobe ten and twelve times;
//   the pill and the square are rounded rectangles;
//   the oval is a circle squashed to 0.64 and turned a eighth.
const Shape kShapes[] = {
    {"circle", [](double) { return 1.0; }},
    {"square", [](double a) { return superellipse(a, 1, 1, 4.0); }},
    {"oval", [](double a) { return elliptical(a, 0.64, -kPi / 4); }},
    {"pill", [](double a) { return superellipse(a, 1, 0.55, 4.5); }},
    {"pentagon", [](double a) { return polygonal(a, 5, 0.30); }},
    {"sunny", [](double a) { return lobed(a, 8, 0.20, 0.60); }},
    {"verySunny", [](double a) { return lobed(a, 8, 0.28, 0.45); }},
    {"softBurst", [](double a) { return lobed(a, 10, 0.15, 1.10); }},
    {"burst", [](double a) { return lobed(a, 12, 0.22, 0.35); }},
    {"cookie4Sided", [](double a) { return lobed(a, 4, 0.30, 1.00); }},
    {"cookie6Sided", [](double a) { return lobed(a, 6, 0.22, 1.00); }},
    {"cookie7Sided", [](double a) { return lobed(a, 7, 0.25, 1.00); }},
    {"cookie9Sided", [](double a) { return lobed(a, 9, 0.20, 1.00); }},
    {"cookie12Sided", [](double a) { return lobed(a, 12, 0.20, 1.00); }},
};
constexpr int kShapeCount = int(sizeof(kShapes) / sizeof(kShapes[0]));

const Shape *find(const QString &name) {
  for (int i = 0; i < kShapeCount; ++i)
    if (name == QLatin1String(kShapes[i].name))
      return &kShapes[i];
  return nullptr;
}
} // namespace

QStringList shapeNames() {
  QStringList names;
  names.reserve(kShapeCount);
  for (int i = 0; i < kShapeCount; ++i)
    names << QString::fromLatin1(kShapes[i].name);
  return names;
}

bool hasShape(const QString &name) { return find(name) != nullptr; }

double shapeRadius(const QString &name, double angle) {
  const auto *shape = find(name);
  return shape ? shape->radius(angle) : 1.0;
}

QList<double> shapeOutline(const QString &name, int steps) {
  const int count = qBound(8, steps, 512);
  const auto *shape = find(name);
  QList<double> radii;
  radii.reserve(count + 1);
  for (int i = 0; i <= count; ++i)
    radii.append(shape ? shape->radius(i * 2 * kPi / count) : 1.0);
  return radii;
}

QPainterPath shapePath(const QString &name, const QRectF &bounds, int steps) {
  const auto radii = shapeOutline(name, steps);
  const double half = qMin(bounds.width(), bounds.height()) / 2;
  const QPointF centre = bounds.center();
  QPainterPath path;
  for (int i = 0; i < radii.size() - 1; ++i) {
    const double angle = i * 2 * kPi / (radii.size() - 1);
    const QPointF point(centre.x() + radii[i] * half * std::cos(angle),
                        centre.y() + radii[i] * half * std::sin(angle));
    if (i == 0)
      path.moveTo(point);
    else
      path.lineTo(point);
  }
  path.closeSubpath();
  return path;
}

} // namespace m3
