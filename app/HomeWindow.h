#pragma once

#include <QMainWindow>
#include <QPointer>
#include <functional>
#include <optional>

#include "finrenamer/Database.h"

class QPushButton;

// The start screen: Case List, File Renamer, Deficiency List and Bates Log. File Renamer and
// Deficiency List ask which case to use; Case List opens the case manager.
class HomeWindow : public QMainWindow {
    Q_OBJECT
public:
    HomeWindow(finrenamer::Database& db, const QString& dataFolder, QWidget* parent = nullptr);
    ~HomeWindow() override;

    void openCaseList();
    // With a case id (from the Case List) the case picker is skipped.
    void openFileRenamer();
    void openFileRenamerFor(std::int64_t caseId);
    void openDeficiencyList();
    void openDeficiencyListFor(std::int64_t caseId);
    void openBatesLog();
    void openVacationLetters();
    void openPdfUnlocker();
    void openPdfSplitter();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // One window per tool: shows the tool in `slot` if it is open, else builds it with `make`
    // (which may return nullptr if the user cancels). Tools are separate top-level windows.
    void showTool(QPointer<QWidget>& slot, const std::function<QWidget*()>& make);
    void openFileRenamerImpl(std::optional<std::int64_t> caseId);
    void openDeficiencyListImpl(std::optional<std::int64_t> caseId);
    QList<QPointer<QWidget>> openTools() const;

    // Asks for a case; nullopt if there are none (says so) or the user cancels.
    std::optional<std::int64_t> chooseCase(const QString& purpose);

    finrenamer::Database& db_;
    QString dataFolder_;
    QPointer<QWidget> caseList_, renamer_, deficiency_, bates_, vacation_, unlocker_, splitter_;
};
