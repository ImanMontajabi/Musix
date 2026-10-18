#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

// How long the output device takes to make a sample audible, as the system
// reports it: the device's own latency, its stream's, its safety offset and
// one buffer. Bluetooth earbuds report a few hundred milliseconds here.
// An empty id means the system's default output. 0 where unknown.
double outputLatencyMs(const QByteArray &deviceId);

// What the output device is plugged into, where the system names it: a Mac's
// built-in output reports "headphones" or "speaker" as the jack is used or not,
// on one device that never goes away. Empty when the device has no such choice
// (Bluetooth, USB, HDMI), or off macOS.
QString outputPort(const QByteArray &deviceId);

// Tells when the default output, the set of outputs, or the chosen device's
// port changes. Core Audio calls from its own thread; changed() arrives on this
// object's.
class OutputPortWatcher : public QObject {
  Q_OBJECT
public:
  explicit OutputPortWatcher(QObject *parent = nullptr) : QObject(parent) {}
  ~OutputPortWatcher() override { stop(); }
  // Starts watching, or points the watch at another device.
  void setDevice(const QByteArray &deviceId);
  void stop();
  bool running() const { return m_running; }
signals:
  void changed();
private slots:
  void poke();
private:
  void rearm();
  QByteArray m_id;
  bool m_running = false;
  unsigned m_device = 0;
};
