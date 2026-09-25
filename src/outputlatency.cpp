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
#else
double outputLatencyMs(const QByteArray &) { return 0; }
#endif
