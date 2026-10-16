#pragma once

#include <QDialog>
#include <QStringList>

#include <vector>

#include "finrenamer/VacationLetters.h"

class QLabel;
class QPushButton;
class QTableWidget;

// The attorneys' signature pictures for the Vacation Letters, kept in settings.ini
// ("vacation/signatures"). Used by Create Letters and managed in SignaturesDialog.
std::vector<finrenamer::SignatureEntry> loadSignatures();
void saveSignatures(const std::vector<finrenamer::SignatureEntry>& entries);

// "Signatures..." in Vacation Letters: the list of attorneys with a signature picture. Add, change
// or remove attorneys as people join or leave; every change is saved at once.
class SignaturesDialog : public QDialog {
    Q_OBJECT
public:
    // `suggestions`: names found in the spreadsheet, offered when adding an attorney.
    explicit SignaturesDialog(const QStringList& suggestions, QWidget* parent = nullptr);
    ~SignaturesDialog() override;

    // Also used by tests.
    bool addEntry(const finrenamer::SignatureEntry& entry);   // false if the name is already listed
    bool removeEntry(const QString& attorney);
    int count() const;

private:
    void edit(bool adding);
    void refresh();
    void updateButtons();
    void showPreview();

    QStringList suggestions_;
    std::vector<finrenamer::SignatureEntry> entries_;
    QTableWidget* table_ = nullptr;
    QLabel* preview_ = nullptr;
    QPushButton* editBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
};
