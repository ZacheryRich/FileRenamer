#pragma once

// Small helpers shared by the GUI files.

#include <QDir>
#include <QMessageBox>
#include <QObject>
#include <QStandardPaths>
#include <QString>

#include <filesystem>
#include <string>

#include "finrenamer/Database.h"
#include "finrenamer/Utf8Path.h"

namespace ui {

// The core library uses UTF-8 std::string; Qt 6's fromStdString/toStdString
// are UTF-8 too, so these are lossless.
inline QString qstr(const std::string& s) { return QString::fromStdString(s); }
inline std::string stdstr(const QString& s) { return s.toStdString(); }

inline std::filesystem::path toPath(const QString& s) { return finrenamer::pathFromUtf8(s.toStdString()); }
inline QString qpath(const std::filesystem::path& p) { return QString::fromStdString(finrenamer::utf8FromPath(p)); }

// The name shown to the user (window titles, installer). The internal name,
// "FinRenamer", stays for the data folder (%APPDATA%\FinRenamer), exe and namespaces.
inline QString appTitle() { return QStringLiteral("Zach's Litigation Tools"); }

// Small per-user settings (last folder, sort options): %APPDATA%\FinRenamer\settings.ini
inline QString settingsFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("settings.ini"));
}

// "Husband (H)", or just "John Smith" when the display name is the same.
inline QString personLabel(const finrenamer::Person& p)
{
    if (p.displayName == p.fullName) return qstr(p.fullName);
    return QStringLiteral("%1 (%2)").arg(qstr(p.fullName), qstr(p.displayName));
}

// Call first thing in the destructor of any widget that connects its children's
// signals to itself. Qt destroys a widget's children only AFTER the widget's own
// destructor has run; a child that emits a signal while being destroyed
// (QPdfDocument does, as it closes) would otherwise call into a half-destroyed
// object -- in a Debug build that's the "Called object is not of the correct
// type (class destructor may have already run)" assert.
inline void disconnectChildren(QObject* owner)
{
    const auto children = owner->findChildren<QObject*>();
    for (QObject* child : children) QObject::disconnect(child, nullptr, owner, nullptr);
}

// Runs a database call and shows any error in a message box.
// Returns true if the call succeeded.
//   DatabaseError -> a rule the user can fix; shown as-is.
//   anything else -> an unexpected failure (disk, file locked, ...).
template <class F>
bool runGuarded(QWidget* parent, F&& f)
{
    try {
        f();
        return true;
    } catch (const finrenamer::DatabaseError& e) {
        QMessageBox::warning(parent, QObject::tr("Can't do that"), qstr(e.what()));
    } catch (const std::exception& e) {
        QMessageBox::critical(parent, QObject::tr("Database error"),
                              QObject::tr("Something went wrong saving your data:\n\n%1")
                                  .arg(qstr(e.what())));
    }
    return false;
}

}  // namespace ui
