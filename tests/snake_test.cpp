#include "snakegame.h"
#include <QtTest>
#include <random>
#include <set>

// The self-playing snake: it moves a cell at a time, eats what the beats put
// down, never bites itself, and when it grows too long it sheds its tail a
// segment at a time rather than disappearing.
class SnakeTest : public QObject {
  Q_OBJECT
  static int step(QPoint a, QPoint b, int w, int h) {
    const int dx = std::abs(a.x() - b.x()), dy = std::abs(a.y() - b.y());
    return std::min(dx, w - dx) + std::min(dy, h - dy);
  }
private slots:
  void itMovesOneCellAtATime() {
    SnakeGame snake(40, 24);
    const auto before = snake.body();
    snake.step();
    QCOMPARE(step(before.front(), snake.body().front(), 40, 24), 1);
    QCOMPARE(snake.body().size(), before.size());
  }

  void foodLandsWhereTheBeatSays() {
    SnakeGame left(64, 30, 3), right(64, 30, 4);
    QVERIFY(left.beat(0.0));
    QVERIFY(right.beat(1.0));
    QVERIFY2(left.food().front().x() < 8 || left.food().front().x() > 60, "bass is on the left");
    QVERIFY2(right.food().front().x() > 54, "treble is on the right");
    // No more than three pieces at once, however many beats come.
    for (int i = 0; i < 10; ++i)
      left.beat(0.5);
    QCOMPARE(left.food().size(), SnakeGame::MostFood);
  }

  void eatingGrowsIt() {
    SnakeGame snake(40, 24, 7);
    snake.beat(0.5);
    const auto length = snake.body().size();
    int steps = 0;
    while (!snake.step() && steps < 400)
      ++steps;
    QVERIFY2(steps < 400, "it found the food");
    for (int i = 0; i < SnakeGame::Growth; ++i)
      snake.step();
    QCOMPARE(snake.body().size(), length + SnakeGame::Growth);
  }

  void itNeverBitesItselfAndShedsGradually() {
    SnakeGame snake(48, 27, 11);
    std::mt19937 random(5);
    int longest = 0, sheds = 0;
    bool wasShedding = false;
    for (int i = 0; i < 20000; ++i) {
      if (random() % 4 == 0)
        snake.beat((random() % 1000) / 1000.0);
      const auto before = int(snake.body().size());
      snake.step();
      const auto &body = snake.body();
      std::set<std::pair<int, int>> cells;
      for (const auto &c : body)
        cells.insert({c.x(), c.y()});
      QVERIFY2(cells.size() == body.size(), qPrintable(QString("overlap at step %1").arg(i)));
      QVERIFY2(std::abs(int(body.size()) - before) <= 1, qPrintable(QString("length jumped at step %1").arg(i)));
      QVERIFY(int(body.size()) <= snake.longest());
      longest = std::max(longest, int(body.size()));
      sheds += snake.shedding() && !wasShedding;
      wasShedding = snake.shedding();
    }
    QVERIFY2(longest >= snake.longest() - 1, "it grew as long as it may");
    QVERIFY2(sheds > 0, "and shed back down");
  }

  void resizingKeepsItOnTheField() {
    SnakeGame snake(40, 24);
    for (int i = 0; i < 30; ++i)
      snake.step();
    snake.resize(12, 8);
    for (const auto &c : snake.body())
      QVERIFY(c.x() >= 0 && c.x() < 12 && c.y() >= 0 && c.y() < 8);
  }
};

QTEST_GUILESS_MAIN(SnakeTest)
#include "snake_test.moc"
