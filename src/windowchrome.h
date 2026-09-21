#pragma once
#include <QObject>
#include <QQuickWindow>

// macOS draws a window's title bar over the window rather than above it, so
// with Qt::ExpandedClientAreaHint the app's own background runs the full
// height and the traffic lights sit on it. Qt Quick Controls already insets
// the content item to clear those buttons; what it leaves behind is the title
// text, drawn over the app's own header, and a chrome appearance still taken
// from the system rather than from the theme. That is all this does.
//
// Everywhere else it does nothing, so Main.qml calls the same thing on every
// platform and needs no test of its own.
class WindowChrome : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  Q_INVOKABLE void blend(QQuickWindow *window);

private:
  void refresh();
  QQuickWindow *m_window = nullptr;
};
