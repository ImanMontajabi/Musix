#pragma once
#include <QByteArray>

// How long the output device takes to make a sample audible, as the system
// reports it: the device's own latency, its stream's, its safety offset and
// one buffer. Bluetooth earbuds report a few hundred milliseconds here.
// An empty id means the system's default output. 0 where unknown.
double outputLatencyMs(const QByteArray &deviceId);
