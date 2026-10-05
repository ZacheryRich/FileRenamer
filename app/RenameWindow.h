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
class QGroupBox;
class QLabel;
class QPushButton;
class QTableWidget;

// The rename screen for one case: pick a folder, fill in an account and date
// for each PDF, preview the new names, and apply.
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

    void rebuildTable();
    void updatePreview();
    void updateSummary();
    void selectRow(int row);
    void loadEditor();
    void commitEditor();

    void goToNext();
    void apply();
    void undoLast();
    void openPdf();
    void newAccount();

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
    QCheckBox* byAccount_ = nullptr;
    QCheckBox* byYear_ = nullptr;
    QComboBox* order_ = nullptr;
    QComboBox* collisions_ = nullptr;

    // Middle
    QTableWidget* table_ = nullptr;
    QGroupBox* editorBox_ = nullptr;
    QLabel* fileLabel_ = nullptr;
    QComboBox* account_ = nullptr;
    QPushButton* newAccountBtn_ = nullptr;
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
