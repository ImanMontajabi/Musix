#pragma once
#include <QPoint>
#include <QList>
#include <deque>
#include <random>

// A snake that plays itself, for the visualizer behind the full-screen player.
// It is not a game anyone plays: it steers toward food on its own, the music
// decides how fast it goes and when food appears, and it never dies. Walls
// wrap, and a snake that grows too long or boxes itself in sheds its tail a
// segment at a time until it is short again.
//
// Everything is deterministic for a given seed, so tests can run thousands of
// steps and know what happened.
class SnakeGame {
public:
  static constexpr int StartLength = 5, Growth = 3, MostFood = 3;

  SnakeGame(int columns, int rows, quint32 seed = 1);
  void resize(int columns, int rows);
  int columns() const { return m_columns; }
  int rows() const { return m_rows; }

  // One cell forward. Returns true when it ate.
  bool step();
  // A beat: food appears in the given fraction of the width (0 = left,
  // where the bass is drawn), at a free cell near there. Nothing appears
  // while the field already holds MostFood.
  bool beat(double across);

  const std::deque<QPoint> &body() const { return m_body; } // head first
  const QList<QPoint> &food() const { return m_food; }
  // True while it is shedding its tail after getting too long or stuck.
  bool shedding() const { return m_shedTo > 0; }
  // How long it may grow before shedding: a fifth of the field, capped.
  int longest() const;

private:
  bool occupied(QPoint cell, bool ignoreTail) const;
  QPoint wrap(QPoint cell) const;
  int distance(QPoint a, QPoint b) const; // on the wrapping grid
  QPoint chooseDirection();
  int m_columns, m_rows;
  std::deque<QPoint> m_body;
  QPoint m_direction{1, 0};
  QList<QPoint> m_food;
  int m_grow = 0;
  int m_shedTo = 0;
  std::mt19937 m_random;
};
