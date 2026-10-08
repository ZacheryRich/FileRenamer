#pragma once

#include <QDialog>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "finrenamer/Database.h"
#include "finrenamer/DeficiencyReport.h"
#include "finrenamer/StatementCoverage.h"

class CaseFolderList;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTextBrowser;

// "Deficiency List": searches the case's folders for statements already renamed
// by this program, works out which months each account is missing, shows the
// report and saves it as a Word document. Nothing in the folders is changed.
// The folder list is shared with Update File Names; the range and options are
// remembered per case.
class DeficiencyDialog : public QDialog {
    Q_OBJECT
public:
    DeficiencyDialog(finrenamer::Database& db, std::int64_t caseId, QWidget* parent = nullptr);
    ~DeficiencyDialog() override;

    bool promptForFolder();
    void addFolder(const QString& folder);  // also used by tests
    bool hasFolders() const;

    // The report as currently shown, and the same thing as plain text.
    const finrenamer::DeficiencyReport& report() const { return report_; }
    QString previewText() const;

    // Writes the current report to `file`. On failure returns false and says why.
    bool saveTo(const std::filesystem::path& file, QString* error = nullptr);

private:
    void rescan();
    void refresh();  // recompute the report from the cached scan
    void saveAs();
    void showUnmatched();
    void showLoose();
    void showFileList(const QString& title, const QString& text, const std::vector<std::filesystem::path>& files);
    void loadSettings();
    void saveSettings() const;
    finrenamer::Month fromMonth() const;
    finrenamer::Month toMonth() const;
    void setFromMonth(finrenamer::Month m);
    void setToMonth(finrenamer::Month m);

    finrenamer::Database& db_;
    std::int64_t caseId_;
    QString caseName_;

    std::vector<finrenamer::Account> accounts_;  // single accounts only
    finrenamer::CoverageScan scan_;
    finrenamer::DeficiencyReport report_;
    bool rangeChosen_ = false;  // false: From follows the earliest statement found
    bool loading_ = false;      // true while widgets are set from code

    CaseFolderList* folderList_ = nullptr;
    QCheckBox* subfolders_ = nullptr;
    QComboBox* fromMonthBox_ = nullptr;
    QSpinBox* fromYear_ = nullptr;
    QComboBox* toMonthBox_ = nullptr;
    QSpinBox* toYear_ = nullptr;
    QCheckBox* present_ = nullptr;
    QCheckBox* found_ = nullptr;
    QCheckBox* missing_ = nullptr;
    QListWidget* accountList_ = nullptr;
    QTextBrowser* preview_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* unmatchedBtn_ = nullptr;
    QPushButton* looseBtn_ = nullptr;
    QPushButton* saveBtn_ = nullptr;
};
