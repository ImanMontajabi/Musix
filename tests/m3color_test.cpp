// Material 3 dynamic color. The guarantees the interface relies on are that a
// requested tone really is that tone, that a hue survives the trip through the
// solver, and that the roles read off the palettes clear Material's contrast
// floors in both themes.
#include "m3color.h"
#include <QtTest>

namespace {
double contrast(const QColor &a, const QColor &b) {
  const auto luminance = [](const QColor &c) {
    const auto channel = [](double v) {
      return v <= 0.040449936 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
  };
  const double x = luminance(a), y = luminance(b);
  return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
double hueGap(double a, double b) {
  const double difference = std::fmod(std::abs(a - b), 360.0);
  return difference > 180.0 ? 360.0 - difference : difference;
}
const QList<QColor> &sources() {
  static const QList<QColor> list{QColor("#ff0000"), QColor("#00ff00"), QColor("#0000ff"),
                                  QColor("#964829"), QColor("#1f4f6b"), QColor("#d98324"),
                                  QColor("#3d2a52"), QColor("#4fa3a5"), QColor("#ffffff"),
                                  QColor("#000000"), QColor("#7f7f7f")};
  return list;
}
} // namespace

class M3ColorTest : public QObject {
  Q_OBJECT
private slots:
  // Tone is CIE L*, and it is the axis every contrast promise rests on, so the
  // solver has to reproduce it rather than approach it.
  void tonesAreExact() {
    for (double hue = 0; hue < 360; hue += 24)
      for (double chroma : {4.0, 16.0, 36.0, 80.0})
        for (double tone : {0.0, 6.0, 12.0, 30.0, 50.0, 80.0, 90.0, 98.0, 100.0}) {
          const auto color = m3::solve(hue, chroma, tone);
          QVERIFY2(color.isValid(), qPrintable(QString("no color for %1/%2/%3").arg(hue).arg(chroma).arg(tone)));
          QVERIFY2(std::abs(m3::toneOf(color) - tone) < 0.5,
                   qPrintable(QString("tone %1 became %2").arg(tone).arg(m3::toneOf(color))));
        }
  }

  // Chroma is clamped to what sRGB can hold, but hue must never drift.
  void huesSurviveTheSolver() {
    for (double hue = 0; hue < 360; hue += 12)
      for (double tone : {20.0, 40.0, 60.0, 80.0}) {
        const auto color = m3::solve(hue, 36.0, tone);
        const auto back = m3::measure(color);
        QVERIFY2(back.chroma > 1.0, "a chromatic request must not collapse to grey");
        QVERIFY2(hueGap(back.hue, hue) < 2.0,
                 qPrintable(QString("hue %1 became %2").arg(hue).arg(back.hue)));
      }
  }

  // Measuring a colour and solving for what was measured returns the colour.
  void measurementRoundTrips() {
    for (const auto &source : sources()) {
      const auto hct = m3::measure(source);
      const auto back = m3::solve(hct.hue, hct.chroma, hct.tone);
      QVERIFY2(std::abs(m3::toneOf(back) - m3::toneOf(source)) < 0.5, qPrintable(source.name()));
      if (hct.chroma > 5.0)
        QVERIFY2(hueGap(m3::measure(back).hue, hct.hue) < 2.0, qPrintable(source.name()));
    }
  }

  // A tonal palette has to climb: tone 10 is always darker than tone 90.
  void palettesClimbWithTone() {
    for (const auto &source : sources()) {
      const auto palettes = m3::palettesFor(source);
      for (const auto *palette : {&palettes.primary, &palettes.secondary, &palettes.tertiary,
                                  &palettes.neutral, &palettes.neutralVariant}) {
        double previous = -1;
        for (double tone = 0; tone <= 100; tone += 5) {
          const double measured = m3::toneOf(palette->tone(tone));
          QVERIFY2(measured > previous, qPrintable(QString("tone %1 did not rise").arg(tone)));
          previous = measured;
        }
      }
    }
  }

  // Neutral palettes tint the surfaces; they must never colour them.
  void neutralsStayNeutral() {
    for (const auto &source : sources()) {
      const auto palettes = m3::palettesFor(source);
      for (double tone = 10; tone <= 95; tone += 5) {
        QVERIFY(m3::measure(palettes.neutral.tone(tone)).chroma <= 7.0);
        QVERIFY(m3::measure(palettes.neutralVariant.tone(tone)).chroma <= 9.0);
      }
    }
  }

  // The contrast floors Material sets for text and for boundaries.
  void schemesClearContrastFloors() {
    for (const auto &source : sources())
      for (bool dark : {false, true}) {
        const auto roles = m3::scheme(source, dark);
        const auto color = [&roles](const char *name) { return roles.value(name).value<QColor>(); };
        const auto surface = color("surface");
        const QString where = source.name() + (dark ? " dark" : " light");
        const auto atLeast = [&](const char *front, const char *back, double floor) {
          const double measured = contrast(color(front), color(back));
          QVERIFY2(measured >= floor, qPrintable(QString("%1: %2 on %3 is %4, needs %5")
                                                     .arg(where, front, back)
                                                     .arg(measured)
                                                     .arg(floor)));
        };
        atLeast("onSurface", "surface", 4.5);
        atLeast("onSurfaceVariant", "surface", 4.5);
        atLeast("onPrimary", "primary", 4.5);
        atLeast("onPrimaryContainer", "primaryContainer", 4.5);
        atLeast("onSecondaryContainer", "secondaryContainer", 4.5);
        atLeast("onTertiaryContainer", "tertiaryContainer", 4.5);
        atLeast("inverseOnSurface", "inverseSurface", 4.5);
        // Boundaries and accents only carry the 3:1 floor for large shapes.
        atLeast("outline", "surface", 3.0);
        atLeast("primary", "surface", 4.5);
        // Sung draws body text and accents on the container ladder as well as
        // on the base surface, so the floors have to hold all the way up it.
        for (const char *step : {"surfaceContainerLow", "surfaceContainer", "surfaceContainerHigh",
                                 "surfaceContainerHighest"}) {
          atLeast("onSurface", step, 4.5);
          atLeast("onSurfaceVariant", step, 4.5);
          atLeast("primary", step, 4.5);
        }
        QVERIFY2(contrast(color("surfaceContainerHighest"), surface) < 3.0,
                 "surface containers are steps, not boundaries");
      }
  }

  // The container ladder has to rise away from the surface in dark themes and
  // sink towards it in light ones, the way Material stacks elevation by tone.
  void surfaceLadderRunsTheRightWay() {
    for (const auto &source : sources()) {
      for (bool dark : {false, true}) {
        const auto roles = m3::scheme(source, dark);
        const auto tone = [&roles](const char *name) {
          return m3::toneOf(roles.value(name).value<QColor>());
        };
        const QList<double> ladder{tone("surfaceContainerLowest"), tone("surfaceContainerLow"),
                                   tone("surfaceContainer"), tone("surfaceContainerHigh"),
                                   tone("surfaceContainerHighest")};
        for (int i = 1; i < ladder.size(); ++i)
          if (dark)
            QVERIFY2(ladder[i] > ladder[i - 1], "dark containers lighten as they stack");
          else
            QVERIFY2(ladder[i] < ladder[i - 1], "light containers darken as they stack");
      }
    }
  }

  // A grey cover has no hue to spread, so the scheme must still be usable.
  void greySourcesDegradeGracefully() {
    for (const auto &source : {QColor("#000000"), QColor("#ffffff"), QColor("#808080")}) {
      for (bool dark : {false, true}) {
        const auto roles = m3::scheme(source, dark);
        QVERIFY(roles.value("primary").value<QColor>().isValid());
        QVERIFY(contrast(roles.value("onSurface").value<QColor>(),
                         roles.value("surface").value<QColor>()) >= 4.5);
      }
    }
  }

  // The fixed accents are the one family that does not move with the theme.
  void fixedAccentsHoldTheirTone() {
    for (const auto &source : {QColor("#3f6ad8"), QColor("#c0392b"), QColor("#2f8f5b")}) {
      const auto light = m3::scheme(source, false);
      const auto dark = m3::scheme(source, true);
      for (const auto &accent : {"primary", "secondary", "tertiary"}) {
        const QString base(accent);
        const QString capital = base.at(0).toUpper() + base.mid(1);
        for (const auto &role : {base + "Fixed", base + "FixedDim", "on" + capital + "Fixed",
                                 "on" + capital + "FixedVariant"}) {
          QVERIFY2(light.contains(role), qPrintable(role + " is published"));
          QCOMPARE(light.value(role).value<QColor>(), dark.value(role).value<QColor>());
        }
        const auto fixed = light.value(base + "Fixed").value<QColor>();
        const auto dim = light.value(base + "FixedDim").value<QColor>();
        const auto ink = light.value("on" + capital + "Fixed").value<QColor>();
        const auto variant = light.value("on" + capital + "FixedVariant").value<QColor>();
        // Material's tones: the container at 90, its dimmer twin at 80, and the
        // two inks at 10 and 30.
        QVERIFY(qAbs(m3::toneOf(fixed) - 90) < 1.5);
        QVERIFY(qAbs(m3::toneOf(dim) - 80) < 1.5);
        QVERIFY(qAbs(m3::toneOf(ink) - 10) < 1.5);
        QVERIFY(qAbs(m3::toneOf(variant) - 30) < 1.5);
        QVERIFY2(contrast(ink, fixed) >= 4.5, "the fixed accent carries its own text");
        QVERIFY2(contrast(variant, fixed) >= 4.5, "and its secondary text too");
      }
    }
  }

  // The inverse accent is the tone the other theme would have used, which is
  // what lets a snackbar sit against the theme rather than in it.
  void inverseRolesCrossTheThemes() {
    for (const auto &source : {QColor("#3f6ad8"), QColor("#c0392b"), QColor("#2f8f5b")}) {
      const auto light = m3::scheme(source, false);
      const auto dark = m3::scheme(source, true);
      QVERIFY(light.contains("inversePrimary") && dark.contains("inversePrimary"));
      // Light schemes take the dark theme's tone 80, and dark ones tone 40.
      QVERIFY(qAbs(m3::toneOf(light.value("inversePrimary").value<QColor>()) - 80) < 1.5);
      QVERIFY(qAbs(m3::toneOf(dark.value("inversePrimary").value<QColor>()) - 40) < 1.5);
      for (const auto &roles : {light, dark}) {
        const auto surface = roles.value("inverseSurface").value<QColor>();
        QVERIFY2(contrast(roles.value("inverseOnSurface").value<QColor>(), surface) >= 4.5,
                 "the inverse surface carries its own text");
        QVERIFY2(contrast(roles.value("inversePrimary").value<QColor>(), surface) >= 3.0,
                 "and its action stands off it");
      }
    }
  }

  // Solving is on the theme's hot path; it has to stay cheap.
  void solvingIsFast() {
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 200; ++i)
      m3::scheme(QColor::fromHsvF(double(i % 100) / 100.0, 0.7, 0.8), i % 2);
    QVERIFY2(timer.elapsed() < 2000,
             qPrintable(QString("200 schemes took %1ms").arg(timer.elapsed())));
  }
};
QTEST_GUILESS_MAIN(M3ColorTest)
#include "m3color_test.moc"
