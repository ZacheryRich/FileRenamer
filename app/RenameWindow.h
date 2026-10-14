#pragma once

#include <QDialog>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "finrenamer/Database.h"
#include "finrenamer/RenameSession.h"

class DateSpecEditor;
class PdfPreview;
class QSplitter;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// The rename screen for one case: pick a folder, say what each PDF is (a statement
// for an account, an email, text, letter or other file) and its date, preview the new names, and apply.
class RenameWindow : public QDialog {
    Q_OBJECT
public:
    RenameWindow(finrenamer::Database& db, std::int64_t caseId, QWidget* parent = nullptr);
    ~RenameWindow() override;

    // Shows the folder picker. Returns false if the user cancelled.
    bool promptForFolder();

    // Opens a folder directly (the picker calls this; tests use it too).
    void openFolder(const QString& folder);

public slots:
    void reject() override;  // asks before throwing away unapplied entries

private:
    void buildUi();
    void refreshFolder();
    void reloadAccounts();
    int accountIndex(std::optional<std::int64_t> id) const;
    void fillNumbers(std::optional<std::int64_t> accountId, const std::string& select);

    void rebuildTable();
    void updatePreview();
    void updateSummary();
    void selectRow(int row);
    void loadEditor();
    void commitEditor();
    finrenamer::DocumentInfo editorDocument() const;  // the kind and typed fields as shown
    void updateKindFields();                          // which fields the chosen kind shows
    void kindChanged();

    void goToNext();
    void apply();
    void undoLast();
    void openPdf();
    void newAccount();
    void editAccount();
    void updateAccountButtons();

    finrenamer::PlanOptions options() const;
    std::optional<std::size_t> currentRow() const;
    void saveSettings() const;

    finrenamer::Database& db_;
    std::int64_t caseId_;
    std::unique_ptr<finrenamer::RenameSession> session_;
    std::vector<finrenamer::PlannedMove> preview_;
    bool loadingEditor_ = false;

    // Top
    QLabel* folderLabel_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
    QPushButton* statementBtn_ = nullptr;  // opens statementDialog_
    QDialog* statementDialog_ = nullptr;   // "Statement Options": folder sorting, statements only
    QCheckBox* byAccount_ = nullptr;
    QCheckBox* byYear_ = nullptr;
    QComboBox* order_ = nullptr;
    QComboBox* collisions_ = nullptr;

    // Middle
    QTableWidget* table_ = nullptr;
    QGroupBox* editorBox_ = nullptr;
    QLabel* fileLabel_ = nullptr;
    QFormLayout* form_ = nullptr;
    QComboBox* kind_ = nullptr;       // Statement / Email / Text / Letter / Other
    QComboBox* textStyle_ = nullptr;  // single message / conversation / group chat
    QLineEdit* label_ = nullptr;
    QLineEdit* from_ = nullptr;
    QLineEdit* to_ = nullptr;
    QLineEdit* description_ = nullptr;
    QWidget* accountBox_ = nullptr;
    QWidget* numberBox_ = nullptr;
    std::vector<QWidget*> editFields_;  // everything that is locked once a file is renamed or skipped
    QComboBox* account_ = nullptr;
    QComboBox* number_ = nullptr;  // the chosen account's numbers; "" data = current
    std::vector<finrenamer::Account> accounts_;
    QPushButton* newAccountBtn_ = nullptr;
    QPushButton* editAccountBtn_ = nullptr;
    DateSpecEditor* date_ = nullptr;
    QCheckBox* skip_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QPushButton* nextBtn_ = nullptr;
    QLabel* rowStatus_ = nullptr;

    // Right
    PdfPreview* pdf_ = nullptr;
    QSplitter* leftSplitter_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;

    // Bottom
    QLabel* summary_ = nullptr;
    QPushButton* undoBtn_ = nullptr;
    QPushButton* applyBtn_ = nullptr;
};
