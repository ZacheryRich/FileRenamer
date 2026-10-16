#pragma once

#include <QDialog>

#include <filesystem>
#include <string>
#include <vector>

#include "finrenamer/PdfUnlock.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// "PDF Unlocker" (no case needed): makes unprotected copies of PDFs.
//   - Restrictions (no printing / copying / editing) are removed; no password needed.
//   - A PDF that asks for a password to open needs that password: type it per file (Password
//     column) or one password for every protected file at once. Passwords are never guessed.
// Copies go to a folder the user chooses; originals are never changed and nothing is overwritten.
// The output folder and the "include subfolders" choice are remembered (settings.ini, "unlock/...").
class UnlockDialog : public QDialog {
    Q_OBJECT
public:
    explicit UnlockDialog(QWidget* parent = nullptr);
    ~UnlockDialog() override;

    // Also used by tests.
    void addPaths(const QStringList& paths, bool includeSubfolders);  // files and/or folders
    void setOutputFolder(const QString& folder);
    void setPassword(int row, const QString& password);   // re-checks that file
    void setBatchPassword(const QString& password);
    void applyBatchPassword();                            // tries it on every file that needs one
    struct Counts {
        int unlocked = 0, skipped = 0, failed = 0;
    };
    Counts unlockAll();
    int fileCount() const { return static_cast<int>(entries_.size()); }
    QString statusOf(int row) const;
    QString resultOf(int row) const;
    QString summaryText() const;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct Entry {
        std::filesystem::path file;
        finrenamer::PdfInspection info;
        std::string password;
        std::string result;
        bool done = false;  // a copy was made
    };

    void buildUi();
    void addFiles();
    void addFolder();
    void removeSelected();
    void clearList();
    void browseOutput();
    void openOutput();
    void recheck(Entry& entry);
    void refreshTable();
    void updateButtons();
    void report(const QString& text, bool error = false);
    static QString describe(const Entry& entry);

    std::vector<Entry> entries_;
    QTableWidget* table_ = nullptr;
    QCheckBox* subfolders_ = nullptr;
    QLineEdit* batchPassword_ = nullptr;
    QPushButton* applyPasswordBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
    QLineEdit* output_ = nullptr;
    QPushButton* unlockBtn_ = nullptr;
    QPushButton* openOutputBtn_ = nullptr;
    QLabel* summary_ = nullptr;
    bool refreshing_ = false;
};
