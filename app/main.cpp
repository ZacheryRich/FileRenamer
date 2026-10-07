#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

#include <memory>

#include "HomeWindow.h"
#include "QtHelpers.h"
#include "finrenamer/Database.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Keep the internal name: it decides the data folder (%APPDATA%\FinRenamer), which
    // must not move. Only the displayed name changes.
    QApplication::setApplicationName(QStringLiteral("FinRenamer"));
    QApplication::setApplicationDisplayName(ui::appTitle());

    // %APPDATA%\FinRenamer\finrenamer.db
    const QString dataFolder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataFolder);
    const QString dbFile = QDir(dataFolder).filePath(QStringLiteral("finrenamer.db"));

    std::unique_ptr<finrenamer::Database> db;
    try {
        db = std::make_unique<finrenamer::Database>(ui::toPath(dbFile));
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, ui::appTitle(),
                              QObject::tr("Could not open the database:\n%1\n\n%2")
                                  .arg(QDir::toNativeSeparators(dbFile), ui::qstr(e.what())));
        return 1;
    }

    // The start screen quits the app itself; the Case List may hide it, not close it.
    QApplication::setQuitOnLastWindowClosed(false);
    HomeWindow window(*db, QDir::toNativeSeparators(dataFolder));
    window.show();
    return app.exec();
}
