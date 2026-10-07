#pragma once

#include <QDialog>

#include <cstdint>

#include "finrenamer/Database.h"
#include "finrenamer/RenamePlan.h"

class CaseFolderList;
class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

// "Update File Names": after an account or person edit, finds files and account
// folders still named the old way in any number of folders (typos can span
// several sessions), shows the changes, and renames them IN PLACE as one batch
// that Rename History can undo. The folder list is remembered per case.
class UpdateNamesDialog : public QDialog {
    Q_OBJECT
public:
    UpdateNamesDialog(finrenamer::Database& db, std::int64_t caseId, QWidget* parent = nullptr);
    ~UpdateNamesDialog() override;

    // Shows the folder picker and adds the chosen folder. False if cancelled.
    bool promptForFolder();
    void addFolder(const QString& folder);  // also used by tests
    bool hasFolders() const;

private:
    void scan();
    void apply();

    finrenamer::Database& db_;
    std::int64_t caseId_;
    finrenamer::RenamePlan plan_;

    CaseFolderList* folderList_ = nullptr;
    QCheckBox* subfolders_ = nullptr;
    QCheckBox* renameFolders_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* applyBtn_ = nullptr;
};
