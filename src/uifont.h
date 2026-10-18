#pragma once
#include <QFontDatabase>
#include <QString>

// The family the whole interface is set in. Google Sans Flex when it is
// installed (Musix does not ship it); otherwise the system's own interface font
// on macOS, which is SF, and Noto Sans elsewhere. Qt would otherwise pick
// whatever it substitutes for a family that is not there.
inline QString interfaceFontFamily() {
  if (QFontDatabase::families().contains("Google Sans Flex"))
    return "Google Sans Flex";
#ifdef Q_OS_MACOS
  return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
#else
  return "Noto Sans";
#endif
}
