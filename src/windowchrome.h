#pragma once
#include <QObject>
#include <QQuickWindow>

// macOS draws a window's title bar over the window rather than above it, so
// with Qt::ExpandedClientAreaHint the app's own background runs the full
// height and the traffic lights sit on it. Qt Quick Controls insets the
// content item to clear those buttons, but three things are left over and
// this is where they live: the title text, drawn over the app's own header;
// a chrome appearance still taken from the system rather than the theme; and
// the window movement the system used to do for us, which the app now has to
// ask for because its own content covers the strip the press would land in.
//
// Everywhere else this does nothing and answers 0, so Main.qml calls the same
// things on every platform and needs no test of its own.
class WindowChrome : public QObject {
  Q_OBJECT
  // How much of the top of the window the system's chrome is drawn over.
  // Zero on Linux, and zero in fullscreen where the title bar goes away.
  Q_PROPERTY(qreal inset READ inset NOTIFY insetChanged)
public:
  using QObject::QObject;
  Q_INVOKABLE void blend(QQuickWindow *window);
  // Whatever Desktop & Dock says a double-click on a title bar should do.
  Q_INVOKABLE void titleBarDoubleClick();
  // Let a frameless window's own rounded shape be the shape macOS sees, so
  // nothing square shows behind it and the shadow follows the corners.
  Q_INVOKABLE void roundCorners(QQuickWindow *window);
  qreal inset() const { return m_inset; }
signals:
  void insetChanged();

private:
  void refresh();
  QQuickWindow *m_window = nullptr;
  qreal m_inset = 0;
};
