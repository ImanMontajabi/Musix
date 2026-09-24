#include "smoketest.h"
#include "backend.h"
#include "profile.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>
#include <QtMath>
#include <cstdio>

// The packaged app, launched and used the way a person would use it, before
// a DMG may be made from it. It exists because a bundle once passed every
// static check and still crashed the first time the mini player opened, and
// because the real profile must never be the one exercised: it refuses to run
// without MUSIX_PROFILE. Nothing here goes to the network.
namespace {

QStringList qmlProblems;
QtMessageHandler previousHandler = nullptr;

void collect(QtMsgType type, const QMessageLogContext &context, const QString &message) {
  // Anything QML reports from one of the app's own files is a defect; Qt's
  // own chatter about fonts and property caches is not.
  if (type != QtDebugMsg && type != QtInfoMsg && message.contains(".qml:"))
    qmlProblems << message;
  if (previousHandler)
    previousHandler(type, context, message);
}

bool waitFor(const std::function<bool()> &done, int milliseconds) {
  QElapsedTimer clock;
  clock.start();
  while (!done()) {
    if (clock.elapsed() > milliseconds)
      return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(10);
  }
  return true;
}

void settle(int milliseconds) { waitFor([] { return false; }, milliseconds); }

// Three seconds of a 440 Hz tone, written by hand so the test needs nothing
// that the bundle does not already carry.
bool writeTone(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  const quint32 rate = 22050, samples = rate * 3;
  QDataStream out(&file);
  out.setByteOrder(QDataStream::LittleEndian);
  out.writeRawData("RIFF", 4); out << quint32(36 + samples * 2);
  out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1) << rate << rate * 2 << quint16(2) << quint16(16);
  out.writeRawData("data", 4); out << quint32(samples * 2);
  for (quint32 i = 0; i < samples; ++i)
    out << qint16(8000 * qSin(2 * M_PI * 440 * i / rate));
  return file.error() == QFile::NoError;
}

} // namespace

void runSmokeTest(Backend *backend, QQuickWindow *window) {
  const auto fail = [](const QString &step) {
    fprintf(stderr, "smoke test FAILED: %s\n", qPrintable(step));
    for (const auto &problem : std::as_const(qmlProblems))
      fprintf(stderr, "  %s\n", qPrintable(problem));
    QCoreApplication::exit(1);
  };
  if (!Profile::isolated())
    return fail("refusing to run without MUSIX_PROFILE: it would use the real library");
  previousHandler = qInstallMessageHandler(collect);
  QStringList passed;
  const auto step = [&](const QString &name, bool ok) {
    if (ok) passed << name;
    return ok;
  };

  if (!step("window shown", waitFor([window] { return window->isExposed(); }, 15000)))
    return fail("the main window never appeared");
  backend->setVolume(0);
  for (const char *destination : {"search", "library", "home"}) {
    window->setProperty("destination", destination);
    settle(300);
  }
  QMetaObject::invokeMethod(window, "applyLibrary", Q_ARG(QVariant, QVariant(QStringLiteral("files"))));
  settle(300);

  const auto tone = Profile::root() + "/smoke-tone.wav";
  if (!writeTone(tone))
    return fail("could not write the audio fixture");
  backend->importLocalFiles({QUrl::fromLocalFile(tone)});
  // The import runs the bundled Python helper: this is the step that proves
  // the runtime inside the app works.
  if (!step("local import", waitFor([backend] { return !backend->importingLocal() && backend->results()->count() > 0; }, 60000)))
    return fail("importing a local file did not finish");
  backend->playItem(backend->results()->get(0));
  if (!step("playback", waitFor([backend] { return backend->playing() && backend->position() > 400; }, 15000)))
    return fail("the imported file did not play");
  backend->seek(1500);
  if (!step("seek", waitFor([backend] { return backend->position() >= 1500; }, 5000)))
    return fail("seeking did not move playback");

  // The mini player crashed on the frame after it first opened while
  // something was playing, so it is opened while something is.
  for (int round = 0; round < 3; ++round) {
    QMetaObject::invokeMethod(window, "openMiniPlayer");
    auto mini = qobject_cast<QQuickWindow *>(window->property("miniPlayer").value<QObject *>());
    if (!mini || !waitFor([mini] { return mini->isExposed(); }, 5000))
      return fail("the mini player did not open");
    settle(600);
    QMetaObject::invokeMethod(window, "restorePlayer");
    if (!waitFor([window] { return window->isExposed(); }, 5000))
      return fail("the main window did not come back from the mini player");
    settle(300);
  }
  step("mini player", true);

  for (const char *theme : {"light", "dark", "system"}) {
    backend->setTheme(theme);
    settle(300);
  }
  step("themes", true);
  if (auto settings = window->findChild<QObject *>("settingsDialog")) {
    QMetaObject::invokeMethod(settings, "open");
    settle(500);
    QMetaObject::invokeMethod(settings, "close");
    settle(300);
    step("settings", true);
  } else {
    return fail("the settings dialog was not found");
  }

  backend->stop();
  if (!qmlProblems.isEmpty())
    return fail("QML reported problems");
  fprintf(stdout, "smoke test passed: %s\n", qPrintable(passed.join(", ")));
  fflush(stdout);
  QCoreApplication::exit(0);
}
