#pragma once
#include "snakegame.h"
#include <QColor>
#include <QQuickItem>
#include <QVariantList>
#include <memory>

// Draws the self-playing snake behind the full-screen player: every cell of
// the snake and its food in one scene-graph node, so the GPU draws the lot in
// one go and nothing is repainted on the CPU. QML drives it: advance() on
// each display frame, beat() on each beat.
class SnakeField : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(qreal cell MEMBER m_cell NOTIFY changed)
  // Overall level, 0..1, already shaped with a fast attack and slow release.
  Q_PROPERTY(qreal level MEMBER m_level NOTIFY changed)
  Q_PROPERTY(QColor head MEMBER m_head NOTIFY changed)
  Q_PROPERTY(QColor body MEMBER m_body NOTIFY changed)
  Q_PROPERTY(QColor food MEMBER m_food NOTIFY changed)
  // Areas to fade behind, as QVector4D (x, y, w, h) fractions of this item.
  Q_PROPERTY(QVariantList guards MEMBER m_guards NOTIFY changed)
  Q_PROPERTY(int length READ length NOTIFY stepped)
public:
  explicit SnakeField(QQuickItem *parent = nullptr);
  // Seconds since the last frame. Steps come faster the louder the music.
  Q_INVOKABLE void advance(qreal seconds);
  // A beat: food where the loudest band is drawn, 0 = left (bass).
  Q_INVOKABLE void beat(qreal strength, qreal across);
  int length() const { return m_game ? int(m_game->body().size()) : 0; }
  // Eating lights the head; like everything else it may not flash more than
  // three times a second.
  static constexpr qreal GlowSpacing = 0.34;
signals:
  void changed();
  void stepped();
protected:
  QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
  void geometryChange(const QRectF &now, const QRectF &before) override;
private:
  void fitGrid();
  qreal guardAt(QPointF fraction) const;
  std::unique_ptr<SnakeGame> m_game;
  qreal m_cell = 22, m_level = 0, m_pending = 0, m_clock = 0, m_lastGlow = -10, m_glow = 0;
  qreal m_drawnLevel = -1;
  bool m_wasGlowing = false;
  QColor m_head, m_body, m_food;
  QVariantList m_guards;
};
