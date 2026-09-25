#include "snakefield.h"
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QVector4D>
#include <algorithm>
#include <cmath>

SnakeField::SnakeField(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  connect(this, &SnakeField::changed, this, &QQuickItem::update);
}

void SnakeField::geometryChange(const QRectF &now, const QRectF &before) {
  QQuickItem::geometryChange(now, before);
  fitGrid();
  update();
}

void SnakeField::fitGrid() {
  if (width() <= 0 || height() <= 0 || m_cell <= 0)
    return;
  const int columns = std::max(4, int(width() / m_cell)), rows = std::max(4, int(height() / m_cell));
  if (!m_game)
    m_game = std::make_unique<SnakeGame>(columns, rows, 20260925);
  else
    m_game->resize(columns, rows);
}

void SnakeField::advance(qreal seconds) {
  fitGrid();
  if (!m_game || seconds <= 0)
    return;
  m_clock += seconds;
  m_glow = std::max<qreal>(0, m_glow - seconds / 0.3);
  // From an amble when it is quiet to a dash when it is loud.
  m_pending += seconds * (4.0 + 14.0 * std::clamp<qreal>(m_level, 0, 1));
  bool moved = false;
  while (m_pending >= 1) {
    m_pending -= 1;
    moved = true;
    if (m_game->step() && m_clock - m_lastGlow >= GlowSpacing) {
      m_lastGlow = m_clock;
      m_glow = 1;
    }
  }
  // Redrawn only when something visible changed: a step, the glow fading,
  // or the level moving far enough to show in the brightness.
  const bool glowing = m_glow > 0 || m_wasGlowing;
  m_wasGlowing = m_glow > 0;
  if (moved || glowing || std::abs(m_level - m_drawnLevel) > 0.03) {
    m_drawnLevel = m_level;
    update();
  }
  if (moved)
    emit stepped();
}

void SnakeField::beat(qreal strength, qreal across) {
  fitGrid();
  if (m_game && strength > 0 && m_game->beat(std::clamp<qreal>(across, 0, 1)))
    update();
}

qreal SnakeField::guardAt(QPointF p) const {
  qreal guarded = 0;
  for (const auto &v : m_guards) {
    const auto g = v.value<QVector4D>();
    if (g.z() <= 0 || g.w() <= 0)
      continue;
    const qreal mx = 0.02, my = 0.03;
    auto ease = [](qreal from, qreal to, qreal x) { return std::clamp<qreal>((x - from) / (to - from), 0, 1); };
    const qreal inside = ease(g.x() - mx, g.x(), p.x()) * (1 - ease(g.x() + g.z(), g.x() + g.z() + mx, p.x())) *
                         ease(g.y() - my, g.y(), p.y()) * (1 - ease(g.y() + g.w(), g.y() + g.w() + my, p.y()));
    guarded = std::max(guarded, inside);
  }
  return guarded;
}

QSGNode *SnakeField::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
  auto *node = static_cast<QSGGeometryNode *>(old);
  if (!m_game || width() <= 0 || height() <= 0) {
    delete node;
    return nullptr;
  }
  const auto &body = m_game->body();
  const auto &food = m_game->food();
  const int cells = int(body.size() + food.size());
  if (!node) {
    node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGVertexColorMaterial);
    node->setFlag(QSGNode::OwnsMaterial);
  }
  auto *geometry = node->geometry();
  geometry->allocate(cells * 6);
  auto *v = geometry->vertexDataAsColoredPoint2D();
  const qreal cw = width() / m_game->columns(), ch = height() / m_game->rows();
  // A cell is a square with a gap around it, like the rain's pixels.
  const qreal inset = std::min(cw, ch) * 0.12;
  auto quad = [&](QPoint cell, QColor color, qreal alpha) {
    const qreal x = cell.x() * cw + inset, y = cell.y() * ch + inset, w = cw - 2 * inset, h = ch - 2 * inset;
    alpha *= 1 - 0.85 * guardAt(QPointF((x + w / 2) / width(), (y + h / 2) / height()));
    alpha = std::clamp<qreal>(alpha, 0, 0.9);
    // The vertex colour material wants premultiplied colour.
    const uchar r = uchar(color.redF() * alpha * 255), g = uchar(color.greenF() * alpha * 255),
                b = uchar(color.blueF() * alpha * 255), a = uchar(alpha * 255);
    const QPointF corners[6] = {{x, y}, {x + w, y}, {x, y + h}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    for (const auto &c : corners)
      (v++)->set(float(c.x()), float(c.y()), r, g, b, a);
  };
  const qreal level = std::clamp<qreal>(m_level, 0, 1);
  for (const auto &f : food)
    quad(f, m_food, 0.8);
  const int n = int(body.size());
  for (int i = n - 1; i >= 0; --i) {
    // Brightest at the head, fading along the body; louder music, brighter
    // snake; the head lights up when it eats.
    const qreal along = n > 1 ? qreal(i) / (n - 1) : 0;
    QColor color = i == 0 ? m_head : QColor::fromRgbF(m_body.redF() + (m_head.redF() - m_body.redF()) * (1 - along) * 0.5,
                                                      m_body.greenF() + (m_head.greenF() - m_body.greenF()) * (1 - along) * 0.5,
                                                      m_body.blueF() + (m_head.blueF() - m_body.blueF()) * (1 - along) * 0.5);
    const qreal alpha = (0.35 + 0.45 * level) * (1 - 0.6 * along) + (i == 0 ? 0.2 + 0.4 * m_glow : 0);
    quad(body[i], color, alpha);
  }
  node->markDirty(QSGNode::DirtyGeometry);
  return node;
}
