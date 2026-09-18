#pragma once
#include <QPainterPath>
#include <QRectF>
#include <QStringList>

// Material 3's shape library.
//
// Material publishes a set of named shapes and uses them to mask imagery and to
// carry the loading indicator's morph. It builds them from RoundedPolygon, which
// Qt has no equivalent of, so each shape here is written as the radius it
// carries at a given angle. The lobe counts, depths and proportions come from
// Material's own definitions; the way the outline is produced does not.
namespace m3 {

// The shapes this build knows, in the order Material lists them.
QStringList shapeNames();
bool hasShape(const QString &name);

// The shape's radius at an angle in radians, normalised so its widest point is
// exactly one. Unknown names are a circle.
double shapeRadius(const QString &name, double angle);

// The outline sampled at `steps` even angles, as radii. The loading indicator
// morphs between two of these.
QList<double> shapeOutline(const QString &name, int steps);

// The outline as a closed path filling `bounds`, for clipping and painting.
QPainterPath shapePath(const QString &name, const QRectF &bounds, int steps = 128);

} // namespace m3
