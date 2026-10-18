#include "outputlatency.h"
#ifdef Q_OS_MACOS
#include <CoreAudio/CoreAudio.h>

namespace {
template <typename T> bool read(AudioObjectID id, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope, T &value) {
  AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
  UInt32 size = sizeof(T);
  return AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, &value) == noErr;
}

AudioObjectID deviceFor(const QByteArray &id) {
  AudioObjectID device = kAudioObjectUnknown;
  if (!id.isEmpty()) {
    // Qt names a device by its Core Audio UID.
    CFStringRef uid = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(id.constData()), id.size(),
                                              kCFStringEncodingUTF8, false);
    AudioObjectPropertyAddress address{kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(device);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(uid), &uid, &size, &device);
    CFRelease(uid);
  }
  if (device == kAudioObjectUnknown)
    read(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, device);
  return device;
}
} // namespace

double outputLatencyMs(const QByteArray &deviceId) {
  const AudioObjectID device = deviceFor(deviceId);
  if (device == kAudioObjectUnknown)
    return 0;
  Float64 rate = 0;
  UInt32 latency = 0, safety = 0, buffer = 0, streamLatency = 0;
  read(device, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, rate);
  read(device, kAudioDevicePropertyLatency, kAudioDevicePropertyScopeOutput, latency);
  read(device, kAudioDevicePropertySafetyOffset, kAudioDevicePropertyScopeOutput, safety);
  read(device, kAudioDevicePropertyBufferFrameSize, kAudioDevicePropertyScopeOutput, buffer);
  AudioObjectPropertyAddress streams{kAudioDevicePropertyStreams, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device, &streams, 0, nullptr, &size) == noErr && size >= sizeof(AudioObjectID)) {
    AudioObjectID stream = kAudioObjectUnknown;
    UInt32 one = sizeof(stream);
    if (AudioObjectGetPropertyData(device, &streams, 0, nullptr, &one, &stream) == noErr)
      read(stream, kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, streamLatency);
  }
  return rate > 0 ? double(latency + safety + buffer + streamLatency) / rate * 1000.0 : 0;
}

QString outputPort(const QByteArray &deviceId) {
  const AudioObjectID device = deviceFor(deviceId);
  UInt32 source = 0;
  if (device == kAudioObjectUnknown || !read(device, kAudioDevicePropertyDataSource, kAudioDevicePropertyScopeOutput, source))
    return {};
  switch (source) {
  case 'hdpn': return QStringLiteral("headphones");
  case 'ispk': return QStringLiteral("speaker");
  default: return QStringLiteral("%1%2%3%4").arg(QChar(source >> 24 & 0xff)).arg(QChar(source >> 16 & 0xff)).arg(QChar(source >> 8 & 0xff)).arg(QChar(source & 0xff));
  }
}

QByteArray defaultOutputUid() {
  AudioObjectID device = kAudioObjectUnknown;
  CFStringRef uid = nullptr;
  UInt32 size = sizeof(uid);
  AudioObjectPropertyAddress address{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  if (!read(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, device) ||
      device == kAudioObjectUnknown || AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &uid) != noErr || !uid)
    return {};
  const QByteArray result = QString::fromCFString(uid).toUtf8();
  CFRelease(uid);
  return result;
}

bool outputPresent(const QByteArray &uid) {
  if (uid.isEmpty()) return true;
  AudioObjectID device = kAudioObjectUnknown;
  CFStringRef key = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(uid.constData()), uid.size(), kCFStringEncodingUTF8, false);
  AudioObjectPropertyAddress address{kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 size = sizeof(device);
  const OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(key), &key, &size, &device);
  CFRelease(key);
  return status == noErr && device != kAudioObjectUnknown;
}

QString describeOutput(const QByteArray &deviceId) {
  const AudioObjectID device = deviceFor(deviceId);
  if (device == kAudioObjectUnknown) return QStringLiteral("none");
  CFStringRef name = nullptr;
  UInt32 size = sizeof(name), transport = 0;
  AudioObjectPropertyAddress address{kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  QString label = AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &name) == noErr && name ? QString::fromCFString(name) : QStringLiteral("?");
  if (name) CFRelease(name);
  read(device, kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, transport);
  return QStringLiteral("%1 · %2%3%4%5 · %6").arg(label).arg(QChar(transport >> 24 & 0xff)).arg(QChar(transport >> 16 & 0xff)).arg(QChar(transport >> 8 & 0xff)).arg(QChar(transport & 0xff)).arg(outputPort(deviceId));
}

namespace {
OSStatus changedOnAudioThread(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *self) {
  QMetaObject::invokeMethod(static_cast<OutputPortWatcher *>(self), "poke", Qt::QueuedConnection);
  return noErr;
}
const AudioObjectPropertyAddress portAddress{kAudioDevicePropertyDataSource, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain};
const AudioObjectPropertyAddress systemAddresses[] = {
    {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain}};
} // namespace

void OutputPortWatcher::rearm() {
  if (m_device != kAudioObjectUnknown)
    AudioObjectRemovePropertyListener(m_device, &portAddress, changedOnAudioThread, this);
  m_device = deviceFor(m_id);
  if (m_device != kAudioObjectUnknown)
    AudioObjectAddPropertyListener(m_device, &portAddress, changedOnAudioThread, this);
}
void OutputPortWatcher::setDevice(const QByteArray &deviceId) {
  m_id = deviceId;
  if (!m_running) {
    m_running = true;
    for (const auto &address : systemAddresses)
      AudioObjectAddPropertyListener(kAudioObjectSystemObject, &address, changedOnAudioThread, this);
  }
  rearm();
}
void OutputPortWatcher::stop() {
  if (!m_running) return;
  m_running = false;
  for (const auto &address : systemAddresses)
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &address, changedOnAudioThread, this);
  if (m_device != kAudioObjectUnknown)
    AudioObjectRemovePropertyListener(m_device, &portAddress, changedOnAudioThread, this);
  m_device = kAudioObjectUnknown;
}
void OutputPortWatcher::poke() {
  if (!m_running) return;
  rearm();
  emit changed();
}
#else
double outputLatencyMs(const QByteArray &) { return 0; }
QString outputPort(const QByteArray &) { return {}; }
QByteArray defaultOutputUid() { return {}; }
bool outputPresent(const QByteArray &) { return true; }
QString describeOutput(const QByteArray &) { return {}; }
void OutputPortWatcher::setDevice(const QByteArray &) {}
void OutputPortWatcher::stop() {}
void OutputPortWatcher::rearm() {}
void OutputPortWatcher::poke() {}
#endif
