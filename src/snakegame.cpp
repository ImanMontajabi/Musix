#include "snakegame.h"
#include <algorithm>
#include <cstdlib>

SnakeGame::SnakeGame(int columns, int rows, quint32 seed) : m_columns(std::max(4, columns)), m_rows(std::max(4, rows)), m_random(seed) {
  const QPoint start(m_columns / 4, m_rows / 2);
  for (int i = 0; i < StartLength; ++i)
    m_body.push_back(wrap(start - QPoint(i, 0)));
}

void SnakeGame::resize(int columns, int rows) {
  columns = std::max(4, columns);
  rows = std::max(4, rows);
  if (columns == m_columns && rows == m_rows)
    return;
  m_columns = columns;
  m_rows = rows;
  // Whatever falls outside comes back in from the other side.
  for (auto &cell : m_body)
    cell = wrap(cell);
  for (auto &cell : m_food)
    cell = wrap(cell);
}

QPoint SnakeGame::wrap(QPoint cell) const {
  return {((cell.x() % m_columns) + m_columns) % m_columns, ((cell.y() % m_rows) + m_rows) % m_rows};
}

int SnakeGame::distance(QPoint a, QPoint b) const {
  const int dx = std::abs(a.x() - b.x()), dy = std::abs(a.y() - b.y());
  return std::min(dx, m_columns - dx) + std::min(dy, m_rows - dy);
}

int SnakeGame::longest() const { return std::clamp(m_columns * m_rows / 5, 12, 160); }

bool SnakeGame::occupied(QPoint cell, bool ignoreTail) const {
  // The tail moves out of the way on the same step, unless the snake grows.
  const auto end = ignoreTail && m_grow == 0 && m_shedTo == 0 ? m_body.end() - 1 : m_body.end();
  return std::find(m_body.begin(), end, cell) != end;
}

QPoint SnakeGame::chooseDirection() {
  const QPoint head = m_body.front();
  const QPoint options[3] = {m_direction, QPoint(-m_direction.y(), m_direction.x()), QPoint(m_direction.y(), -m_direction.x())};
  // The nearest food, if any, is where it wants to go.
  QPoint target = head + m_direction * 8;
  int best = 1 << 30;
  for (const auto &f : m_food)
    if (const int d = distance(head, f); d < best) {
      best = d;
      target = f;
    }
  // Of the free cells ahead, left and right, take the one closest to the
  // target, and among equals keep going straight; a little wandering keeps
  // it from looking mechanical when there is nothing to eat.
  QPoint chosen{0, 0};
  int chosenScore = 1 << 30;
  for (int i = 0; i < 3; ++i) {
    const QPoint next = wrap(head + options[i]);
    if (occupied(next, true))
      continue;
    int score = distance(next, target) * 4 + (i == 0 ? 0 : 1);
    if (m_food.isEmpty())
      score += int(m_random() % 5);
    // Room to move on from there, so it does not steer into a pocket.
    int exits = 0;
    for (const auto &d : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)})
      exits += !occupied(wrap(next + d), false);
    score += (4 - exits) * 3;
    if (score < chosenScore) {
      chosenScore = score;
      chosen = options[i];
    }
  }
  return chosen;
}

bool SnakeGame::step() {
  bool shed = false;
  if (m_shedTo > 0) {
    // Shedding: the tail comes away a segment at a time while the head keeps
    // going, until it is back to its starting length.
    if (int(m_body.size()) > m_shedTo) {
      m_body.pop_back();
      shed = true;
    }
    if (int(m_body.size()) <= m_shedTo)
      m_shedTo = 0;
  }
  const QPoint direction = chooseDirection();
  if (direction.isNull()) {
    // Boxed in: nowhere to go without biting itself. It stays put this step
    // and sheds, which frees the cells it needs.
    m_shedTo = StartLength;
    m_grow = 0;
    if (!shed && int(m_body.size()) > StartLength)
      m_body.pop_back();
    return false;
  }
  m_direction = direction;
  const QPoint next = wrap(m_body.front() + m_direction);
  m_body.push_front(next);
  bool ate = false;
  if (const auto found = m_food.indexOf(next); found >= 0) {
    m_food.removeAt(found);
    m_grow += Growth;
    ate = true;
  }
  if (m_grow > 0)
    --m_grow;
  else
    m_body.pop_back();
  if (m_shedTo == 0 && int(m_body.size()) >= longest())
    m_shedTo = StartLength;
  return ate;
}

bool SnakeGame::beat(double across) {
  if (m_food.size() >= MostFood)
    return false;
  const int column = std::clamp(int(across * m_columns), 0, m_columns - 1);
  // A free cell in that stretch of columns, anywhere up or down.
  for (int attempt = 0; attempt < 40; ++attempt) {
    const int spread = std::max(1, m_columns / 16);
    const QPoint cell = wrap(QPoint(column + int(m_random() % (2 * spread + 1)) - spread, int(m_random() % m_rows)));
    if (!occupied(cell, false) && !m_food.contains(cell)) {
      m_food.append(cell);
      return true;
    }
  }
  return false;
}
