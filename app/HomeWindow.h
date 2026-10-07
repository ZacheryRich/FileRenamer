#pragma once

#include <QMainWindow>

#include "finrenamer/Database.h"

class QPushButton;

// The start screen: Case List, File Renamer and Deficiency List. File Renamer and
// Deficiency List ask which case to use; Case List opens the case manager.
class HomeWindow : public QMainWindow {
    Q_OBJECT
public:
    HomeWindow(finrenamer::Database& db, const QString& dataFolder, QWidget* parent = nullptr);
    ~HomeWindow() override;

    void openCaseList();
    void openFileRenamer();
    void openDeficiencyList();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // Asks for a case; nullopt if there are none (says so) or the user cancels.
    std::optional<std::int64_t> chooseCase(const QString& purpose);

    finrenamer::Database& db_;
    QString dataFolder_;
};
